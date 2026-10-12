/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_internal.h"

#include <ArduinoJson.hpp>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mbedtls/error.h>
#include <mqtt_client.h>
#include <mutex>
#include <optional>

namespace {

constexpr char Tag[]             = "HAL-EZDATA";
constexpr char RegistrationUrl[] = "https://ezdata2.m5stack.com/api/v2/device/registerMac";
constexpr char DeviceType[]      = "ChainCaptain";
// Keep the existing EzData broker transport until its TLS endpoint is confirmed.
constexpr char BrokerUri[]                  = "mqtt://uiflow2.m5stack.com:1883";
constexpr std::size_t MaximumNodes          = 32;
constexpr std::size_t MaximumMessageSize    = 2048;
constexpr std::size_t MaximumQueuedMessages = 8;
constexpr std::size_t MaximumTokenSize      = 256;
constexpr std::uint32_t UploadIntervalMs    = 1000;
constexpr std::uint32_t RequestTimeoutMs    = 8000;
constexpr std::uint32_t MaximumRetryMs      = 30000;
constexpr std::uint32_t PairRetryMs         = 30000;
constexpr unsigned MaximumPairAttempts      = 3;
constexpr int CommandAdd                    = 100;
constexpr int CommandUpdate                 = 101;
constexpr int CommandUserUpdate             = 107;
constexpr int CommandPair                   = 112;

using Value = std::variant<bool, int, std::string>;

struct Field {
    std::string name;
    Value desired;
    std::optional<Value> confirmed;
    bool retired = false;
    bool refresh = true;
};

struct PendingWrite {
    std::string name;
    Value value;
    int command          = CommandAdd;
    std::uint32_t sentAt = 0;
};

struct RgbWrite {
    std::uint32_t deviceId;
    RgbColor color;
    std::uint32_t generation;
};

struct RgbOverride {
    std::uint32_t deviceId;
    RgbColor color;
    std::uint32_t expiresAt;
};

std::uint32_t nowMs()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

bool reached(std::uint32_t now, std::uint32_t deadline)
{
    return static_cast<std::int32_t>(now - deadline) >= 0;
}

void logRegistrationTransportError(esp_http_client_handle_t client)
{
    const int socket_error = esp_http_client_get_errno(client);
    int tls_code           = 0;
    int tls_flags          = 0;
    const auto tls_error   = esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags);
    char description[128]  = {};
    if (tls_code != 0) mbedtls_strerror(tls_code, description, sizeof(description));
    ESP_LOGW(Tag, "registration transport: errno=%d, TLS=%s, mbedTLS=%d (%s), cert_flags=0x%x", socket_error,
             esp_err_to_name(tls_error), tls_code, description, static_cast<unsigned>(tls_flags));
    ESP_LOGW(Tag, "registration heap: internal free=%u largest=%u, PSRAM free=%u largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
}

void logRegistrationResponse(const std::string& body)
{
    if (body.empty()) return;
    ArduinoJson::JsonDocument document;
    const auto error = ArduinoJson::deserializeJson(document, body);
    if (error) {
        ESP_LOGW(Tag, "registration response: %u bytes, JSON error: %s", static_cast<unsigned>(body.size()),
                 error.c_str());
        return;
    }
    // The data field can contain a device token, even on an unexpected HTTP status.
    ESP_LOGW(Tag, "registration response: code=%d, msg=%.256s, message=%.256s", document["code"].as<int>(),
             document["msg"] | "", document["message"] | "");
}

std::string nodePrefix(std::uint32_t id)
{
    char prefix[24];
    std::snprintf(prefix, sizeof(prefix), "CHAIN.%08lX.", static_cast<unsigned long>(id));
    return prefix;
}

std::string rgbValue(RgbColor color)
{
    char value[8];
    std::snprintf(value, sizeof(value), "#%02X%02X%02X", color.red, color.green, color.blue);
    return value;
}

bool parseRgb(std::string_view value, RgbColor& color)
{
    if (value.size() != 7 || value.front() != '#') return false;
    unsigned parsed = 0;
    for (char ch : value.substr(1)) {
        unsigned nibble;
        if (ch >= '0' && ch <= '9')
            nibble = ch - '0';
        else if (ch >= 'a' && ch <= 'f')
            nibble = ch - 'a' + 10;
        else if (ch >= 'A' && ch <= 'F')
            nibble = ch - 'A' + 10;
        else
            return false;
        parsed = (parsed << 4) | nibble;
    }
    color = {static_cast<std::uint8_t>(parsed >> 16), static_cast<std::uint8_t>(parsed >> 8),
             static_cast<std::uint8_t>(parsed)};
    return true;
}

class EzDataRuntime {
public:
    bool start()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_desired_running.load()) return true;
        ++_generation;
        _desired_running.store(true);
        _wifi_state.store(WifiState::Off);
        _upload_enabled.store(GetHAL().deviceSettings().ezDataUploadEnabled);
        _status   = {EzDataState::WaitingForWifi, {}, {}};
        _snapshot = {};
        _rgb_writes.clear();
        if (_task_running) return true;
        _task_running = true;
        if (xTaskCreate(taskEntry, "chain_ezdata", 8192, this, 2, nullptr) == pdPASS) return true;
        _task_running = false;
        _desired_running.store(false);
        _wifi_state.store(WifiState::Off);
        _status = {EzDataState::Failed, {}, "Unable to start EzData"};
        return false;
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _desired_running.store(false);
        _wifi_state.store(WifiState::Off);
        ++_generation;
        _status   = {};
        _snapshot = {};
        _rgb_writes.clear();
    }

    void update()
    {
        if (!_desired_running.load()) return;
        _wifi_state.store(GetHAL().getWifiState());
        _upload_enabled.store(GetHAL().deviceSettings().ezDataUploadEnabled);
        std::vector<RgbWrite> writes;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_rgb_writes.empty()) return;
            writes.swap(_rgb_writes);
        }
        for (const auto& write : writes) {
            if (active(write.generation)) GetHAL().setChainRgb(write.deviceId, write.color);
        }
    }

    void publish(const ChainSnapshot& snapshot)
    {
        if (!snapshot.initialScanComplete || snapshot.left.size() + snapshot.right.size() > MaximumNodes) return;
        std::lock_guard<std::mutex> lock(_mutex);
        if (_desired_running.load()) _snapshot = snapshot;
    }

    EzDataStatus status()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _status;
    }

private:
    struct HttpResponse {
        EzDataRuntime* runtime;
        std::uint32_t generation;
        std::string body;
        bool oversized = false;
    };

    bool active(std::uint32_t generation) const
    {
        return _desired_running.load() && _generation.load() == generation;
    }

    void setStatus(EzDataState state, std::string error = {})
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (!active(_session_generation)) return;
        _status.state     = state;
        _status.lastError = std::move(error);
        if (state != EzDataState::Connected) _status.pairingCode.clear();
    }

    static void taskEntry(void* context)
    {
        static_cast<EzDataRuntime*>(context)->run();
        vTaskDelete(nullptr);
    }

    static esp_err_t httpEvent(esp_http_client_event_t* event)
    {
        auto& response = *static_cast<HttpResponse*>(event->user_data);
        if (!response.runtime->active(response.generation)) return ESP_FAIL;
        if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
        if (response.body.size() + event->data_len > MaximumMessageSize) {
            response.oversized = true;
            return ESP_FAIL;
        }
        response.body.append(static_cast<const char*>(event->data), event->data_len);
        return ESP_OK;
    }

    bool registerDevice()
    {
        setStatus(EzDataState::Registering);
        std::uint8_t mac[6]   = {};
        const auto mac_result = esp_efuse_mac_get_default(mac);
        if (mac_result != ESP_OK) {
            ESP_LOGW(Tag, "registration failed to read factory MAC: %s", esp_err_to_name(mac_result));
            return false;
        }
        char mac_text[13];
        std::snprintf(mac_text, sizeof(mac_text), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4],
                      mac[5]);
        _client_id = std::string("ez") + mac_text + "ez";

        ArduinoJson::JsonDocument request;
        request["deviceType"] = DeviceType;
        request["mac"]        = mac_text;
        std::string payload;
        ArduinoJson::serializeJson(request, payload);

        HttpResponse response{this, _session_generation, {}};
        esp_http_client_config_t config{};
        config.url                   = RegistrationUrl;
        config.method                = HTTP_METHOD_POST;
        config.crt_bundle_attach     = esp_crt_bundle_attach;
        config.timeout_ms            = RequestTimeoutMs;
        config.disable_auto_redirect = true;
        config.event_handler         = httpEvent;
        config.user_data             = &response;
        config.buffer_size           = 1024;
        config.buffer_size_tx        = 512;
        auto client                  = esp_http_client_init(&config);
        if (client == nullptr) {
            ESP_LOGW(Tag, "registration failed to create HTTP client");
            return false;
        }
        esp_err_t result = esp_http_client_set_header(client, "Content-Type", "application/json");
        if (result == ESP_OK) result = esp_http_client_set_post_field(client, payload.c_str(), payload.size());
        if (result == ESP_OK) result = esp_http_client_perform(client);
        const int status = esp_http_client_get_status_code(client);
        if (active(_session_generation) && result != ESP_OK) logRegistrationTransportError(client);
        esp_http_client_cleanup(client);
        if (!active(_session_generation)) return false;
        if (result != ESP_OK || status != 200 || response.oversized) {
            ESP_LOGW(Tag, "registration failed: HTTP %d, %s, response_bytes=%u, oversized=%d", status,
                     esp_err_to_name(result), static_cast<unsigned>(response.body.size()), response.oversized);
            logRegistrationResponse(response.body);
            return false;
        }

        ArduinoJson::JsonDocument document;
        if (const auto error = ArduinoJson::deserializeJson(document, response.body)) {
            ESP_LOGW(Tag, "registration returned invalid JSON: %s (%u bytes)", error.c_str(),
                     static_cast<unsigned>(response.body.size()));
            return false;
        }
        if (document["code"] != 200 || document["msg"] != "OK" || !document["data"].is<std::string>()) {
            ESP_LOGW(Tag, "registration rejected for %s: code %d", DeviceType, document["code"].as<int>());
            logRegistrationResponse(response.body);
            return false;
        }
        const std::string token = document["data"].as<std::string>();
        if (token.empty() || token.size() > MaximumTokenSize ||
            !std::all_of(token.begin(), token.end(), [](unsigned char ch) {
                return ch > ' ' && ch < 127 && ch != '/' && ch != '+' && ch != '#';
            })) {
            ESP_LOGW(Tag, "registration returned an invalid token");
            return false;
        }
        _token           = token;
        _publish_topic   = "$ezdata/" + token + "/up";
        _subscribe_topic = "$ezdata/" + token + "/down";
        ESP_LOGI(Tag, "device registered");
        return true;
    }

    static void mqttEvent(void* context, esp_event_base_t, std::int32_t id, void* data)
    {
        auto& self  = *static_cast<EzDataRuntime*>(context);
        auto& event = *static_cast<esp_mqtt_event_t*>(data);
        if (!self.active(self._session_generation)) return;
        switch (id) {
            case MQTT_EVENT_CONNECTED:
                self._connected.store(true);
                break;
            case MQTT_EVENT_SUBSCRIBED:
                self._subscribed.store(true);
                break;
            case MQTT_EVENT_DISCONNECTED:
                self._connected.store(false);
                self._connection_failed.store(true);
                break;
            case MQTT_EVENT_ERROR:
                if (event.error_handle && event.error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED &&
                    (event.error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_BAD_USERNAME ||
                     event.error_handle->connect_return_code == MQTT_CONNECTION_REFUSE_NOT_AUTHORIZED)) {
                    self._credentials_rejected.store(true);
                }
                self._connection_failed.store(true);
                break;
            case MQTT_EVENT_DATA:
                self.receiveMessage(event);
                break;
            default:
                break;
        }
    }

    void receiveMessage(const esp_mqtt_event_t& event)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (event.current_data_offset == 0) {
            _incoming.clear();
            _incoming_total = 0;
            if (event.total_data_len <= 0 || event.total_data_len > MaximumMessageSize || event.topic_len <= 0 ||
                std::string_view(event.topic, event.topic_len) != _subscribe_topic)
                return;
            _incoming_total = event.total_data_len;
        }
        if (_incoming_total == 0 || event.data_len < 0 || event.current_data_offset != _incoming.size() ||
            _incoming.size() + event.data_len > _incoming_total) {
            _incoming.clear();
            _incoming_total = 0;
            return;
        }
        _incoming.append(event.data, event.data_len);
        if (_incoming.size() != _incoming_total) return;
        if (_messages.size() < MaximumQueuedMessages) _messages.push_back(std::move(_incoming));
        _incoming.clear();
        _incoming_total = 0;
    }

    bool connectMqtt()
    {
        setStatus(EzDataState::Connecting);
        _connected.store(false);
        _subscribed.store(false);
        _connection_failed.store(false);
        _credentials_rejected.store(false);
        esp_mqtt_client_config_t config{};
        config.broker.address.uri                  = BrokerUri;
        config.credentials.client_id               = _client_id.c_str();
        config.credentials.username                = _token.c_str();
        config.credentials.authentication.password = "";
        config.network.disable_auto_reconnect      = true;
        config.network.timeout_ms                  = 3000;
        config.task.priority                       = 2;
        config.task.stack_size                     = 4096;
        config.buffer.size                         = 1024;
        config.buffer.out_size                     = 512;
        config.outbox.limit                        = 2048;
        config.session.keepalive                   = 30;
        _mqtt                                      = esp_mqtt_client_init(&config);
        if (_mqtt == nullptr) return false;
        if (esp_mqtt_client_register_event(_mqtt, MQTT_EVENT_ANY, mqttEvent, this) != ESP_OK ||
            esp_mqtt_client_start(_mqtt) != ESP_OK) {
            destroyMqtt();
            return false;
        }
        _connection_started_at = nowMs();
        _subscribe_sent        = false;
        _ready                 = false;
        return true;
    }

    void destroyMqtt()
    {
        if (_mqtt) {
            esp_mqtt_client_stop(_mqtt);
            esp_mqtt_client_destroy(_mqtt);
            _mqtt = nullptr;
        }
        _connected.store(false);
        _subscribed.store(false);
        _ready              = false;
        _pair_pending       = false;
        _pair_retry_pending = false;
        _pending_write.reset();
        std::lock_guard<std::mutex> lock(_mutex);
        _incoming.clear();
        _incoming_total = 0;
        _messages.clear();
        _rgb_writes.clear();
    }

    bool sendPacket(int command, const std::string& name = {}, const Value* value = nullptr)
    {
        if (!_connected.load() || !active(_session_generation)) return false;
        ArduinoJson::JsonDocument document;
        document["deviceToken"] = _token;
        auto body               = document["body"].to<ArduinoJson::JsonObject>();
        body["requestType"]     = command;
        if (!name.empty()) body["name"] = name;
        if (value) std::visit([&](const auto& item) { body["value"] = item; }, *value);
        std::string payload;
        ArduinoJson::serializeJson(document, payload);
        return esp_mqtt_client_publish(_mqtt, _publish_topic.c_str(), payload.c_str(), payload.size(), 0, 0) >= 0;
    }

    void prepareFields()
    {
        ChainSnapshot snapshot;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            snapshot = _snapshot;
        }
        if (!snapshot.initialScanComplete) return;
        _rgb_overrides.erase(std::remove_if(_rgb_overrides.begin(), _rgb_overrides.end(),
                                            [](const auto& item) { return reached(nowMs(), item.expiresAt); }),
                             _rgb_overrides.end());

        std::vector<Field> next;
        next.reserve(2 + (snapshot.left.size() + snapshot.right.size()) * 6);
        const auto add = [&](std::string name, Value value) {
            const auto previous =
                std::find_if(_fields.begin(), _fields.end(), [&](const auto& field) { return field.name == name; });
            if (previous == _fields.end()) {
                next.push_back({std::move(name), std::move(value), {}, false, true});
            } else {
                auto field    = *previous;
                field.desired = std::move(value);
                field.retired = false;
                next.push_back(std::move(field));
            }
        };
        add("CHAIN.LEFT.COUNT", static_cast<int>(snapshot.left.size()));
        add("CHAIN.RIGHT.COUNT", static_cast<int>(snapshot.right.size()));
        for (const auto* side : {&snapshot.left, &snapshot.right}) {
            for (const auto& node : *side) {
                const auto prefix = nodePrefix(node.id);
                add(prefix + "CONNECTED", true);
                RgbColor color      = std::visit([](const auto& data) { return data.rgb; }, node.data);
                const auto override = std::find_if(_rgb_overrides.begin(), _rgb_overrides.end(),
                                                   [&](const auto& item) { return item.deviceId == node.id; });
                if (override != _rgb_overrides.end()) {
                    const bool applied = color.red == override->color.red && color.green == override->color.green &&
                                         color.blue == override->color.blue;
                    if (applied || reached(nowMs(), override->expiresAt))
                        _rgb_overrides.erase(override);
                    else
                        color = override->color;
                }
                add(prefix + "RGB", rgbValue(color));
                if (const auto* data = std::get_if<ChainKeyData>(&node.data)) {
                    add(prefix + "PRESSED", data->pressed);
                } else if (const auto* data = std::get_if<ChainAngleData>(&node.data)) {
                    add(prefix + "ANGLE", static_cast<int>(data->angleDegrees));
                } else if (const auto* data = std::get_if<ChainJoystickData>(&node.data)) {
                    add(prefix + "X", static_cast<int>(data->x));
                    add(prefix + "Y", static_cast<int>(data->y));
                    add(prefix + "PRESSED", data->pressed);
                } else if (const auto* data = std::get_if<ChainBuzzerData>(&node.data)) {
                    add(prefix + "MODE", static_cast<int>(data->mode));
                    add(prefix + "FREQUENCY", static_cast<int>(data->frequency));
                    add(prefix + "DUTY", static_cast<int>(data->duty));
                    add(prefix + "ENABLED", data->enabled);
                } else if (const auto* data = std::get_if<ChainEncoderData>(&node.data)) {
                    add(prefix + "VALUE", static_cast<int>(data->value));
                    add(prefix + "DELTA", static_cast<int>(data->delta));
                    add(prefix + "PRESSED", data->pressed);
                } else if (const auto* data = std::get_if<ChainMonoData>(&node.data)) {
                    add(prefix + "MODE", static_cast<int>(data->mode));
                    add(prefix + "BRIGHTNESS", static_cast<int>(data->brightness));
                } else if (const auto* data = std::get_if<ChainRgbData>(&node.data)) {
                    add(prefix + "MODE", static_cast<int>(data->mode));
                    add(prefix + "BRIGHTNESS", static_cast<int>(data->brightness));
                } else if (const auto* data = std::get_if<ChainPirData>(&node.data)) {
                    add(prefix + "DETECTED", data->detected);
                } else if (const auto* data = std::get_if<ChainTofData>(&node.data)) {
                    add(prefix + "DISTANCE", static_cast<int>(data->distanceMm));
                }
            }
        }

        std::size_t tombstones = 0;
        for (auto& field : _fields) {
            if (field.name.size() < 10 || field.name.compare(field.name.size() - 10, 10, ".CONNECTED") != 0) continue;
            const auto retained =
                std::find_if(next.begin(), next.end(), [&](const auto& item) { return item.name == field.name; });
            if (retained != next.end() || (field.retired && field.confirmed == std::optional<Value>(false))) continue;
            if (++tombstones > MaximumNodes) break;
            field.desired = false;
            field.retired = true;
            next.push_back(std::move(field));
        }
        _fields = std::move(next);
    }

    void handleUserUpdate(ArduinoJson::JsonObjectConst body)
    {
        if (!body["name"].is<std::string>() || !body["value"].is<std::string>()) return;
        const auto name = body["name"].as<std::string>();
        RgbColor color;
        if (!parseRgb(body["value"].as<std::string>(), color)) return;
        std::lock_guard<std::mutex> lock(_mutex);
        if (!active(_session_generation)) return;
        for (const auto* side : {&_snapshot.left, &_snapshot.right}) {
            for (const auto& node : *side) {
                if (name != nodePrefix(node.id) + "RGB") continue;
                const auto pending = std::find_if(_rgb_writes.begin(), _rgb_writes.end(),
                                                  [&](const auto& item) { return item.deviceId == node.id; });
                if (pending != _rgb_writes.end())
                    pending->color = color;
                else if (_rgb_writes.size() < MaximumNodes)
                    _rgb_writes.push_back({node.id, color, _session_generation});
                const auto override = std::find_if(_rgb_overrides.begin(), _rgb_overrides.end(),
                                                   [&](const auto& item) { return item.deviceId == node.id; });
                const RgbOverride next{node.id, color, nowMs() + 2000};
                if (override != _rgb_overrides.end())
                    *override = next;
                else if (_rgb_overrides.size() < MaximumNodes)
                    _rgb_overrides.push_back(next);
                const auto field =
                    std::find_if(_fields.begin(), _fields.end(), [&](const auto& item) { return item.name == name; });
                if (field != _fields.end()) {
                    field->desired   = rgbValue(color);
                    field->confirmed = field->desired;
                    field->refresh   = false;
                }
                return;
            }
        }
    }

    void handleMessage(const std::string& message)
    {
        ArduinoJson::JsonDocument document;
        if (ArduinoJson::deserializeJson(document, message) || !document["code"].is<int>() ||
            !document["cmd"].is<int>())
            return;
        const int code    = document["code"].as<int>();
        const int command = document["cmd"].as<int>();
        const auto body   = document["body"].as<ArduinoJson::JsonObjectConst>();
        if (command == CommandUserUpdate && code == 200) {
            handleUserUpdate(body);
            return;
        }
        if (_pair_pending && (command == CommandPair || command == 500)) {
            _pair_pending = false;
            if (code == 200 && body["pairCode"].is<std::string>()) {
                const auto pair = body["pairCode"].as<std::string>();
                if (!pair.empty() && pair.size() <= 32 &&
                    std::all_of(pair.begin(), pair.end(), [](unsigned char ch) { return std::isalnum(ch); })) {
                    std::lock_guard<std::mutex> lock(_mutex);
                    if (active(_session_generation)) {
                        _status.pairingCode = pair;
                        _status.lastError.clear();
                    }
                    ESP_LOGI(Tag, "pairing code ready");
                    return;
                }
            }
            setStatus(EzDataState::Connected, "Pairing code unavailable");
            _pair_retry_at      = nowMs() + PairRetryMs;
            _pair_retry_pending = _pair_attempts < MaximumPairAttempts;
            ESP_LOGW(Tag, "pairing request failed: %d", code);
            return;
        }
        if (!_pending_write || (command != _pending_write->command && command != 500)) return;
        if (body["name"].is<std::string>() && body["name"].as<std::string>() != _pending_write->name) return;
        if (code != 200) {
            if (_pending_write->command == CommandAdd) {
                if (!_upload_enabled.load()) {
                    _pending_write.reset();
                    return;
                }
                // Existing device data survives app sessions; an add may need an update instead.
                _pending_write->command = CommandUpdate;
                _pending_write->sentAt  = nowMs();
                if (!sendPacket(CommandUpdate, _pending_write->name, &_pending_write->value)) {
                    _connection_failed.store(true);
                }
            } else {
                const auto field = std::find_if(_fields.begin(), _fields.end(),
                                                [&](const auto& item) { return item.name == _pending_write->name; });
                if (field != _fields.end()) field->confirmed.reset();
                setStatus(EzDataState::Failed, "EzData rejected an update");
                ESP_LOGW(Tag, "data update rejected: %d", code);
                _connection_failed.store(true);
            }
            return;
        }
        if (command == 500) return;
        const auto field = std::find_if(_fields.begin(), _fields.end(),
                                        [&](const auto& item) { return item.name == _pending_write->name; });
        if (field != _fields.end()) {
            field->confirmed = _pending_write->value;
            field->refresh   = false;
        }
        _pending_write.reset();
    }

    void processMessages()
    {
        std::vector<std::string> messages;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_messages.empty()) return;
            messages.swap(_messages);
        }
        for (const auto& message : messages) {
            if (active(_session_generation)) handleMessage(message);
        }
    }

    void publishNextField(std::uint32_t now)
    {
        if (_pair_pending) {
            if (now - _pair_sent_at >= RequestTimeoutMs) _connection_failed.store(true);
            return;
        }
        if (_pending_write) {
            // Reconnect on timeout so an uncorrelated late response cannot acknowledge another field.
            if (now - _pending_write->sentAt >= RequestTimeoutMs) _connection_failed.store(true);
            return;
        }
        if (_pair_retry_pending && reached(now, _pair_retry_at)) {
            requestPairCode(now);
            return;
        }
        if (!_upload_enabled.load()) return;
        if (_fields.empty()) return;
        for (std::size_t visited = 0; visited < _fields.size(); ++visited) {
            _field_cursor %= _fields.size();
            const auto& field = _fields[_field_cursor++];
            if (!field.refresh && field.confirmed && *field.confirmed == field.desired) continue;
            const int command = field.confirmed ? CommandUpdate : CommandAdd;
            if (sendPacket(command, field.name, &field.desired)) {
                _pending_write = PendingWrite{field.name, field.desired, command, now};
            } else {
                _connection_failed.store(true);
            }
            return;
        }
    }

    void failAndRetry(std::string message, std::uint32_t now)
    {
        destroyMqtt();
        setStatus(EzDataState::Failed, std::move(message));
        _retry_at    = now + _retry_delay;
        _retry_delay = std::min(_retry_delay * 2, MaximumRetryMs);
    }

    void requestPairCode(std::uint32_t now)
    {
        ++_pair_attempts;
        _pair_retry_pending = false;
        _pair_pending       = sendPacket(CommandPair);
        _pair_sent_at       = now;
        if (!_pair_pending) _connection_failed.store(true);
    }

    void step(std::uint32_t now)
    {
        const bool upload_enabled = _upload_enabled.load();
        if (upload_enabled && !_upload_was_enabled) {
            _refresh_at = now;
            for (auto& field : _fields) field.refresh = true;
        }
        _upload_was_enabled = upload_enabled;

        const auto wifi_state = _wifi_state.load();
        if (wifi_state != WifiState::Connected) {
            if (_mqtt) destroyMqtt();
            if (wifi_state == WifiState::Failed)
                setStatus(EzDataState::Failed, "Wi-Fi connection failed");
            else
                setStatus(EzDataState::WaitingForWifi);
            _retry_at = now;
            return;
        }
        if (_mqtt == nullptr) {
            if (!reached(now, _retry_at)) return;
            if (_token.empty() && !registerDevice()) {
                failAndRetry("EzData registration failed", nowMs());
                return;
            }
            if (!active(_session_generation)) return;
            if (!connectMqtt()) failAndRetry("Unable to connect to EzData", nowMs());
            return;
        }
        if (_connection_failed.load() || (!_ready && now - _connection_started_at >= RequestTimeoutMs)) {
            if (_credentials_rejected.load()) _token.clear();
            failAndRetry("EzData connection interrupted", now);
            return;
        }
        if (!_connected.load()) return;
        if (!_subscribe_sent) {
            if (esp_mqtt_client_subscribe(_mqtt, _subscribe_topic.c_str(), 0) < 0) {
                failAndRetry("EzData subscription failed", now);
                return;
            }
            _subscribe_sent = true;
        }
        if (!_subscribed.load()) return;
        if (!_ready) {
            _ready       = true;
            _retry_delay = 1000;
            _refresh_at  = now;
            for (auto& field : _fields) field.refresh = true;
            setStatus(EzDataState::Connected);
            ESP_LOGI(Tag, "connected");
            _pair_attempts = 0;
            requestPairCode(now);
        }
        processMessages();
        if (_connection_failed.load()) return;
        if (reached(now, _refresh_at)) {
            _refresh_at = now + UploadIntervalMs;
            prepareFields();
        }
        publishNextField(now);
    }

    void run()
    {
        while (true) {
            _session_generation = _generation.load();
            _retry_at           = nowMs();
            _retry_delay        = 1000;
            _upload_was_enabled = false;
            _token.clear();
            _fields.clear();
            _rgb_overrides.clear();
            while (active(_session_generation)) {
                step(nowMs());
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            destroyMqtt();
            _token.clear();
            _publish_topic.clear();
            _subscribe_topic.clear();
            std::vector<Field>{}.swap(_fields);
            std::vector<RgbOverride>{}.swap(_rgb_overrides);
            std::lock_guard<std::mutex> lock(_mutex);
            std::vector<RgbWrite>{}.swap(_rgb_writes);
            std::vector<std::string>{}.swap(_messages);
            std::string{}.swap(_incoming);
            if (_desired_running.load()) continue;
            _task_running = false;
            return;
        }
    }

    std::mutex _mutex;
    std::atomic<bool> _desired_running{false};
    std::atomic<WifiState> _wifi_state{WifiState::Off};
    std::atomic<bool> _upload_enabled{true};
    std::atomic<std::uint32_t> _generation{0};
    bool _task_running = false;
    EzDataStatus _status;
    ChainSnapshot _snapshot;
    std::vector<RgbWrite> _rgb_writes;
    std::vector<std::string> _messages;
    std::string _incoming;
    std::size_t _incoming_total = 0;

    std::uint32_t _session_generation = 0;
    esp_mqtt_client_handle_t _mqtt    = nullptr;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _subscribed{false};
    std::atomic<bool> _connection_failed{false};
    std::atomic<bool> _credentials_rejected{false};
    bool _subscribe_sent                 = false;
    bool _upload_was_enabled             = false;
    bool _ready                          = false;
    bool _pair_pending                   = false;
    bool _pair_retry_pending             = false;
    unsigned _pair_attempts              = 0;
    std::uint32_t _pair_retry_at         = 0;
    std::uint32_t _pair_sent_at          = 0;
    std::uint32_t _connection_started_at = 0;
    std::uint32_t _retry_at              = 0;
    std::uint32_t _retry_delay           = 1000;
    std::uint32_t _refresh_at            = 0;
    std::string _token;
    std::string _client_id;
    std::string _publish_topic;
    std::string _subscribe_topic;
    std::vector<Field> _fields;
    std::vector<RgbOverride> _rgb_overrides;
    std::optional<PendingWrite> _pending_write;
    std::size_t _field_cursor = 0;
};

EzDataRuntime& runtime()
{
    static EzDataRuntime instance;
    return instance;
}

}  // namespace

namespace hal::internal {

bool ezdataStart()
{
    return runtime().start();
}

void ezdataStop()
{
    runtime().stop();
}

void ezdataUpdate(std::uint32_t)
{
    runtime().update();
}

void ezdataPublishChainSnapshot(const ChainSnapshot& snapshot)
{
    runtime().publish(snapshot);
}

EzDataStatus ezdataStatus()
{
    return runtime().status();
}

}  // namespace hal::internal
