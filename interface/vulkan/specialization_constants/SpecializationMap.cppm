export module vk_gltf_viewer.vulkan.specialization_constants.SpecializationMap;

import std;
import reflect;
export import vulkan_hpp;

#define INDEX_SEQ(Is, N, ...) []<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})

namespace vk_gltf_viewer::vulkan::inline specialization_constants {
    export template <typename T> requires std::is_aggregate_v<T>
    struct SpecializationMap {
        static constexpr std::array<vk::SpecializationMapEntry, reflect::size<T>()> value
            = INDEX_SEQ(Is, reflect::size<T>(), {
                return std::array { vk::SpecializationMapEntry { Is, reflect::offset_of<Is, T>(), reflect::size_of<Is, T>() }... };
            });
    };
}