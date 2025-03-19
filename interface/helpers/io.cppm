module;

#include <cerrno>

export module vk_gltf_viewer:helpers.io;

import std;

export
[[nodiscard]] std::vector<std::byte> loadFileAsBinary(const std::filesystem::path &path, std::size_t offset = 0) {
    std::ifstream file { path, std::ios::binary };
    if (!file) {
#if __clang__
        // Using std::format in Clang causes template instantiation of formatter<wchar_t, const char*>.
        // TODO: report to Clang developers.
        using namespace std::string_literals;
        throw std::runtime_error { "Failed to open file: "s + std::strerror(errno) + " (error code=" + std::to_string(errno) + ")" };
#else
        throw std::runtime_error { std::format("Failed to open file: {} (error code={})", std::strerror(errno), errno) };
#endif
    }

    file.seekg(0, std::ios::end);
    const std::size_t fileSize = file.tellg();
    file.seekg(offset);

    std::vector<std::byte> result(fileSize - offset);
    file.read(reinterpret_cast<char*>(result.data()), result.size());

    return result;
}