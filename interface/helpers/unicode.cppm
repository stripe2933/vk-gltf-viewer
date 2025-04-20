module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer:helpers.unicode;

import std;

#ifdef _WIN32
export class StringHolder {
	std::u8string str;

	StringHolder(std::u8string str) noexcept : str{ std::move(str) } {}

	friend StringHolder c_str(const std::filesystem::path&) noexcept;

public:
	[[nodiscard]] operator const char* () const noexcept LIFETIMEBOUND {
		return reinterpret_cast<const char*>(str.c_str());
	}
};

export
[[nodiscard]] StringHolder c_str(const std::filesystem::path& path LIFETIMEBOUND) noexcept {
	return path.u8string();
}
#else
export
[[nodiscard]] const char* c_str(const std::filesystem::path& path LIFETIMEBOUND) noexcept {
	return path.c_str();
}
#endif