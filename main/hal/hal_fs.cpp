/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include "hal_internal.h"

#include <esp_vfs_fat.h>
#include <esp_log.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <sys/stat.h>

namespace {

constexpr char Tag[]              = "HAL-FS";
constexpr char StorageRoot[]      = "/storage";
constexpr char StoragePartition[] = "storage";

wl_handle_t wl_handle = WL_INVALID_HANDLE;
bool mounted          = false;

bool makeStoragePath(std::string_view relativePath, std::string& path)
{
    while (!relativePath.empty() && relativePath.front() == '/') {
        relativePath.remove_prefix(1);
    }

    path = StorageRoot;
    if (relativePath.empty()) {
        return true;
    }

    std::size_t segmentStart = 0;
    while (segmentStart < relativePath.size()) {
        const std::size_t slash        = relativePath.find('/', segmentStart);
        const std::size_t end          = slash == std::string_view::npos ? relativePath.size() : slash;
        const std::string_view segment = relativePath.substr(segmentStart, end - segmentStart);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }

        path.push_back('/');
        path.append(segment);
        if (slash == std::string_view::npos) {
            break;
        }
        segmentStart = slash + 1;
    }
    return true;
}

bool ensureDirectory(const std::string& path)
{
    if (path == StorageRoot) {
        return true;
    }

    std::string current(StorageRoot);
    std::size_t segmentStart = sizeof(StorageRoot) - 1;
    while (segmentStart < path.size()) {
        if (path[segmentStart] == '/') {
            ++segmentStart;
        }
        const std::size_t slash = path.find('/', segmentStart);
        const std::size_t end   = slash == std::string::npos ? path.size() : slash;
        current.push_back('/');
        current.append(path, segmentStart, end - segmentStart);

        if (mkdir(current.c_str(), 0775) != 0 && errno != EEXIST) {
            ESP_LOGW(Tag, "failed to create directory %s: errno=%d", current.c_str(), errno);
            return false;
        }

        struct stat info = {};
        if (stat(current.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
            ESP_LOGW(Tag, "path is not a directory: %s", current.c_str());
            return false;
        }

        if (slash == std::string::npos) {
            break;
        }
        segmentStart = slash + 1;
    }
    return true;
}

std::size_t fileSystemBytes(bool freeSpace)
{
    std::uint64_t totalBytes = 0;
    std::uint64_t freeBytes  = 0;
    const esp_err_t result   = esp_vfs_fat_info(StorageRoot, &totalBytes, &freeBytes);
    if (result != ESP_OK) {
        ESP_LOGW(Tag, "failed to query filesystem space: %s", esp_err_to_name(result));
        return 0;
    }

    const std::uint64_t bytes = freeSpace ? freeBytes : totalBytes;
    if (bytes > std::numeric_limits<std::size_t>::max()) {
        return std::numeric_limits<std::size_t>::max();
    }
    return static_cast<std::size_t>(bytes);
}

}  // namespace

namespace hal::internal {

esp_err_t fsInit()
{
    if (mounted) {
        return ESP_OK;
    }

    const esp_vfs_fat_mount_config_t mountConfig = {
        .format_if_mount_failed   = true,
        .max_files                = 8,
        .allocation_unit_size     = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat              = false,
    };

    const esp_err_t result = esp_vfs_fat_spiflash_mount_rw_wl(StorageRoot, StoragePartition, &mountConfig, &wl_handle);
    if (result != ESP_OK) {
        ESP_LOGE(Tag, "failed to mount FAT filesystem: %s", esp_err_to_name(result));
        wl_handle = WL_INVALID_HANDLE;
        return result;
    }

    mounted = true;
    ESP_LOGI(Tag, "mounted FAT filesystem at %s (%u bytes free)", StorageRoot,
             static_cast<unsigned>(fileSystemBytes(true)));
    return ESP_OK;
}

}  // namespace hal::internal

bool Hal::initFileSystem()
{
    const esp_err_t result = hal::internal::fsInit();
    _file_system_ready     = result == ESP_OK;
    return _file_system_ready;
}

bool Hal::isFileSystemReady() const
{
    return _file_system_ready;
}

std::string Hal::fileSystemPath(std::string_view relativePath) const
{
    if (!_file_system_ready) {
        return {};
    }

    std::string path;
    if (!makeStoragePath(relativePath, path)) {
        return {};
    }
    return path;
}

std::size_t Hal::fileSystemCapacity() const
{
    return _file_system_ready ? fileSystemBytes(false) : 0;
}

std::size_t Hal::fileSystemFreeSpace() const
{
    return _file_system_ready ? fileSystemBytes(true) : 0;
}

bool Hal::ensureFileSystemDirectory(std::string_view relativePath) const
{
    if (!_file_system_ready) {
        return false;
    }

    std::string path;
    return makeStoragePath(relativePath, path) && ensureDirectory(path);
}
