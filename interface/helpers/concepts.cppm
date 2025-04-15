export module vk_gltf_viewer:helpers.concepts;

import std;

namespace concepts {
    /**
     * @brief Check if a type is a callable object with a signature of \p Signature.
     *
     * This is a shorthand of testing if \p F is convertible to <tt>std::function<Signature></tt>.
     */
    export template <typename F, typename Signature>
    concept signature_of = std::convertible_to<F, std::function<Signature>>;

    export template <typename T, typename... Ts>
    concept one_of = (std::same_as<T, Ts> || ...);
}