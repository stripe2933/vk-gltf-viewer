export module vk_gltf_viewer:helpers.enums.Flags;

import std;
export import :helpers.enums.FlagTraits;

export template <typename BitType>
class Flags;

export template <typename BitType>
[[nodiscard]] constexpr Flags<BitType> operator&(BitType bit, const Flags<BitType> &flags) noexcept {
    return flags.operator&(bit);
}

export template <typename BitType>
[[nodiscard]] constexpr Flags<BitType> operator^(BitType bit, const Flags<BitType> &flags) noexcept {
    return flags.operator^(bit);
}

export template <typename BitType> requires (FlagTraits<BitType>::isBitmask)
[[nodiscard]] constexpr Flags<BitType> operator&(BitType lhs, BitType rhs) noexcept {
    return Flags<BitType>(lhs) & rhs;
}

export template <typename BitType> requires (FlagTraits<BitType>::isBitmask)
[[nodiscard]] constexpr Flags<BitType> operator|(BitType lhs, BitType rhs) noexcept {
    return Flags<BitType>(lhs) | rhs;
}

export template <typename BitType> requires (FlagTraits<BitType>::isBitmask)
[[nodiscard]] constexpr Flags<BitType> operator^(BitType lhs, BitType rhs) noexcept {
    return Flags<BitType>(lhs) ^ rhs;
}

export template <typename BitType> requires (FlagTraits<BitType>::isBitmask)
[[nodiscard]] constexpr Flags<BitType> operator~(BitType bit) noexcept {
    return ~Flags<BitType>(bit);
}

template <typename BitType>
class Flags {
public:
    using MaskType = typename std::underlying_type_t<BitType>;

    constexpr Flags() noexcept = default;
    constexpr Flags(BitType bit) noexcept : mask { std::to_underlying(bit) } { }
    constexpr Flags(const Flags&) = default;
    explicit constexpr Flags(MaskType flags) noexcept : mask { flags } { }
    constexpr Flags &operator=(const Flags&) noexcept = default;

    [[nodiscard]] auto operator<=>(const Flags&) const = default;

    [[nodiscard]] constexpr bool operator!() const noexcept { return !mask; }
    [[nodiscard]] constexpr Flags operator&(const Flags &rhs) const noexcept { return Flags { static_cast<MaskType>(mask & rhs.mask) }; }
    [[nodiscard]] constexpr Flags operator|(const Flags &rhs) const noexcept { return Flags { static_cast<MaskType>(mask | rhs.mask) }; }
    [[nodiscard]] constexpr Flags operator^(const Flags &rhs) const noexcept { return Flags { static_cast<MaskType>(mask ^ rhs.mask) }; }
    [[nodiscard]] constexpr Flags operator~() const noexcept { return Flags { static_cast<MaskType>(mask ^ FlagTraits<BitType>::allFlags.mask) }; }

    constexpr Flags &operator|=(const Flags &rhs) noexcept { mask |= rhs.mask; return *this; }
    constexpr Flags &operator&=(const Flags &rhs) noexcept { mask &= rhs.mask; return *this; }
    constexpr Flags &operator^=(const Flags &rhs) noexcept { mask ^= rhs.mask; return *this; }

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return !!mask; }
    [[nodiscard]] explicit constexpr operator MaskType() const noexcept { return mask; }

private:
    MaskType mask;
};

export template <typename BitType, typename CharT>
struct std::formatter<Flags<BitType>, CharT> : range_formatter<BitType, CharT> {
    constexpr formatter() {
        range_formatter<BitType, CharT>::set_separator(" | ");
        range_formatter<BitType, CharT>::set_brackets("", "");
    }

    auto format(const Flags<BitType> &flags, auto &ctx) const {
        const auto flagMask = static_cast<typename Flags<BitType>::MaskType>(flags);

        static constexpr auto allFlagMask = static_cast<typename Flags<BitType>::MaskType>(FlagTraits<BitType>::allFlags);
        static constexpr std::size_t flagCount = std::popcount(allFlagMask);
        std::array<BitType, flagCount> flagBits; // TODO: use boost::container::static_vector or std::inplace_vector instead.

        std::size_t count = 0;
        for (std::size_t shift = 0; shift < flagCount; ++shift) {
            const auto bitmask = std::underlying_type_t<BitType> { 1 } << shift;
            if ((allFlagMask & bitmask) && (flagMask & bitmask)) {
                flagBits[count++] = static_cast<BitType>(bitmask);
            }
        }

        return range_formatter<BitType, CharT>::format(flagBits | std::views::take(count), ctx);
    }
};