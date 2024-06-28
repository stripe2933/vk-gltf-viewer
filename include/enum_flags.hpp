/*
 * Helper macros for flag enums using Boost.Preprocessor.
 *
 * <Usage case 1>
 * Define flag enum operators.
 *
 * enum class Flags : std::uint8_t { A = 1, B = 2, C = 4, D = 8 };
 * ENUM_FLAG_UNARY_OPERATOR(Flags, ~); // Define operator~(Flags) -> Flags.
 * ENUM_FLAG_BINARY_OPERATOR(Flags, &); // Define operator&(Flags, Flags) -> Flags.
 * ENUM_FLAG_BINARY_OPERATOR(Flags, |); // Define operator|(Flags, Flags) -> Flags.
 * ENUM_FLAG_BINARY_OPERATOR(Flags, ^); // Define operator^(Flags, Flags) -> Flags.
 * ENUM_FLAG_IN_PLACE_BINARY_OPERATOR(Flags, &=); // Define operator&=(Flags&, Flags) -> Flags&.
 * ENUM_FLAG_IN_PLACE_BINARY_OPERATOR(Flags, |=); // Define operator|=(Flags&, Flags) -> Flags&.
 * ENUM_FLAG_IN_PLACE_BINARY_OPERATOR(Flags, ^=); // Define operator^=(Flags&, Flags) -> Flags&.
 *
 * You can use the universal definition macro ENUM_FLAG_OPERATORS(Flags) to define all operators at once.
 *
 * <Usage case 2>
 * Combine multiple enum values into a single value.
 *
 * ENUM_COMBINE(Flags, |, A, B, C); // Flags::A | Flags::B | Flags::C
 *
 * Bitwise AND and OR operations can be done with ENUM_AND and ENUM_OR.
 * ENUM_AND(Flags, C, D); // Flags::C & Flags::D
 * ENUM_OR(Flags, A, B, D); // Flags::A | Flags::B | Flags::D
 */

#pragma once

#include <boost/preprocessor/seq/transform.hpp>
#include <boost/preprocessor/seq/fold_left.hpp>
#include <boost/preprocessor/variadic/to_seq.hpp>

#define ENUM_FLAG_UNARY_OPERATOR(EnumType, Operator) \
    [[nodiscard]] constexpr auto operator Operator(EnumType lhs) noexcept -> EnumType { \
        return static_cast<EnumType>(Operator static_cast<std::underlying_type_t<EnumType>>(lhs)); \
    }
#define ENUM_FLAG_BINARY_OPERATOR(EnumType, Operator) \
    [[nodiscard]] constexpr auto operator Operator(EnumType lhs, EnumType rhs) noexcept -> EnumType { \
        return static_cast<EnumType>(static_cast<std::underlying_type_t<EnumType>>(lhs) Operator static_cast<std::underlying_type_t<EnumType>>(rhs)); \
    }
#define ENUM_FLAG_IN_PLACE_BINARY_OPERATOR(EnumType, Operator) \
    auto operator Operator(EnumType &lhs, EnumType rhs) noexcept -> EnumType& { \
        return (EnumType&)((std::underlying_type_t<EnumType>&)lhs Operator static_cast<std::underlying_type_t<EnumType>>(rhs)); \
    }
#define ENUM_FLAG_OPERATORS(EnumType) \
    ENUM_FLAG_UNARY_OPERATOR(EnumType, ~); \
    ENUM_FLAG_BINARY_OPERATOR(EnumType, &); \
    ENUM_FLAG_BINARY_OPERATOR(EnumType, |); \
    ENUM_FLAG_BINARY_OPERATOR(EnumType, ^); \
    ENUM_FLAG_IN_PLACE_BINARY_OPERATOR(EnumType, &=); \
    ENUM_FLAG_IN_PLACE_BINARY_OPERATOR(EnumType, |=); \
    ENUM_FLAG_IN_PLACE_BINARY_OPERATOR(EnumType, ^=);

#define ENUM_COMBINE_JOIN_LEFT(Index, State, Enum) State|Enum
#define ENUM_COMBINE_CAT_ENUMS(Seq) \
    BOOST_PP_SEQ_FOLD_LEFT(ENUM_COMBINE_JOIN_LEFT, BOOST_PP_SEQ_HEAD(Seq), BOOST_PP_SEQ_TAIL(Seq))
#define ENUM_COMBINE_PREFIX_TYPE(S, EnumType, EnumValue) EnumType::EnumValue
#define ENUM_COMBINE(EnumType, Operator, ...) \
    ENUM_COMBINE_CAT_ENUMS(BOOST_PP_SEQ_TRANSFORM(ENUM_COMBINE_PREFIX_TYPE, EnumType, BOOST_PP_VARIADIC_TO_SEQ(__VA_ARGS__)))
#define ENUM_OR(EnumType, ...) ENUM_COMBINE(EnumType, |, __VA_ARGS__)
#define ENUM_AND(EnumType, ...) ENUM_COMBINE(EnumType, &, __VA_ARGS__)