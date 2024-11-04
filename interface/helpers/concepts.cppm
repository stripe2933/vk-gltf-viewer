export module vk_gltf_viewer:helpers.concepts;

import std;

namespace concepts {
    export template <typename F, typename R, typename... Ts>
    concept signature_of = requires(F f, Ts... ts) {
        { f(std::forward<Ts>(ts)...) } -> std::same_as<R>;
    };

    export template <typename F, typename R, typename... Ts>
    concept compatible_signature_of = requires(F f, Ts... ts) {
        { f(std::forward<Ts>(ts)...) } -> std::convertible_to<R>;
    };
}