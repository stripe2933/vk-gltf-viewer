module;

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
     *   0 +------------------+------------------------------------+ <----+
     *     |                  |        worldTransform: mat4        |      | Reference to the self's world transform
     *  64 |                  |------------------------------------+      | if node is not instanced.
     *     |                  | &(instance world transform buffer) | -----+
     *  72 |                  |------------------------------------+
     *     |       Node       |    &(morph target weight buffer)   | ---+
     *  80 |                  |------------------------------------+    | Reference to the contiguous morph target weights,
     *     |                  |     &(skin joint index buffer)     | -+ | if exists.
     *  88 |                  |------------------------------------+  | |
     *     |                  |    &(inverse bind matrix buffer)   |  +-+----------------> +----------------------+
     *  96 |------------------|------------------------------------+    |                  | External skin buffer |
     *     |                  |        worldTransform: mat4        |    |                  +----------------------+
     *     |                  |------------------------------------+    |
     *     |                  | &(instance world transform buffer) | ---+----+
     *     |                  |------------------------------------+    |    | Reference to the contiguous instanced world
     *     |       Node       |    &(morph target weight buffer)   | -+ |    | transform matrices, if node is instanced.
     *     |                  |------------------------------------+  | |    |
     *     |                  |     &(skin joint index buffer)     |  | |    | Therefore, regardless of whether the node
     *     |                  |------------------------------------+  | |    | is instanced or not, this field always
     *     |                  |    &(inverse bind matrix buffer)   |  | |    | contains valid device address.
     *     |------------------|------------------------------------+  | |    |
     *     |       ...        |                                       | |    |
     *     |------------------|                                       | |    |
     *     |       Node       |                                       | |    |
     *     |------------------| <-------------------------------------+-+----+ Instance transform matrices starts from here.
     *     |       mat4       |                                       | |
     *     |------------------|                                       | |
     *     |       mat4       |                                       | |
     *     |------------------|                                       | |
     *     |        ...       |                                       | |
     *     |------------------|                                       | |
     *     |       mat4       |                                       | |
     *     |------------------| <-------------------------------------+-+ Morph target weights starts from here.
     *     |       float      |                                       |
     *     |------------------|                                       |
     *     |       float      |                                       |
     *     |------------------|                                       |
     *     |        ...       |                                       |
     *     |------------------|                                       |
     *     |       float      |                                       |
     *     |------------------| <-------------------------------------+
     *     |       float      |
     *     |------------------|
     *     |       float      |
     *     |------------------|
     *     |        ...       |
     *     |------------------|
     *     |       float      |
     *     |------------------|
     *     |        ...       |
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
             *
             * @note <tt>flags</tt> MUST contain either <tt>vma::AllocationCreateFlagBits::eHostAccessSequentialWrite</tt> or
             * <tt>vma::AllocationCreateFlagBits::eHostAccessRandom</tt> to allow the host to write to the buffer.
             * @warning If <tt>vma::AllocationCreateFlagBits::eHostAccessSequentialWrite</tt> is used, usage of buffer
             * reading methods (<tt>getWorldTransform</tt>, <tt>getInstanceWorldTransforms</tt>, <tt>getMorphTargetWeights</tt>)
             * are highly discouraged, as reading operation will be extremely slow. Use the methods for sequential
             * writing purpose only.
             */
            vma::AllocationCreateInfo allocationCreateInfo = vma::AllocationCreateInfo {
                vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
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

        [[nodiscard]] fastgltf::math::fmat4x4 &getWorldTransform(std::size_t nodeIndex) noexcept;
        [[nodiscard]] const fastgltf::math::fmat4x4 &getWorldTransform(std::size_t nodeIndex) const noexcept;
        [[nodiscard]] std::pair<vk::DeviceSize, vk::DeviceSize> getWorldTransformOffsetAndSize(std::size_t nodeIndex) const noexcept;

        [[nodiscard]] std::span<fastgltf::math::fmat4x4> getInstanceWorldTransforms(std::size_t nodeIndex) noexcept;
        [[nodiscard]] std::span<const fastgltf::math::fmat4x4> getInstanceWorldTransforms(std::size_t nodeIndex) const noexcept;
        [[nodiscard]] std::pair<vk::DeviceSize, vk::DeviceSize> getInstanceWorldTransformsOffsetAndSize(std::size_t nodeIndex) const noexcept;

        [[nodiscard]] std::span<float> getMorphTargetWeights(std::size_t nodeIndex) noexcept;
        [[nodiscard]] std::span<const float> getMorphTargetWeights(std::size_t nodeIndex) const noexcept;
        [[nodiscard]] std::pair<vk::DeviceSize, vk::DeviceSize> getTargetWeightsOffsetAndSize(std::size_t nodeIndex) const noexcept;

        /**
         * @brief Update the node world transforms (and optionally its instance world transforms) at \p nodeIndex.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransform World transform matrix of the node.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            std::size_t nodeIndex,
            const fastgltf::math::fmat4x4 &nodeWorldTransform,
            const BufferDataAdapter &adapter = {}
        ) {
            nodes[nodeIndex].worldTransform = nodeWorldTransform;

            const bool hasInstancingAttributes = !asset.get().nodes[nodeIndex].instancingAttributes.empty();
            if (hasInstancingAttributes) {
                std::ranges::transform(
                    vkgltf::getInstanceTransforms(asset, nodeIndex, adapter),
                    instanceWorldTransformsByNode[nodeIndex].begin(),
                    [&](const fastgltf::math::fmat4x4 &instanceTransform) noexcept {
                        return nodeWorldTransform * instanceTransform;
                    });
            }

            if (!isHostCoherent) {
                const auto [offset, size] = getWorldTransformOffsetAndSize(nodeIndex);
                allocator.flushAllocation(allocation, offset, size);

                if (hasInstancingAttributes) {
                    const auto [offset, size] = getInstanceWorldTransformsOffsetAndSize(nodeIndex);
                    allocator.flushAllocation(allocation, offset, size);
                }
            }
        }

        /**
         * @brief Update the node world transforms (and optionally its instance world transforms) from given \p nodeIndex, to its descendants.
         * @param nodeIndex Node index to be started.
         * @param nodeWorldTransforms Node world transform matrices ordered by node indices in the asset.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void updateHierarchical(
            std::size_t nodeIndex,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                update(nodeIndex, nodeWorldTransforms[nodeIndex], adapter);
            });
        }

        /**
         * @brief Update the node world transforms (and optionally its instance world transforms) for all nodes in a scene.
         * @param scene Scene to be updated.
         * @param nodeWorldTransforms Node world transform matrices that is indexed by node index.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void update(
            const fastgltf::Scene &scene,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            for (std::size_t nodeIndex : scene.nodeIndices) {
                updateHierarchical(nodeIndex, nodeWorldTransforms, adapter);
            }
        }

    private:
        struct IData {
            vk::DeviceSize bufferSize;
            vk::DeviceSize instanceTransformDataByteOffset;
            std::size_t instanceTransformMatrixCount;
            vk::DeviceSize morphTargetWeightDataByteOffset;
            std::size_t morphTargetWeightCount;

            explicit IData(const fastgltf::Asset &asset);
        };

        std::reference_wrapper<const fastgltf::Asset> asset;
        std::span<shader_type::Node> nodes;
        std::vector<std::span<fastgltf::math::fmat4x4>> instanceWorldTransformsByNode;
        std::vector<std::span<float>> morphTargetWeightsByNode;
        bool isHostCoherent;

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
            descriptorInfo { buffer, 0, sizeof(shader_type::Node) * asset.nodes.size() },
            asset { asset },
            isHostCoherent { vku::contains(allocator.getAllocationMemoryProperties(allocation), vk::MemoryPropertyFlagBits::eHostCoherent) } {
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
            instanceWorldTransformsByNode.reserve(asset.nodes.size());

            vk::DeviceAddress morphTargetWeightBufferAddress = selfDeviceAddress + intermediateData.morphTargetWeightDataByteOffset;
            auto morphTargetWeightIt = std::span {
                reinterpret_cast<float*>(mapped + intermediateData.morphTargetWeightDataByteOffset),
                intermediateData.morphTargetWeightCount,
            }.begin();
            morphTargetWeightsByNode.reserve(asset.nodes.size());

            for (std::size_t nodeIndex = 0; const fastgltf::Node &node : asset.nodes) {
                shader_type::Node &shaderNode = (nodes[nodeIndex] = shader_type::Node {
                    .worldTransform = nodeWorldTransforms[nodeIndex],
                });

                // Instance transform matrices.
                if (node.instancingAttributes.empty()) {
                    // Use address of self's worldTransform if no instancing attributes are presented.
                    shaderNode.pInstancedWorldTransformBuffer
                        = selfDeviceAddress + sizeof(shader_type::Node) * nodeIndex + offsetof(shader_type::Node, worldTransform);
                    instanceWorldTransformsByNode.emplace_back(std::to_address(instanceTransformIt), 0);
                }
                else {
                    // Use address of instanced node world transform buffer if instancing attributes are presented.
                    shaderNode.pInstancedWorldTransformBuffer = instanceTransformBufferAddress;
                    std::vector<fastgltf::math::fmat4x4> instanceTransforms = vkgltf::getInstanceTransforms(asset, nodeIndex, config.adapter);
                    instanceWorldTransformsByNode.emplace_back(std::to_address(instanceTransformIt), instanceTransforms.size());
                    instanceTransformIt = std::ranges::transform(
                        instanceTransforms,
                        instanceTransformIt,
                        [&](const fastgltf::math::fmat4x4 &instanceTransform) noexcept {
                            return nodeWorldTransforms[nodeIndex] * instanceTransform;
                        }).out;
                    instanceTransformBufferAddress += sizeof(fastgltf::math::fmat4x4) * instanceTransforms.size();
                }

                // Morph target weights.
                shaderNode.pMorphTargetWeightBuffer = morphTargetWeightBufferAddress;
                const std::span targetWeights = getTargetWeights(asset, node);
                morphTargetWeightsByNode.emplace_back(std::to_address(morphTargetWeightIt), targetWeights.size());
                morphTargetWeightIt = std::ranges::copy(targetWeights, morphTargetWeightIt).out;
                morphTargetWeightBufferAddress += sizeof(float) * targetWeights.size();

                // Skinning data.
                if (config.skinBuffer && node.skinIndex) {
                    shaderNode.pSkinJointIndexBuffer = skinJointIndexBufferAddress + config.skinBuffer->getJointIndicesOffsetAndSize(*node.skinIndex).first;
                    shaderNode.pInverseBindMatrixBuffer = inverseBindMatrixBufferAddress + config.skinBuffer->getInverseBindMatricesOffsetAndSize(*node.skinIndex).first;
                }

                ++nodeIndex;
            }

            if (!isHostCoherent) {
                allocator.flushAllocation(allocation, 0, vk::WholeSize);
            }
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

fastgltf::math::fmat4x4 & vkgltf::NodeBuffer::getWorldTransform(std::size_t nodeIndex) noexcept {
    return nodes[nodeIndex].worldTransform;
}

const fastgltf::math::fmat4x4 & vkgltf::NodeBuffer::getWorldTransform(std::size_t nodeIndex) const noexcept {
    return nodes[nodeIndex].worldTransform;
}

std::pair<vk::DeviceSize, vk::DeviceSize> vkgltf::NodeBuffer::getWorldTransformOffsetAndSize(
    std::size_t nodeIndex
) const noexcept {
    return {
        sizeof(shader_type::Node) * nodeIndex + offsetof(shader_type::Node, worldTransform),
        sizeof(shader_type::Node::worldTransform),
    };
}

std::span<fastgltf::math::fmat4x4> vkgltf::NodeBuffer::getInstanceWorldTransforms(std::size_t nodeIndex) noexcept {
    return instanceWorldTransformsByNode[nodeIndex];
}

std::span<const fastgltf::math::fmat4x4> vkgltf::NodeBuffer::getInstanceWorldTransforms(std::size_t nodeIndex) const noexcept {
    return instanceWorldTransformsByNode[nodeIndex];
}

std::pair<vk::DeviceSize, vk::DeviceSize> vkgltf::NodeBuffer::getInstanceWorldTransformsOffsetAndSize(
    std::size_t nodeIndex
) const noexcept {
    return {
        reinterpret_cast<const std::byte*>(instanceWorldTransformsByNode[nodeIndex].data()) - reinterpret_cast<const std::byte*>(nodes.data()),
        instanceWorldTransformsByNode[nodeIndex].size_bytes(),
    };
}

std::span<float> vkgltf::NodeBuffer::getMorphTargetWeights(std::size_t nodeIndex) noexcept {
    return morphTargetWeightsByNode[nodeIndex];
}

std::span<const float> vkgltf::NodeBuffer::getMorphTargetWeights(std::size_t nodeIndex) const noexcept {
    return morphTargetWeightsByNode[nodeIndex];
}

std::pair<vk::DeviceSize, vk::DeviceSize> vkgltf::NodeBuffer::getTargetWeightsOffsetAndSize(
    std::size_t nodeIndex
) const noexcept {
    return {
        reinterpret_cast<const std::byte*>(morphTargetWeightsByNode[nodeIndex].data()) - reinterpret_cast<const std::byte*>(nodes.data()),
        morphTargetWeightsByNode[nodeIndex].size_bytes(),
    };
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
        morphTargetWeightCount += getTargetWeightCount(asset, node);
    }

    morphTargetWeightDataByteOffset = bufferSize;
    bufferSize = morphTargetWeightDataByteOffset + sizeof(float) * morphTargetWeightCount;
}