export module vk_gltf_viewer:helpers.vulkan;

import std;
export import vulkan_hpp;

export enum class TopologyClass : std::uint8_t {
    Point,
    Line,
    Triangle,
    Patch,
};

/**
 * Get topology class from \p primitiveTopology.
 *
 * You can find more information about the term "Topology Class" from Vulkan specification.
 * https://vkdoc.net/chapters/drawing#drawing-primitive-topology-class
 *
 * @param primitiveTopology Primitive topology.
 * @return Topology class.
 */
export
[[nodiscard]] TopologyClass getTopologyClass(vk::PrimitiveTopology primitiveTopology) noexcept {
    switch (primitiveTopology) {
    case vk::PrimitiveTopology::ePointList:
        return TopologyClass::Point;
    case vk::PrimitiveTopology::eLineList:
    case vk::PrimitiveTopology::eLineStrip:
    case vk::PrimitiveTopology::eLineListWithAdjacency:
    case vk::PrimitiveTopology::eLineStripWithAdjacency:
        return TopologyClass::Line;
    case vk::PrimitiveTopology::eTriangleList:
    case vk::PrimitiveTopology::eTriangleStrip:
    case vk::PrimitiveTopology::eTriangleFan:
    case vk::PrimitiveTopology::eTriangleListWithAdjacency:
    case vk::PrimitiveTopology::eTriangleStripWithAdjacency:
        return TopologyClass::Triangle;
    case vk::PrimitiveTopology::ePatchList:
        return TopologyClass::Patch;
    }
    std::unreachable();
}

/**
 * @brief Get one of the <tt>vk::PrimitiveTopology</tt> type that is match to the given \p topologyClass.
 *
 * This function is useful when you're using dynamic state for primitive topology (VK_EXT_extended_dynamic_state), and
 * system GPU does not support <tt>VkPhysicalDeviceExtendedDynamicState3PropertiesEXT::dynamicPrimitiveTopologyUnrestricted</tt>.
 * You can pass the representative primitive topology to the PSO initialization and change the primitive topology dynamically.
 *
 * @param topologyClass Topology class.
 * @return One of the <tt>vk::PrimitiveTopology</tt> type that is match to the given \p topologyClass.
 * @note Currently its implementation returns XXXList type of primitive topology, but you should not rely on this behavior.
 */
export
[[nodiscard]] vk::PrimitiveTopology getRepresentativePrimitiveTopology(TopologyClass topologyClass) noexcept {
    switch (topologyClass) {
    case TopologyClass::Point:
        return vk::PrimitiveTopology::ePointList;
    case TopologyClass::Line:
        return vk::PrimitiveTopology::eLineList;
    case TopologyClass::Triangle:
        return vk::PrimitiveTopology::eTriangleList;
    case TopologyClass::Patch:
        return vk::PrimitiveTopology::ePatchList;
    }
    std::unreachable();
}