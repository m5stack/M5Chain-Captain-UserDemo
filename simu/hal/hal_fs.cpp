/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <cstdlib>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <mooncake_log.h>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

constexpr std::string_view Tag       = "HAL-FS";
constexpr char StorageDirectoryEnv[] = "CHAIN_CAPTAIN_SIMU_STORAGE_DIR";

bool isPathWithin(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    auto root_it        = root.begin();
    const auto root_end = root.end();
    auto candidate_it   = candidate.begin();

    for (; root_it != root_end; ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) {
            return false;
        }
    }
    return true;
}

std::filesystem::path makeAbsolute(std::filesystem::path path, std::error_code& error)
{
    if (path.is_relative()) {
        path = std::filesystem::absolute(path, error);
    }
    if (error) {
        return {};
    }
    return path.lexically_normal();
}

std::filesystem::path configuredRoot(std::error_code& error)
{
    const char* configured_path = std::getenv(StorageDirectoryEnv);
    std::filesystem::path root;
    if (configured_path != nullptr && configured_path[0] != '\0') {
        root = configured_path;
    } else {
        root = std::filesystem::current_path(error) / ".chain-captain-storage";
    }
    if (error) {
        return {};
    }
    return makeAbsolute(std::move(root), error);
}

std::filesystem::path resolvePath(const std::string& root_string, std::string_view relative_path, bool& valid)
{
    valid = false;
    if (root_string.empty()) {
        return {};
    }

    const std::filesystem::path root(root_string);
    while (!relative_path.empty() && relative_path.front() == '/') {
        relative_path.remove_prefix(1);
    }

    std::size_t segment_start = 0;
    while (segment_start < relative_path.size()) {
        const std::size_t slash        = relative_path.find('/', segment_start);
        const std::size_t end          = slash == std::string_view::npos ? relative_path.size() : slash;
        const std::string_view segment = relative_path.substr(segment_start, end - segment_start);
        if (segment.empty() || segment == "." || segment == "..") {
            return {};
        }
        if (slash == std::string_view::npos) {
            break;
        }
        segment_start = slash + 1;
    }

    const std::filesystem::path relative{std::string(relative_path)};
    const std::filesystem::path candidate = (root / relative).lexically_normal();
    if (!isPathWithin(root, candidate)) {
        return {};
    }

    // weakly_canonical also resolves symlinks in existing path components, so
    // a user-provided path cannot escape the simulator storage root.
    std::error_code root_error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, root_error);
    if (root_error) {
        return {};
    }

    std::error_code candidate_error;
    const auto canonical_candidate = std::filesystem::weakly_canonical(candidate, candidate_error);
    if (candidate_error || !isPathWithin(canonical_root, canonical_candidate)) {
        return {};
    }

    valid = true;
    return candidate;
}

std::size_t toSize(std::uintmax_t value)
{
    constexpr auto Maximum = std::numeric_limits<std::size_t>::max();
    if (value > Maximum) {
        return Maximum;
    }
    return static_cast<std::size_t>(value);
}

}  // namespace

bool Hal::initFileSystem()
{
    if (_file_system_ready) {
        return true;
    }

    std::error_code error;
    const auto root = configuredRoot(error);
    if (error || root.empty()) {
        mclog::tagError(Tag, "failed to resolve simulator storage directory: {}", error.message());
        return false;
    }

    if (!std::filesystem::exists(root, error)) {
        std::filesystem::create_directories(root, error);
        if (error) {
            mclog::tagError(Tag, "failed to create storage directory {}: {}", root.string(), error.message());
            return false;
        }
    }
    if (!std::filesystem::is_directory(root, error) || error) {
        mclog::tagError(Tag, "storage path is not a directory: {}", root.string());
        return false;
    }

    const auto space = std::filesystem::space(root, error);
    if (error) {
        mclog::tagError(Tag, "failed to query storage directory {}: {}", root.string(), error.message());
        return false;
    }

    _file_system_root  = root.string();
    _file_system_ready = true;
    mclog::tagInfo(Tag, "simulator storage ready: path={}, capacity={} bytes, free={} bytes", _file_system_root,
                   toSize(space.capacity), toSize(space.available));
    return true;
}

bool Hal::isFileSystemReady() const
{
    return _file_system_ready;
}

std::string Hal::fileSystemPath(std::string_view relativePath) const
{
    bool valid      = false;
    const auto path = resolvePath(_file_system_root, relativePath, valid);
    return valid ? path.string() : std::string{};
}

std::size_t Hal::fileSystemCapacity() const
{
    if (!_file_system_ready) {
        return 0;
    }

    std::error_code error;
    const auto space = std::filesystem::space(_file_system_root, error);
    return error ? 0 : toSize(space.capacity);
}

std::size_t Hal::fileSystemFreeSpace() const
{
    if (!_file_system_ready) {
        return 0;
    }

    std::error_code error;
    const auto space = std::filesystem::space(_file_system_root, error);
    return error ? 0 : toSize(space.available);
}

bool Hal::ensureFileSystemDirectory(std::string_view relativePath) const
{
    const std::string directory = fileSystemPath(relativePath);
    if (directory.empty()) {
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    return !error && std::filesystem::is_directory(directory, error) && !error;
}
