module;

#include <boost/container/static_vector.hpp>

export module vk_gltf_viewer.helpers.Flags;

import std;

export template <typename BitType>
class Flags;

export template <typename>
struct FlagTraits {
    static constexpr bool isBitmask = false;
};

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

    [[nodiscard]] constexpr operator bool() const noexcept { return !!mask; }
    [[nodiscard]] explicit constexpr operator MaskType() const noexcept { return mask; }

private:
    MaskType mask;
};

export template <typename BitType, typename CharT>
struct std::formatter<Flags<BitType>, CharT> : formatter<BitType, CharT> {
    [[nodiscard]] auto format(const Flags<BitType> &flags, auto &ctx) const {
        const typename Flags<BitType>::MaskType flagMask { flags };

        constexpr typename Flags<BitType>::MaskType allFlagMask { FlagTraits<BitType>::allFlags };
        constexpr std::size_t flagCount = std::popcount(allFlagMask);

        boost::container::static_vector<BitType, flagCount> flagBits;
        typename Flags<BitType>::MaskType bitmask { 1 };
        for (std::size_t i = 0; i < flagCount; bitmask <<= 1) {
            if (allFlagMask & bitmask) {
                if (flagMask & bitmask) {
                    flagBits.push_back(static_cast<BitType>(bitmask));
                }
                ++i;
            }
        }

        if (!flagBits.empty()) {
            formatter<BitType, CharT>::format(flagBits[0], ctx);
            for (std::size_t i = 1; i < flagBits.size(); ++i) {
                format_to(ctx.out(), " | ");
                formatter<BitType, CharT>::format(flagBits[i], ctx);
            }
        }

        return ctx.out();
    }
};