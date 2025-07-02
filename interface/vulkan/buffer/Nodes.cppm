module;

#include <cstddef>

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.buffer.Nodes;

import std;
export import fastgltf;
import vku;
export import vk_mem_alloc_hpp;

import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.optional;
import vk_gltf_viewer.helpers.ranges;
export import vk_gltf_viewer.vulkan.buffer.InstancedNodeWorldTransforms;
export import vk_gltf_viewer.vulkan.buffer.InverseBindMatrices;
export import vk_gltf_viewer.vulkan.buffer.MorphTargetWeights;
export import vk_gltf_viewer.vulkan.buffer.SkinJointIndices;
import vk_gltf_viewer.vulkan.shader_type.Node;

namespace vk_gltf_viewer::vulkan::buffer {
    export class Nodes {
    public:
        Nodes(
            const vk::raii::Device &device LIFETIMEBOUND,
            vma::Allocator allocator LIFETIMEBOUND,
            const fastgltf::Asset &asset LIFETIMEBOUND,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const InstancedNodeWorldTransforms *instancedNodeWorldTransformBuffer LIFETIMEBOUND = nullptr,
            const MorphTargetWeights *morphTargetWeightBuffer LIFETIMEBOUND = nullptr,
            const std::pair<SkinJointIndices, InverseBindMatrices> *skinJointIndexAndInverseBindMatrixBuffers LIFETIMEBOUND = nullptr
        );

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept;

        /**
         * @brief Update the node world transforms at \p nodeIndex.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransform World transform matrix of the node.
         */
        void update(std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform);

        /**
         * @brief Update the node world transforms from given \p nodeIndex, to its descendants.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
         */
        void updateHierarchical(std::size_t nodeIndex, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms);

        /**
         * @brief Update the node world transforms for all nodes in a scene.
         * @param scene Scene to be updated.
         * @param nodeWorldTransforms Node world transform matrices that is indexed by node index.
         */
        void update(const fastgltf::Scene &scene, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms);

    private:
        std::reference_wrapper<const fastgltf::Asset> asset;
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
    };
}

#ifndef __GNUC
module :private;
#endif

vk_gltf_viewer::vulkan::buffer::Nodes::Nodes(
    const vk::raii::Device &device,
    vma::Allocator allocator,
    const fastgltf::Asset &asset,
    std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
    const InstancedNodeWorldTransforms *instancedNodeWorldTransformBuffer,
    const MorphTargetWeights *morphTargetWeightBuffer,
    const std::pair<SkinJointIndices, InverseBindMatrices> *skinJointIndexAndInverseBindMatrixBuffers
) : asset { asset },
    buffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(shader_type::Node) * asset.nodes.size(),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferDevice,
        },
    },
    descriptorInfo { buffer, 0, vk::WholeSize } {
    const vk::DeviceAddress selfDeviceAddress = device.getBufferAddress({ buffer.buffer });

    vk::DeviceAddress instanceNodeWorldTransformBufferAddress = 0;
    if (instancedNodeWorldTransformBuffer) {
        instanceNodeWorldTransformBufferAddress = device.getBufferAddress({ *instancedNodeWorldTransformBuffer });
    }

    vk::DeviceAddress morphTargetWeightBufferAddress = 0;
    if (morphTargetWeightBuffer) {
        morphTargetWeightBufferAddress = device.getBufferAddress({ *morphTargetWeightBuffer });
    }

    vk::DeviceAddress skinJointIndexBufferAddress = 0;
    vk::DeviceAddress inverseBindMatrixBufferAddress = 0;
    if (skinJointIndexAndInverseBindMatrixBuffers) {
        skinJointIndexBufferAddress = device.getBufferAddress({ skinJointIndexAndInverseBindMatrixBuffers->first.buffer });
        inverseBindMatrixBufferAddress = device.getBufferAddress({ skinJointIndexAndInverseBindMatrixBuffers->second.buffer });
    }

    const std::span data = buffer.asRange<shader_type::Node>();
    for (const auto &[nodeIndex, node] : asset.nodes | ranges::views::enumerate) {
        data[nodeIndex].worldTransform = glm::make_mat4(nodeWorldTransforms[nodeIndex].data());

        if (node.instancingAttributes.empty()) {
            // Use address of self's worldTransform if no instancing attributes are presented.
            data[nodeIndex].pInstancedWorldTransformBuffer
                = selfDeviceAddress + sizeof(shader_type::Node) * nodeIndex + offsetof(shader_type::Node, worldTransform);
        }
        else {
            // Use address of instanced node world transform buffer if instancing attributes are presented.
            data[nodeIndex].pInstancedWorldTransformBuffer
                = instanceNodeWorldTransformBufferAddress
                + sizeof(glm::mat4) * instancedNodeWorldTransformBuffer->nodeInstanceCountExclusiveScanWithCount.get()[nodeIndex];
        }

        if (morphTargetWeightBuffer) {
            data[nodeIndex].pMorphTargetWeightBuffer
                = morphTargetWeightBufferAddress
                + sizeof(float) * morphTargetWeightBuffer->targetWeightCountExclusiveScanWithCount.get()[nodeIndex];
        }

        if (skinJointIndexAndInverseBindMatrixBuffers && node.skinIndex) {
            const std::size_t offset = skinJointIndexAndInverseBindMatrixBuffers->first.skinJointCountExclusiveScanWithCount.get()[*node.skinIndex];
            data[nodeIndex].pSkinJointIndexBuffer = skinJointIndexBufferAddress + sizeof(std::uint32_t) * offset;
            data[nodeIndex].pInverseBindMatrixBuffer = inverseBindMatrixBufferAddress + sizeof(fastgltf::math::fmat4x4) * offset;
        }
        else {
            // Use 0 if no skin joint index buffer is presented.
            data[nodeIndex].pSkinJointIndexBuffer = 0;
        }
    }
}

const vk::DescriptorBufferInfo &vk_gltf_viewer::vulkan::buffer::Nodes::getDescriptorInfo() const noexcept {
    return descriptorInfo;
}

void vk_gltf_viewer::vulkan::buffer::Nodes::update(std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
    shader_type::Node &node = buffer.asRange<shader_type::Node>()[nodeIndex];
    node.worldTransform = glm::make_mat4(nodeWorldTransform.data());
}

void vk_gltf_viewer::vulkan::buffer::Nodes::updateHierarchical(std::size_t nodeIndex, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms) {
    const std::span bufferData = buffer.asRange<shader_type::Node>();
    traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
        bufferData[nodeIndex].worldTransform = glm::make_mat4(nodeWorldTransforms[nodeIndex].data());
    });
}

void vk_gltf_viewer::vulkan::buffer::Nodes::update(const fastgltf::Scene &scene, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms) {
    for (std::size_t nodeIndex : scene.nodeIndices) {
        updateHierarchical(nodeIndex, nodeWorldTransforms);
    }
}