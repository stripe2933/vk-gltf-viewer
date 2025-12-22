module;

#include <cerrno>

export module vk_gltf_viewer.helpers.io;

import std;
import fmt;

export
[[nodiscard]] std::vector<std::byte> loadFileAsBinary(const std::filesystem::path &path, std::size_t offset = 0);

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::vector<std::byte> loadFileAsBinary(const std::filesystem::path &path, std::size_t offset) {
    std::ifstream file { path, std::ios::binary };
    if (!file) {
        throw std::runtime_error { fmt::format("Failed to open file: {} (error code={})", std::strerror(errno), errno) };
    }

    file.seekg(0, std::ios::end);
    const std::size_t fileSize = file.tellg();
    file.seekg(offset);

    std::vector<std::byte> result(fileSize - offset);
    file.read(reinterpret_cast<char*>(result.data()), result.size());

    return result;
}