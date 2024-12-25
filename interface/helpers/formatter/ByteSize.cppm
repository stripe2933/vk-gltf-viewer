export module vk_gltf_viewer:helpers.formatter.ByteSize;

import std;

export struct ByteSize {
    std::size_t size;
};

export template <>
struct std::formatter<ByteSize> : formatter<float> {
    auto format(ByteSize v, auto &ctx) const {
        if (v.size < 1024) return format_to(ctx.out(), "{} B", v.size);
        if (v.size < 1024 * 1024) return format_to(ctx.out(), "{:.2f} KB", v.size / 1024.f);
        if (v.size < 1024 * 1024 * 1024) return format_to(ctx.out(), "{:.2f} MB", v.size / (1024.f * 1024.f));
        return format_to(ctx.out(), "{:.2f} GB", v.size / (1024.f * 1024.f * 1024.f));
    }
};