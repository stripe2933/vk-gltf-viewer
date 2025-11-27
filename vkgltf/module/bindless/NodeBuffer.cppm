module;

#include <cassert>
#include <cstddef>

#include <lifetimebound.hpp>

export module vkgltf.bindless.NodeBuffer;

import std;
export import fastgltf;
export import vkgltf;
import vkgltf.util;
export import vku;

export import vkgltf.bindless.shader_type.Node;
export import vkgltf.bindless.SkinBuffer;

namespace vkgltf {
    /**
     * @brief Vulkan buffer of every addressable node data in the glTF asset.
     *
     * The following diagram explains the data layout of the buffer.
     *
     *   0 +------------------+-------------------------------+
     *     |                  |     worldTransform: mat4      |
     *  64 |                  |-------------------------------+
     *     |                  | &(instance transform buffer)  | ---+ (NULL if not instanced)
     *  72 |                  |-------------------------------+    |
     *     |       Node       | &(morph target weight buffer) | ---+----+ (NULL if no morph targets)
     *  80 |                  |-------------------------------+    |    |
     *     |                  |  &(skin joint index buffer)   | ---+----+-----+
     *  88 |                  |-------------------------------+    |    |     |--> (external if provided, NULL otherwise)
     *     |                  | &(inverse bind matrix buffer) | ---+----+-----+
     *  96 |------------------|-------------------------------+    |    |
     *     |       ...        |                                    |    |
     *     |------------------|                                    |    |
     *     |       Node       |                                    |    |
     *     |------------------| <----------------------------------+----+ Instance transform data starts from here with 16-byte alignment.
     *     |       mat4       |                                    |
     *     |------------------|                                    |
     *     |       mat4       |                                    |
     *     |------------------|                                    |
     *     |        ...       |                                    |
     *     |------------------|                                    |
     *     |       mat4       |                                    |
     *     |------------------| <----------------------------------+ Morph target weight data starts from here with 4-byte alignment.
     *     |       float      |
     *     |------------------|
     *     |       float      |
     *     |------------------|
     *     |        ...       |
     *     |------------------|
     *     |       float      |
     *     |------------------|
     */
    export class NodeBuffer : public vku::raii::AllocatedBuffer {
    public:
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        class Config {
        public:
            const BufferDataAdapter &adapter;

            /**
             * @brief Skin information that be embedded to the node buffer, if set.
             *
             * If set, device addresses of <tt>SkinBuffer::jointIndices</tt> and <tt>SkinBuffer::inverseBindMatrices</tt>
             * is obtained from this skin buffer and embedded to the node buffer.
             */
            const SkinBuffer *skinBuffer = nullptr;

            /**
             * @brief Vulkan buffer usage flags for the buffer creation.
             */
            vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;

            /**
             * @brief Queue family indices that the buffer can be concurrently accessed.
             *
             * If its size is less than 2, <tt>sharingMode</tt> of the buffer will be set to <tt>vk::SharingMode::eExclusive</tt>.
             */
            vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};

            /**
             * @brief VMA allocation creation flags for the buffer allocation.
             */
            vma::AllocationCreateInfo allocationCreateInfo = vma::AllocationCreateInfo {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                vma::MemoryUsage::eAutoPreferDevice,
            };
        };

        vk::DescriptorBufferInfo descriptorInfo;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        NodeBuffer(
            const fastgltf::Asset &asset LIFETIMEBOUND,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const vk::raii::Device &device LIFETIMEBOUND,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config = {}
        ) : NodeBuffer { asset, nodeWorldTransforms, device, allocator, config, IData { asset } } { }

        /// Get byte offset of world transform matrix data of the node at \p nodeIndex.
        [[nodiscard]] static vk::DeviceSize getWorldTransformDataOffset(std::size_t nodeIndex) noexcept;

        /// Get byte offset of instance transform matrix data of the node at \p nodeIndex.
        [[nodiscard]] vk::DeviceSize getInstanceTransformDataOffset(std::size_t nodeIndex) const noexcept;

        /// Get byte offset of morph target weight data of the node at \p nodeIndex.
        [[nodiscard]] vk::DeviceSize getTargetWeightsDataOffset(std::size_t nodeIndex) const noexcept;

        /**
         * @brief Update the world transform matrix of the node at \p nodeIndex.
         * @param nodeIndex Index of the node to be updated.
         * @param nodeWorldTransform World transform matrix of the node.
         * @note Prefer to use <tt>updateBulk()</tt> if you want to update multiple nodes at once. If the buffer is not
         * host coherent, it will call <tt>vkFlushMappedMemoryRanges</tt> for every update, which may degrade the performance.
         */
        void update(std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform);

        /**
         * @brief Update the world transform matrices of all nodes given by \p sortedNodeIndices.
         * @param sortedNodeIndices Indices of the nodes to be updated. Must be nonempty and sorted in ascending order.
         * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
         * @note This method is optimized for updating multiple nodes at once. It batches the memory flush operation
         * by identifying the contiguous memory regions to be updated.
         */
        void updateBulk(std::span<const std::size_t> sortedNodeIndices, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms);

    private:
        struct IData {
            vk::DeviceSize bufferSize;
            vk::DeviceSize instanceTransformDataByteOffset;
            std::size_t instanceTransformMatrixCount;
            vk::DeviceSize morphTargetWeightDataByteOffset;
            std::size_t morphTargetWeightCount;

            explicit IData(const fastgltf::Asset &asset);
        };

        std::span<shader_type::Node> nodes;
        std::vector<vk::DeviceSize> instanceTransformDataOffsets;
        std::vector<vk::DeviceSize> morphTargetWeightDataOffsets;

        template <typename BufferDataAdapter>
        NodeBuffer(
            const fastgltf::Asset &asset,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const vk::raii::Device &device LIFETIMEBOUND,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config,
            const IData &intermediateData
        ) : AllocatedBuffer {
                allocator,
                vk::BufferCreateInfo {
                    {},
                    intermediateData.bufferSize,
                    config.usageFlags,
                    vku::getSharingMode(config.queueFamilies),
                    config.queueFamilies,
                },
                config.allocationCreateInfo,
            },
            descriptorInfo { buffer, 0, sizeof(shader_type::Node) * asset.nodes.size() } {
            vk::DeviceAddress skinJointIndexBufferAddress;
            vk::DeviceAddress inverseBindMatrixBufferAddress;
            if (config.skinBuffer) {
                skinJointIndexBufferAddress = device.getBufferAddress({ static_cast<vk::Buffer>(config.skinBuffer->jointIndices) });
                inverseBindMatrixBufferAddress = device.getBufferAddress({ static_cast<vk::Buffer>(config.skinBuffer->inverseBindMatrices) });
            }

            std::byte* const mapped = static_cast<std::byte*>(allocator.getAllocationInfo(allocation).pMappedData);
            nodes = std::span { reinterpret_cast<shader_type::Node*>(mapped), asset.nodes.size() };

            const vk::DeviceAddress selfDeviceAddress = device.getBufferAddress({ static_cast<vk::Buffer>(*this) });

            vk::DeviceAddress instanceTransformBufferAddress = selfDeviceAddress + intermediateData.instanceTransformDataByteOffset;
            auto instanceTransformIt = std::span {
                reinterpret_cast<fastgltf::math::fmat4x4*>(mapped + intermediateData.instanceTransformDataByteOffset),
                intermediateData.instanceTransformMatrixCount,
            }.begin();
            instanceTransformDataOffsets.reserve(asset.nodes.size());

            vk::DeviceAddress morphTargetWeightBufferAddress = selfDeviceAddress + intermediateData.morphTargetWeightDataByteOffset;
            auto morphTargetWeightIt = std::span {
                reinterpret_cast<float*>(mapped + intermediateData.morphTargetWeightDataByteOffset),
                intermediateData.morphTargetWeightCount,
            }.begin();
            morphTargetWeightDataOffsets.reserve(asset.nodes.size());

            for (std::size_t nodeIndex = 0; nodeIndex < asset.nodes.size(); ++nodeIndex) {
                const fastgltf::Node &node = asset.nodes[nodeIndex];
                shader_type::Node &shaderNode = (nodes[nodeIndex] = shader_type::Node {
                    .worldTransform = nodeWorldTransforms[nodeIndex],
                });

                // Instance transform matrices.
                instanceTransformDataOffsets.push_back(instanceTransformBufferAddress - selfDeviceAddress);
                if (!node.instancingAttributes.empty()) {
                    // Use address of instanced node world transform buffer if instancing attributes are presented.
                    shaderNode.pInstanceTransformBuffer = instanceTransformBufferAddress;
                    std::vector<fastgltf::math::fmat4x4> instanceTransforms = utils::getInstanceTransforms(asset, nodeIndex, config.adapter);
                    instanceTransformIt = std::ranges::copy(instanceTransforms, instanceTransformIt).out;
                    instanceTransformBufferAddress += sizeof(fastgltf::math::fmat4x4) * instanceTransforms.size();
                }

                // Morph target weights.
                morphTargetWeightDataOffsets.push_back(morphTargetWeightBufferAddress - selfDeviceAddress);
                if (std::span targetWeights = utils::getTargetWeights(asset, node); !targetWeights.empty()) {
                    shaderNode.pMorphTargetWeightBuffer = morphTargetWeightBufferAddress;
                    morphTargetWeightIt = std::ranges::copy(targetWeights, morphTargetWeightIt).out;
                    morphTargetWeightBufferAddress += sizeof(float) * targetWeights.size();
                }

                // Skinning data.
                if (config.skinBuffer && node.skinIndex) {
                    shaderNode.pSkinJointIndexBuffer = skinJointIndexBufferAddress + config.skinBuffer->getJointIndicesOffsetAndSize(*node.skinIndex).first;
                    shaderNode.pInverseBindMatrixBuffer = inverseBindMatrixBufferAddress + config.skinBuffer->getInverseBindMatricesOffsetAndSize(*node.skinIndex).first;
                }
            }

            allocator.flushAllocation(allocation, 0, vk::WholeSize);
        }
    };

    export template <>
    class NodeBuffer::Config<fastgltf::DefaultBufferDataAdapter> {
        static constexpr fastgltf::DefaultBufferDataAdapter adapter;

        // Make adapter accessible by NodeBuffer.
        friend class NodeBuffer;

    public:
        const SkinBuffer *skinBuffer = nullptr;
        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};
        vma::AllocationCreateInfo allocationCreateInfo = vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferDevice,
        };
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

vk::DeviceSize vkgltf::NodeBuffer::getWorldTransformDataOffset(std::size_t nodeIndex) noexcept {
    return sizeof(shader_type::Node) * nodeIndex + offsetof(shader_type::Node, worldTransform);
}

vk::DeviceSize vkgltf::NodeBuffer::getInstanceTransformDataOffset(std::size_t nodeIndex) const noexcept {
    return instanceTransformDataOffsets[nodeIndex];
}

vk::DeviceSize vkgltf::NodeBuffer::getTargetWeightsDataOffset(std::size_t nodeIndex) const noexcept {
    return morphTargetWeightDataOffsets[nodeIndex];
}

void vkgltf::NodeBuffer::update(std::size_t nodeIndex, const fastgltf::math::fmat4x4 &nodeWorldTransform) {
    allocator.copyMemoryToAllocation(&nodeWorldTransform, allocation, getWorldTransformDataOffset(nodeIndex), sizeof(nodeWorldTransform));
}

void vkgltf::NodeBuffer::updateBulk(std::span<const std::size_t> sortedNodeIndices, std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms) {
    assert(!sortedNodeIndices.empty() && "sortedNodeIndices must not be empty.");
    assert(std::ranges::is_sorted(sortedNodeIndices) && "sortedNodeIndices must be sorted in ascending order.");

    void* const mapped = allocator.getAllocationInfo(allocation).pMappedData;
    for (std::size_t nodeIndex : sortedNodeIndices) {
        void* const dst = vku::offsetPtr(mapped, getWorldTransformDataOffset(nodeIndex));
        std::memcpy(dst, &nodeWorldTransforms[nodeIndex], sizeof(fastgltf::math::fmat4x4));
    }

    if (!vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
        // Find the contiguous memory regions to be flushed.
        std::vector<vk::DeviceSize> flushOffsets, flushSizes;
        auto it = sortedNodeIndices.begin();
        while (true) {
            flushOffsets.push_back(getWorldTransformDataOffset(*it));

            it = std::adjacent_find(it, sortedNodeIndices.end(), [](std::size_t a, std::size_t b) noexcept {
                return b - a != 1;
            });

            if (it == sortedNodeIndices.end()) {
                flushSizes.push_back(vk::WholeSize);
                break;
            }
            else {
                flushSizes.push_back(getWorldTransformDataOffset(*it) + sizeof(shader_type::Node::worldTransform) - flushOffsets.back());
                ++it;
            }
        }

        const std::vector allocations(flushOffsets.size(), allocation);
        allocator.flushAllocations(allocations, flushOffsets, flushSizes);
    }
}

vkgltf::NodeBuffer::IData::IData(const fastgltf::Asset &asset)
    : bufferSize { sizeof(shader_type::Node) * asset.nodes.size() } {
    instanceTransformMatrixCount = 0;
    for (const fastgltf::Node &node : asset.nodes) {
        if (!node.instancingAttributes.empty()) {
            instanceTransformMatrixCount += asset.accessors[node.instancingAttributes[0].accessorIndex].count;
        }
    }

    instanceTransformDataByteOffset = bufferSize;
    bufferSize = instanceTransformDataByteOffset + sizeof(fastgltf::math::fmat4x4) * instanceTransformMatrixCount;

    morphTargetWeightCount = 0;
    for (const fastgltf::Node &node : asset.nodes) {
        morphTargetWeightCount += utils::getTargetWeightCount(asset, node);
    }

    morphTargetWeightDataByteOffset = bufferSize;
    bufferSize = morphTargetWeightDataByteOffset + sizeof(float) * morphTargetWeightCount;
}