export module vk_gltf_viewer:helpers.type_variant;

import std;
import :helpers.concepts;

export template <typename... Ts>
class type_variant {
public:
    // --------------------
    // Constructors.
    // --------------------

    /**
     * Default constructor, variant set with the first alternative type.
     */
    constexpr type_variant() noexcept = default;

    /**
     * Initialize the variant with given type.
     * @tparam T Type to initialize. Must be one of the alternative types.
     */
    template <concepts::one_of<Ts...> T>
    constexpr type_variant() noexcept { emplace<T>(); }

    // Copy constructor.
    constexpr type_variant(const type_variant&) = default;

    // Move constructor.
    constexpr type_variant(type_variant&&) noexcept = default;

    // --------------------
    // Methods.
    // --------------------

    /**
     * Set the variant with given type \p T.
     * @tparam T Type to set. Must be one of the alternative types.
     */
    template <concepts::one_of<Ts...> T>
    auto emplace() noexcept -> void {
        v.template emplace<std::type_identity<T>>();
    }

    /**
     * Check if a variant currently holds a given type.
     * @tparam T Type to check. Must be one of the alternative types.
     * @return <tt>true</tt> if the variant holds the given type, <tt>false</tt> otherwise.
     */
    template <concepts::one_of<Ts...> T>
    auto holds_alternative() const noexcept -> bool {
        return std::holds_alternative<std::type_identity<T>>(v);
    }

private:
    std::variant<std::type_identity<Ts>...> v;
};