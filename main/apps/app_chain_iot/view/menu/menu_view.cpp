/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "menu_view.h"

#include "node_draw.h"

#include <M5GFX.h>
#include <algorithm>
#include <apps/common/audio/audio.h>
#include <apps/common/gfx/alpha_renderer.h>
#include <assets/assets.h>
#include <cmath>
#include <hal/hal.h>

namespace {

constexpr int ScreenWidth  = 240;
constexpr int ScreenHeight = 240;

constexpr int InfoPanelY       = -45;
constexpr int NodeCenterY      = 194;
constexpr int NodeStep         = 80;
constexpr int HiddenMenuOffset = ScreenHeight;
constexpr int IndicatorCenterY = 155;
constexpr int InfoDetailY      = 112;

constexpr float MenuWaterLevel        = 130.0F;
constexpr float BackWaveOffset        = -10.0F;
constexpr float IndicatorBobAmplitude = 2.0F;
constexpr float IndicatorBobSpeed     = 1.8F;

constexpr float ContentVisualDuration = 0.60F;
constexpr float ContentBounce         = 0.20F;
constexpr float MenuVisualDuration    = 0.52F;
constexpr float MenuBounce            = 0.18F;
constexpr float InfoTransitionDelay   = 0.07F;
constexpr float DetailFadeDuration    = 0.28F;
constexpr float DetailRevealOffset    = 135.0F;
constexpr float IndicatorMaxOffsetX   = 5.0F;
constexpr float IndicatorSettlePoint  = 0.98F;
constexpr float Pi                    = 3.14159265F;

constexpr float FrontWaveAmplitude      = 10.0F;
constexpr float BackWaveAmplitude       = 13.0F;
constexpr float FrontWaveAmplitudeBoost = 0.30F;
constexpr float BackWaveAmplitudeBoost  = 0.23F;
constexpr float FrontWaveLength         = 58.0F;
constexpr float BackWaveLength          = 76.0F;
constexpr float FrontWaveSpeed          = 48.0F;
constexpr float BackWaveSpeed           = 82.0F;
constexpr float WaveSpeedBoost          = 5.75F;
constexpr float WaveBoostRiseDuration   = 0.10F;
constexpr float WaveBoostFallDuration   = 1.15F;
constexpr float WaveBoostHold           = 0.15F;
constexpr float SurfaceTiltPeak         = 3.5F;
constexpr std::uint32_t RenderInterval  = 16;

constexpr std::uint32_t BackWaveColor  = 0x767088;
constexpr std::uint32_t IndicatorColor = 0xF0F4F6;

const float FrontWaveStepSine   = std::sin(1.0F / FrontWaveLength);
const float FrontWaveStepCosine = std::cos(1.0F / FrontWaveLength);
const float BackWaveStepSine    = std::sin(1.0F / BackWaveLength);
const float BackWaveStepCosine  = std::cos(1.0F / BackWaveLength);

int roundToInt(float value)
{
    return static_cast<int>(std::lround(value));
}

void configureContentSpring(uitk::AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = ContentVisualDuration;
    options.bounce         = ContentBounce;
    options.restDelta      = 0.1F;
    options.restSpeed      = 0.1F;
    animation.delay        = 0.0F;
}

void configureMenuSpring(uitk::AnimateValue& animation, float delay = 0.0F)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = MenuVisualDuration;
    options.bounce         = MenuBounce;
    options.restDelta      = 0.1F;
    options.restSpeed      = 0.1F;
    animation.delay        = delay;
}

void configureDetailFade(uitk::AnimateValue& animation)
{
    auto& options          = animation.easingOptions();
    options.duration       = DetailFadeDuration;
    options.easingFunction = uitk::ease::ease_out_quad;
    animation.delay        = 0.0F;
}

void configureWaveSpeedEasing(uitk::AnimateValue& animation, float duration)
{
    auto& options          = animation.easingOptions();
    options.duration       = duration;
    options.easingFunction = uitk::ease::ease_out_quad;
    animation.delay        = 0.0F;
}

void configureSurfaceTiltSpring(uitk::AnimateValue& animation)
{
    auto& options          = animation.springOptions();
    options.duration       = 0.0F;
    options.visualDuration = 0.46F;
    options.bounce         = 0.32F;
    options.restDelta      = 0.02F;
    options.restSpeed      = 0.02F;
    animation.delay        = 0.0F;
}

void drawWave(lgfx::LGFX_Sprite& canvas, float waterLevel, float phase, float amplitude, float wavelength,
              float stepSine, float stepCosine, float verticalOffset, float surfaceTilt, std::uint32_t color)
{
    float wave_sine                 = std::sin(phase / wavelength);
    float wave_cosine               = std::cos(phase / wavelength);
    constexpr float ScreenHalfWidth = static_cast<float>(ScreenWidth - 1) * 0.5F;

    for (int x = 0; x < ScreenWidth; ++x) {
        const float horizontal_position = (static_cast<float>(x) - ScreenHalfWidth) / ScreenHalfWidth;
        const float top     = waterLevel + wave_sine * amplitude + verticalOffset + surfaceTilt * horizontal_position;
        const int solid_top = static_cast<int>(std::ceil(top));
        const int edge_y    = solid_top - 1;
        const float edge_coverage = static_cast<float>(solid_top) - top;
        if (edge_y >= 0 && edge_y < ScreenHeight && edge_coverage > 0.0F) {
            const auto edge_alpha =
                static_cast<std::uint8_t>(std::lround(std::clamp(edge_coverage, 0.0F, 1.0F) * 255.0F));
            canvas.fillRectAlpha(x, edge_y, 1, 1, edge_alpha, color);
        }

        const int clipped_solid_top = std::clamp(solid_top, 0, ScreenHeight);
        if (clipped_solid_top < ScreenHeight) {
            canvas.drawFastVLine(x, clipped_solid_top, ScreenHeight - clipped_solid_top, color);
        }

        const float next_sine = wave_sine * stepCosine + wave_cosine * stepSine;
        wave_cosine           = wave_cosine * stepCosine - wave_sine * stepSine;
        wave_sine             = next_sine;
    }
}

void drawRoundedIndicator(lgfx::LGFX_Sprite& canvas, int offsetX, float offsetY)
{
    const auto& icon = image_chain_iot_change_history_fill;
    if (icon.header.cf != LV_COLOR_FORMAT_A8 || icon.data == nullptr) return;
    gfx::drawRotatedA8Mask(canvas, icon.data, static_cast<int>(icon.header.w), static_cast<int>(icon.header.h),
                           static_cast<int>(icon.header.stride),
                           {0, 0, static_cast<int>(icon.header.w), static_cast<int>(icon.header.h)},
                           static_cast<float>(ScreenWidth / 2 + offsetX),
                           static_cast<float>(IndicatorCenterY) + offsetY, 180.0F, IndicatorColor);
}

}  // namespace

namespace chain_iot::view {

void ChainMenuView::drawLoading()
{
    static const lgfx::LVGLFont LoadingFont{&font_noto_sans_semibold_14};
    auto& canvas = GetHAL().gfxCanvas();
    canvas.clear(draw::InfoPanelColor);
    canvas.setFont(&LoadingFont);
    canvas.setTextSize(1);
    canvas.setTextDatum(lgfx::textdatum_t::middle_center);
    canvas.setTextColor(draw::MenuSurfaceColor);
    canvas.setBaseColor(draw::InfoPanelColor);
    canvas.drawString("Loading...", ScreenWidth / 2, ScreenHeight / 2);
    GetHAL().presentGfx();
}

void ChainMenuView::init(const ChainSnapshot& snapshot)
{
    _nodes.clear();
    _topology_revision          = snapshot.topologyRevision;
    _captain_index              = 0;
    _selected_index             = 0;
    _held_edge_direction        = 0;
    _indicator_motion_direction = 0;
    _last_update_time           = snapshot.capturedAt;
    _last_render_time           = 0;
    _last_change_at             = 0;
    _front_wave_phase           = 0.0F;
    _back_wave_phase            = 0.0F;
    _indicator_phase            = 0.0F;
    _wave_boost_direction       = 1.0F;
    _has_update_time            = true;
    _wave_boost_requested       = false;
    _wave_boost_active          = false;
    _wave_boost_settling        = false;
    _indicator_motion_active    = false;
    _detail_fade_started        = false;
    _ok_armed                   = false;
    _render_state               = {};
    _render_state_valid         = false;
    _render_requested           = true;
    _detail_view.reset();

    configureWaveSpeedEasing(_wave_energy, WaveBoostRiseDuration);
    _wave_energy.teleport(0.0F);
    configureSurfaceTiltSpring(_surface_tilt);
    _surface_tilt.teleport(0.0F);
    configureDetailFade(_detail_mask_opacity);
    _detail_mask_opacity.teleport(255.0F);
    rebuildTopology(snapshot);
}

void ChainMenuView::rebuildTopology(const ChainSnapshot& snapshot)
{
    constexpr std::uint32_t LeftEmptyId  = 0xFFFFFFFEU;
    constexpr std::uint32_t RightEmptyId = 0xFFFFFFFFU;

    const bool initial                  = _nodes.empty();
    const bool detail_was_active        = _detail_view.active();
    const std::uint32_t selected_id     = selectedNode() == nullptr ? 0 : selectedNode()->id();
    const int previous_selected_index   = _selected_index;
    const int previous_captain_index    = _captain_index;
    const float previous_content_offset = _content_offset.directValue();
    std::uint32_t newly_added_id        = 0;
    std::vector<std::uint32_t> previous_node_ids;
    for (const auto& node : _nodes) {
        previous_node_ids.push_back(node->id());
    }
    const auto detect_new_device = [&](const ChainDeviceSnapshot& device) {
        if (std::find(previous_node_ids.begin(), previous_node_ids.end(), device.id) == previous_node_ids.end()) {
            newly_added_id = device.id;
        }
    };
    for (const auto& device : snapshot.left) detect_new_device(device);
    for (const auto& device : snapshot.right) detect_new_device(device);

    auto previous_nodes  = std::move(_nodes);
    const auto take_node = [&](std::uint32_t id, NodeKind kind) -> std::unique_ptr<ChainNode> {
        const auto found = std::find_if(previous_nodes.begin(), previous_nodes.end(), [id, kind](const auto& node) {
            return node != nullptr && node->id() == id && node->kind() == kind;
        });
        if (found == previous_nodes.end()) return nullptr;
        auto node = std::move(*found);
        return node;
    };
    const auto append_device = [&](const ChainDeviceSnapshot& device) {
        NodeKind kind;
        switch (device.kind) {
            case ChainDeviceKind::Key:
                kind = NodeKind::Key;
                break;
            case ChainDeviceKind::Angle:
                kind = NodeKind::Angle;
                break;
            case ChainDeviceKind::Joystick:
                kind = NodeKind::Joystick;
                break;
            case ChainDeviceKind::Buzzer:
                kind = NodeKind::Buzzer;
                break;
            case ChainDeviceKind::Encoder:
                kind = NodeKind::Encoder;
                break;
            case ChainDeviceKind::Mono:
                kind = NodeKind::Mono;
                break;
            case ChainDeviceKind::Rgb:
                kind = NodeKind::Rgb;
                break;
            case ChainDeviceKind::Pir:
                kind = NodeKind::Pir;
                break;
            case ChainDeviceKind::Tof:
                kind = NodeKind::Tof;
                break;
            default:
                return;
        }
        auto node = take_node(device.id, kind);
        if (node == nullptr) node = makeChainNode(device);
        if (node != nullptr) _nodes.push_back(std::move(node));
    };

    auto left_empty = take_node(LeftEmptyId, NodeKind::Empty);
    _nodes.push_back(left_empty == nullptr ? makeEmptyNode(true) : std::move(left_empty));
    for (auto iterator = snapshot.left.rbegin(); iterator != snapshot.left.rend(); ++iterator) {
        append_device(*iterator);
    }
    _captain_index = static_cast<int>(_nodes.size());
    auto captain   = take_node(0, NodeKind::Captain);
    _nodes.push_back(captain == nullptr ? makeCaptainNode() : std::move(captain));
    for (const auto& device : snapshot.right) append_device(device);
    auto right_empty = take_node(RightEmptyId, NodeKind::Empty);
    _nodes.push_back(right_empty == nullptr ? makeEmptyNode(false) : std::move(right_empty));

    auto& canvas = GetHAL().gfxCanvas();
    for (auto& node : _nodes) {
        node->apply(snapshot);
        node->prepareIcon(canvas);
    }
    _topology_revision = snapshot.topologyRevision;

    const auto find_node_index = [&](std::uint32_t id) {
        const auto found = std::find_if(_nodes.begin(), _nodes.end(),
                                        [id](const auto& node) { return node != nullptr && node->id() == id; });
        return found == _nodes.end() ? -1 : static_cast<int>(std::distance(_nodes.begin(), found));
    };

    int preserved_index = initial ? _captain_index : find_node_index(selected_id);
    if (preserved_index < 0) {
        _ok_armed                = false;
        _held_edge_direction     = 0;
        _indicator_motion_active = false;
        const int direction      = previous_selected_index < previous_captain_index ? 1 : -1;
        for (int index = previous_selected_index; index != previous_captain_index; index += direction) {
            preserved_index = find_node_index(previous_node_ids[index]);
            if (preserved_index >= 0) break;
        }
        if (preserved_index < 0) preserved_index = _captain_index;
    }
    _selected_index = preserved_index;

    if (initial) {
        configureContentSpring(_content_offset);
        _content_offset.teleport(0.0F);
        configureMenuSpring(_info_offset_y, InfoTransitionDelay);
        _info_offset_y.teleport(static_cast<float>(HiddenMenuOffset));
        _info_offset_y = 0.0F;
        configureMenuSpring(_menu_offset_y);
        _menu_offset_y.teleport(static_cast<float>(HiddenMenuOffset));
        _menu_offset_y = 0.0F;
        render(snapshot.capturedAt);
        return;
    }

    if (detail_was_active && find_node_index(selected_id) < 0) {
        _detail_view.reset();
        _info_offset_y.teleport(0.0F);
        _menu_offset_y.teleport(0.0F);
        _detail_mask_opacity.teleport(255.0F);
        _detail_fade_started = false;
    }

    const auto anchor = std::find(previous_node_ids.begin(), previous_node_ids.end(), selectedNode()->id());
    const int previous_anchor_index = static_cast<int>(std::distance(previous_node_ids.begin(), anchor));
    // Keep the surviving node at its current screen position before settling the selection.
    _content_offset.teleport(
        previous_content_offset +
        static_cast<float>((previous_anchor_index - previous_captain_index - _selected_index + _captain_index) *
                           NodeStep));
    _content_offset       = static_cast<float>((_captain_index - _selected_index) * NodeStep);
    const int added_index = !detail_was_active && newly_added_id != 0 ? find_node_index(newly_added_id) : -1;
    if (added_index >= 0) {
        select(added_index);
    } else {
        _render_requested = true;
    }
}

void ChainMenuView::applySnapshot(const ChainSnapshot& snapshot)
{
    if (snapshot.topologyRevision != _topology_revision) {
        rebuildTopology(snapshot);
        return;
    }
    const auto* selected = selectedNode();
    const bool full_screen_detail =
        _detail_view.visible() && selected != nullptr && selected->detailLayout() == DetailLayout::FullScreen;
    for (auto& node : _nodes) {
        const bool changed = node->apply(snapshot);
        if (changed && (!full_screen_detail || node.get() == selected)) _render_requested = true;
    }
}

void ChainMenuView::select(int index)
{
    if (index < 0 || index >= static_cast<int>(_nodes.size()) || index == _selected_index) return;
    const int direction         = index > _selected_index ? 1 : -1;
    _selected_index             = index;
    _content_offset             = static_cast<float>((_captain_index - _selected_index) * NodeStep);
    _indicator_motion_direction = direction;
    _indicator_motion_active    = true;
    _wave_boost_direction       = static_cast<float>(direction);
    _wave_boost_requested       = true;
    _render_requested           = true;
}

void ChainMenuView::updateWaveMotion(std::uint32_t currentTime)
{
    if (_wave_boost_requested) {
        configureWaveSpeedEasing(_wave_energy, WaveBoostRiseDuration);
        configureSurfaceTiltSpring(_surface_tilt);
        _wave_energy          = 1.0F;
        _surface_tilt         = _wave_boost_direction * SurfaceTiltPeak;
        _last_change_at       = currentTime;
        _wave_boost_requested = false;
        _wave_boost_active    = true;
        _wave_boost_settling  = false;
        return;
    }

    if (_wave_boost_active && !_wave_boost_settling &&
        currentTime - _last_change_at >= static_cast<std::uint32_t>(WaveBoostHold * 1000.0F)) {
        configureWaveSpeedEasing(_wave_energy, WaveBoostFallDuration);
        configureSurfaceTiltSpring(_surface_tilt);
        _wave_energy         = 0.0F;
        _surface_tilt        = 0.0F;
        _wave_boost_settling = true;
    }

    if (_wave_boost_settling && _wave_energy.done()) {
        _wave_boost_active   = false;
        _wave_boost_settling = false;
    }
}

int ChainMenuView::indicatorOffsetX()
{
    if (!_indicator_motion_active || _indicator_motion_direction == 0) return 0;

    const float remaining                 = _content_offset.end - _content_offset.directValue();
    const float remaining_along_direction = -remaining * static_cast<float>(_indicator_motion_direction);
    const float progress = std::clamp(1.0F - remaining_along_direction / static_cast<float>(NodeStep), 0.0F, 1.0F);
    const float motion   = std::sin(progress * Pi) * IndicatorMaxOffsetX;
    return roundToInt(motion * static_cast<float>(_indicator_motion_direction));
}

bool ChainMenuView::handleInput(input::KeyEvent event)
{
    if (_held_edge_direction != 0 && navigationButtonsReleased()) _held_edge_direction = 0;
    if (_detail_view.active()) {
        if (_detail_view.transitioning()) return true;
        if (auto* node = selectedNode(); node != nullptr && node->handleDetailInput(event)) {
            return true;
        }
        if (event == input::KeyEvent::Home) {
            audio::play_ui_sfx(audio::UiSfx::Back);
            _detail_view.close();
            if (selectedNode() != nullptr && selectedNode()->detailLayout() == DetailLayout::InfoPanel) {
                _detail_mask_opacity = 255.0F;
            }
            _detail_fade_started = false;
            configureMenuSpring(_info_offset_y, InfoTransitionDelay);
            configureMenuSpring(_menu_offset_y);
            _info_offset_y    = 0.0F;
            _menu_offset_y    = 0.0F;
            _render_requested = true;
        }
        return true;
    }

    switch (event) {
        case input::KeyEvent::Home:
            return false;
        case input::KeyEvent::Previous:
        case input::KeyEvent::PreviousRepeat:
            _ok_armed = false;
            if (atNavigationEdge(-1)) {
                if (_held_edge_direction == 0) {
                    _held_edge_direction = -1;
                    audio::play_ui_sfx(audio::UiSfx::Boundary);
                }
            } else {
                audio::play_ui_sfx(audio::UiSfx::Navigate);
                select(_selected_index - 1);
            }
            return true;
        case input::KeyEvent::Next:
        case input::KeyEvent::NextRepeat:
            _ok_armed = false;
            if (atNavigationEdge(1)) {
                if (_held_edge_direction == 0) {
                    _held_edge_direction = 1;
                    audio::play_ui_sfx(audio::UiSfx::Boundary);
                }
            } else {
                audio::play_ui_sfx(audio::UiSfx::Navigate);
                select(_selected_index + 1);
            }
            return true;
        case input::KeyEvent::OkPressed:
            if (const auto* node = selectedNode(); node != nullptr && node->detailAvailable()) {
                _ok_armed = true;
                audio::play_ui_sfx(audio::UiSfx::Confirm);
            } else {
                _ok_armed = false;
                audio::play_ui_sfx(audio::UiSfx::Boundary);
            }
            return true;
        case input::KeyEvent::OkReleased:
            if (_ok_armed) {
                const bool uses_full_screen =
                    selectedNode() != nullptr && selectedNode()->detailLayout() == DetailLayout::FullScreen;
                _detail_view.open();
                _detail_mask_opacity.teleport(255.0F);
                _detail_fade_started = false;
                configureMenuSpring(_info_offset_y, InfoTransitionDelay);
                configureMenuSpring(_menu_offset_y);
                _info_offset_y    = uses_full_screen ? static_cast<float>(HiddenMenuOffset) : 0.0F;
                _menu_offset_y    = static_cast<float>(HiddenMenuOffset);
                _render_requested = true;
            }
            _ok_armed = false;
            return true;
        default:
            return true;
    }
}

void ChainMenuView::update(std::uint32_t currentTime)
{
    const float time_seconds = static_cast<float>(currentTime) / 1000.0F;
    _content_offset.update(time_seconds);
    _info_offset_y.update(time_seconds);
    _menu_offset_y.update(time_seconds);
    _detail_mask_opacity.update(time_seconds);
    if (_indicator_motion_active) {
        const float remaining                 = _content_offset.end - _content_offset.directValue();
        const float remaining_along_direction = -remaining * static_cast<float>(_indicator_motion_direction);
        const float progress                  = 1.0F - remaining_along_direction / static_cast<float>(NodeStep);
        if (progress >= IndicatorSettlePoint || _content_offset.done()) _indicator_motion_active = false;
    }
    updateWaveMotion(currentTime);
    _wave_energy.update(time_seconds);
    _surface_tilt.update(time_seconds);

    if (_detail_view.phase() == DetailView::Phase::Opening && !_detail_fade_started &&
        _menu_offset_y.directValue() >= DetailRevealOffset && selectedNode() != nullptr) {
        _detail_mask_opacity = 0.0F;
        _detail_fade_started = true;
        _render_requested    = true;
    }

    float elapsed_seconds = 0.0F;
    if (_has_update_time) {
        elapsed_seconds = std::min(static_cast<float>(currentTime - _last_update_time) / 1000.0F, 0.05F);
    }
    _last_update_time = currentTime;
    _has_update_time  = true;

    const float wave_energy      = std::clamp(_wave_energy.directValue(), 0.0F, 1.0F);
    const float speed_multiplier = 1.0F + WaveSpeedBoost * wave_energy;
    _front_wave_phase += FrontWaveSpeed * speed_multiplier * elapsed_seconds;
    _back_wave_phase -= BackWaveSpeed * speed_multiplier * elapsed_seconds;
    _indicator_phase += IndicatorBobSpeed * elapsed_seconds;

    if (_detail_view.transitioning() && _info_offset_y.done() && _menu_offset_y.done()) {
        const bool opening = _detail_view.phase() == DetailView::Phase::Opening;
        if (_detail_view.completeTransition()) {
            if (opening && !_detail_fade_started && selectedNode() != nullptr) {
                _detail_mask_opacity = 0.0F;
                _detail_fade_started = true;
            } else if (!opening) {
                _detail_mask_opacity.teleport(255.0F);
                _detail_fade_started = false;
            }
            _render_requested = true;
        }
    }
    if (_detail_view.active()) {
        if (auto* node = selectedNode(); node != nullptr && node->updateDetail(currentTime)) {
            _render_requested = true;
        }
    }
    render(currentTime);
}

bool ChainMenuView::atNavigationEdge(int direction) const
{
    return direction < 0 ? _selected_index == 0 : _selected_index == static_cast<int>(_nodes.size()) - 1;
}

bool ChainMenuView::navigationButtonsReleased() const
{
    return !GetHAL().btnB.isPressed() && !GetHAL().btnC.isPressed();
}

ChainNode* ChainMenuView::selectedNode()
{
    return _selected_index >= 0 && _selected_index < static_cast<int>(_nodes.size())
               ? _nodes[static_cast<std::size_t>(_selected_index)].get()
               : nullptr;
}

const ChainNode* ChainMenuView::renderedNode(int index) const
{
    return index >= 0 && index < static_cast<int>(_nodes.size()) ? _nodes[static_cast<std::size_t>(index)].get()
                                                                 : nullptr;
}

void ChainMenuView::render(std::uint32_t currentTime)
{
    const RenderState state{
        roundToInt(_content_offset.directValue()),
        indicatorOffsetX(),
        roundToInt(_info_offset_y.directValue()),
        roundToInt(_menu_offset_y.directValue()),
        roundToInt(_detail_mask_opacity.directValue()),
        _selected_index,
        _detail_view.phase(),
    };
    const bool interval_elapsed = currentTime - _last_render_time >= RenderInterval;
    const auto* node            = selectedNode();
    const bool full_screen_detail =
        _detail_view.visible() && node != nullptr && node->detailLayout() == DetailLayout::FullScreen;
    // Full-screen pages hide the animated waves. Submit only their actual changes,
    // so static Wi-Fi setup does not continuously occupy the display transport.
    if (!_render_requested && _render_state_valid && state == _render_state &&
        (full_screen_detail || !interval_elapsed))
        return;
    _render_state       = state;
    _render_state_valid = true;
    _render_requested   = false;
    _last_render_time   = currentTime;
    draw();
    GetHAL().presentGfx();
}

void ChainMenuView::drawInfoLayer(lgfx::LGFX_Sprite& canvas, int offsetY) const
{
    if (const auto* node = renderedNode(_render_state.selectedIndex); node != nullptr) {
        node->drawInfo(canvas, InfoPanelY + offsetY);
    }
}

void ChainMenuView::drawInfoDetail(lgfx::LGFX_Sprite& canvas, const ChainNode& node) const
{
    const Frame frame{0, InfoDetailY, ScreenWidth, ScreenHeight - InfoDetailY};
    node.drawDetail(canvas, frame);

    const int mask_opacity = std::clamp(_render_state.detailMaskOpacity, 0, 255);
    if (mask_opacity == 255) {
        canvas.fillRect(frame.x, frame.y, frame.width, frame.height, draw::InfoPanelColor);
    } else if (mask_opacity != 0) {
        canvas.fillRectAlpha(frame.x, frame.y, frame.width, frame.height, static_cast<std::uint8_t>(mask_opacity),
                             draw::InfoPanelColor);
    }
}

void ChainMenuView::drawFullScreenDetail(lgfx::LGFX_Sprite& canvas, const ChainNode& node) const
{
    node.drawDetail(canvas, {0, 0, ScreenWidth, ScreenHeight});
    const int mask_opacity = std::clamp(_render_state.detailMaskOpacity, 0, 255);
    if (mask_opacity != 0) {
        canvas.fillRectAlpha(0, 0, ScreenWidth, ScreenHeight, static_cast<std::uint8_t>(mask_opacity),
                             draw::InfoPanelColor);
    }
}

void ChainMenuView::drawMenuLayer(lgfx::LGFX_Sprite& canvas, int offsetY)
{
    const float wave_energy     = std::clamp(_wave_energy.directValue(), 0.0F, 1.0F);
    const float back_amplitude  = BackWaveAmplitude * (1.0F + BackWaveAmplitudeBoost * wave_energy);
    const float front_amplitude = FrontWaveAmplitude * (1.0F + FrontWaveAmplitudeBoost * wave_energy);
    const float surface_tilt    = _surface_tilt.directValue();
    const float water_level     = MenuWaterLevel + static_cast<float>(offsetY);

    drawWave(canvas, water_level, _back_wave_phase, back_amplitude, BackWaveLength, BackWaveStepSine,
             BackWaveStepCosine, BackWaveOffset, surface_tilt, BackWaveColor);
    drawWave(canvas, water_level, _front_wave_phase, front_amplitude, FrontWaveLength, FrontWaveStepSine,
             FrontWaveStepCosine, 0.0F, surface_tilt, draw::MenuSurfaceColor);

    const int node_center_y = NodeCenterY + offsetY;
    canvas.fillRect(0, node_center_y - 1, ScreenWidth, 2, draw::ConnectionColor);

    for (std::size_t index = 0; index < _nodes.size(); ++index) {
        const int center_x =
            ScreenWidth / 2 + (static_cast<int>(index) - _captain_index) * NodeStep + _render_state.contentOffset;
        const int center_y = node_center_y;
        if (center_x < -30 || center_x > ScreenWidth + 30 || center_y < -40 || center_y > ScreenHeight + 40) {
            continue;
        }
        _nodes[index]->drawIcon(canvas, center_x, center_y);
    }
    const float indicator_offset = static_cast<float>(offsetY) + std::sin(_indicator_phase) * IndicatorBobAmplitude;
    drawRoundedIndicator(canvas, _render_state.indicatorOffsetX, indicator_offset);
}

void ChainMenuView::draw()
{
    auto& canvas = GetHAL().gfxCanvas();
    canvas.clear(draw::InfoPanelColor);

    const auto* node           = renderedNode(_render_state.selectedIndex);
    const bool uses_info_panel = node != nullptr && node->detailLayout() == DetailLayout::InfoPanel;

    if (_render_state.detailPhase == DetailView::Phase::Hidden) {
        drawInfoLayer(canvas, -_render_state.infoOffsetY);
        drawMenuLayer(canvas, _render_state.menuOffsetY);
        return;
    }

    if (_render_state.detailPhase == DetailView::Phase::Opening) {
        drawInfoLayer(canvas, -_render_state.infoOffsetY);
        if (uses_info_panel) {
            drawInfoDetail(canvas, *node);
        } else if (node != nullptr && _detail_fade_started) {
            drawFullScreenDetail(canvas, *node);
        }
        drawMenuLayer(canvas, _render_state.menuOffsetY);
        return;
    }

    if (_render_state.detailPhase == DetailView::Phase::Closing) {
        drawInfoLayer(canvas, -_render_state.infoOffsetY);
        if (uses_info_panel) drawInfoDetail(canvas, *node);
        drawMenuLayer(canvas, _render_state.menuOffsetY);
        return;
    }

    if (node != nullptr) {
        if (uses_info_panel) {
            drawInfoLayer(canvas, 0);
            drawInfoDetail(canvas, *node);
        } else {
            drawFullScreenDetail(canvas, *node);
        }
    }
}

}  // namespace chain_iot::view
