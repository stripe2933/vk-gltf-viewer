module;

#include <cassert>

#include <boost/container/static_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer:vulkan.Frame;

import std;
export import :gltf.OrderedPrimitives;
import :helpers.optional;
import :math.extended_arithmetic;
export import :math.Frustum;
import :vulkan.ag.MousePicking;
import :vulkan.ag.JumpFloodSeed;
import :vulkan.ag.SceneOpaque;
import :vulkan.ag.SceneWeightedBlended;
import :vulkan.buffer.IndirectDrawCommands;
import :vulkan.buffer.InstancedNodeWorldTransforms;
import :vulkan.buffer.MorphTargetWeights;
import :vulkan.buffer.Nodes;
export import :vulkan.SharedData;

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)
#define LIFT(...) [](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

/**
 * @brief A type that represents the state for a single multi-draw-indirect call.
 *
 * Multi-draw-indirect call execute multiple draw calls with single state. Therefore, if there are many draw calls whose
 * PSOs are different, they need to be grouped by each draw call's required state, and the grouped call can be executed
 * with a single multi-draw-indirect call.
 *
 * This type will be used for the key of associated grouped draw calls.
 */
struct CommandSeparationCriteria {
    std::uint32_t subpass;
    vk::Pipeline pipeline;
    std::optional<vk::IndexType> indexType;
    vk::PrimitiveTopology primitiveTopology;
    std::optional<std::uint32_t> stencilReference;
    vk::CullModeFlagBits cullMode;

    [[nodiscard]] std::strong_ordering operator<=>(const CommandSeparationCriteria&) const noexcept = default;
};

template <>
struct std::less<CommandSeparationCriteria> {
    using is_transparent = void;

    [[nodiscard]] bool operator()(const CommandSeparationCriteria &lhs, const CommandSeparationCriteria &rhs) const noexcept { return lhs < rhs; }
    [[nodiscard]] bool operator()(const CommandSeparationCriteria &lhs, std::uint32_t rhs) const noexcept { return lhs.subpass < rhs; }
    [[nodiscard]] bool operator()(std::uint32_t lhs, const CommandSeparationCriteria &rhs) const noexcept { return lhs < rhs.subpass; }
};

struct CommandSeparationCriteriaNoShading {
    vk::Pipeline pipeline;
    std::optional<vk::IndexType> indexType;
    vk::PrimitiveTopology primitiveTopology;
    vk::CullModeFlagBits cullMode;

    [[nodiscard]] std::strong_ordering operator<=>(const CommandSeparationCriteriaNoShading&) const noexcept = default;
};

namespace vk_gltf_viewer::vulkan {
    export class Frame {
        const SharedData &sharedData;

    public:
        struct GltfAsset {
            std::optional<buffer::InstancedNodeWorldTransforms> instancedNodeWorldTransformBuffer;
            buffer::Nodes nodeBuffer;
            std::optional<buffer::MorphTargetWeights> morphTargetWeightBuffer;

            vku::MappedBuffer mousePickingResultBuffer;

            // Used only if GPU does not support variable descriptor count.
            std::optional<vk::raii::DescriptorPool> descriptorPool;

            template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
            GltfAsset(
                const fastgltf::Asset &asset LIFETIMEBOUND,
                std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
                const SharedData &sharedData LIFETIMEBOUND,
                const BufferDataAdapter &adapter = {}
            ) : instancedNodeWorldTransformBuffer { value_if(sharedData.gltfAsset->nodeInstanceCountExclusiveScanWithCount.back() != 0, [&]() {
                    return buffer::InstancedNodeWorldTransforms {
                        sharedData.gpu.allocator,
                        asset,
                        asset.scenes[asset.defaultScene.value_or(0)],
                        sharedData.gltfAsset->nodeInstanceCountExclusiveScanWithCount,
                        nodeWorldTransforms,
                        adapter,
                    };
                }) },
                nodeBuffer {
                    sharedData.gpu.device,
                    sharedData.gpu.allocator,
                    asset,
                    nodeWorldTransforms,
                    sharedData.gltfAsset->targetWeightCountExclusiveScanWithCount,
                    sharedData.gltfAsset->skinJointCountExclusiveScanWithCount,
                    instancedNodeWorldTransformBuffer.transform(LIFT(std::addressof)).value_or(nullptr),
                },
                morphTargetWeightBuffer { value_if(sharedData.gltfAsset->targetWeightCountExclusiveScanWithCount.back() != 0, [&]() {
                    return buffer::MorphTargetWeights { asset, sharedData.gltfAsset->targetWeightCountExclusiveScanWithCount, sharedData.gpu };
                }) },
                mousePickingResultBuffer { sharedData.gpu.allocator, vk::BufferCreateInfo {
                    {},
                    sizeof(std::uint32_t) * math::divCeil<std::uint32_t>(asset.nodes.size(), 32U),
                    vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer,
                }, vku::allocation::hostRead },
                descriptorPool { value_if(!sharedData.gpu.supportVariableDescriptorCount, [&]() {
                    return vk::raii::DescriptorPool {
                        sharedData.gpu.device,
                        sharedData.assetDescriptorSetLayout.getPoolSize()
                            .getDescriptorPoolCreateInfo(vk::DescriptorPoolCreateFlagBits::eUpdateAfterBind),
                    };
                }) } { }
        };

        struct ExecutionTask {
            struct Gltf {
                struct HoveringNode {
                    std::size_t index;
                    glm::vec4 outlineColor;
                    float outlineThickness;
                };

                struct SelectedNodes {
                    const std::unordered_set<std::size_t>& indices;
                    glm::vec4 outlineColor;
                    float outlineThickness;
                };

                const fastgltf::Asset &asset;
                const gltf::OrderedPrimitives &orderedPrimitives;
                std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms;

                bool regenerateDrawCommands;
                const std::vector<bool> &nodeVisibilities;
                std::optional<HoveringNode> hoveringNode;
                std::optional<SelectedNodes> selectedNodes;
            };

            vk::Rect2D passthruRect;

            /**
             * @brief Camera matrices.
             */
            struct { glm::mat4 view, projection; } camera;

            /**
             * @brief The frustum of the camera, which would be used for frustum culling.
             *
             * If <tt>std::nullopt</tt>, frustum culling would be disabled.
             */
            std::optional<math::Frustum> frustum;

            /**
             * @brief Cursor position or selection rectangle for handling mouse picking.
             *
             * - If mouse picking has to be done inside the selection rectangle, passthrough rectangle aligned, framebuffer-scale <tt>vk::Rect2D</tt> used.
             * - If mouse picking has to be done under the current cursor, passthrough rectangle aligned <tt>vk::Offset2D</tt> used.
             * - Otherwise, <tt>std::monostate</tt> used.
             */
            std::variant<std::monostate, vk::Offset2D, vk::Rect2D> mousePickingInput;

            /**
             * @brief Information of glTF to be rendered. <tt>std::nullopt</tt> if no glTF scene to be rendered.
             */
            std::optional<Gltf> gltf;
            std::optional<glm::vec3> solidBackground; // If this is nullopt, use SharedData::SkyboxDescriptorSet instead.
        };

        struct UpdateResult {
            /**
             * @brief Node index of the current pointing mesh. <tt>std::nullopt</tt> if there is no mesh under the cursor.
             */
            std::variant<std::monostate, std::size_t, std::vector<std::size_t>> mousePickingResult;
        };

        // --------------------
        // glTF asset.
        // --------------------

        std::optional<GltfAsset> gltfAsset;
        vku::DescriptorSet<dsl::Asset> assetDescriptorSet;

        explicit Frame(const SharedData &sharedData LIFETIMEBOUND);

        UpdateResult update(const ExecutionTask &task);

        void recordCommandsAndSubmit(
            std::uint32_t swapchainImageIndex,
            vk::Semaphore swapchainImageAcquireSemaphore,
            vk::Semaphore swapchainImageReadySemaphore,
            vk::Fence inFlightFence = nullptr
        ) const;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void changeAsset(
            const fastgltf::Asset &asset,
            std::span<const fastgltf::math::fmat4x4> nodeWorldTransforms,
            const BufferDataAdapter &adapter = {}
        ) {
            const auto &inner = gltfAsset.emplace(asset, nodeWorldTransforms, sharedData, adapter);
            if (sharedData.gpu.supportVariableDescriptorCount) {
                (*sharedData.gpu.device).freeDescriptorSets(*descriptorPool, assetDescriptorSet);
                assetDescriptorSet = decltype(assetDescriptorSet) {
                    vku::unsafe,
                    (*sharedData.gpu.device).allocateDescriptorSets(vk::StructureChain {
                         vk::DescriptorSetAllocateInfo {
                             *descriptorPool,
                             *sharedData.assetDescriptorSetLayout,
                         },
                         vk::DescriptorSetVariableDescriptorCountAllocateInfo {
                             vku::unsafeProxy<std::uint32_t>(asset.textures.size() + 1),
                         },
                     }.get())[0],
                };
            }
            else {
                std::tie(assetDescriptorSet) = vku::allocateDescriptorSets(*inner.descriptorPool.value(), std::tie(sharedData.assetDescriptorSetLayout));
            }

            const vk::DescriptorBufferInfo mousePickingResultBufferDescriptorInfo{ inner.mousePickingResultBuffer, 0, sizeof(std::uint32_t) };
            const vk::DescriptorBufferInfo multiNodeMousePickingResultBufferDescriptorInfo{ inner.mousePickingResultBuffer, 0, vk::WholeSize };

            std::vector<vk::DescriptorImageInfo> imageInfos;
            imageInfos.reserve(asset.textures.size() + 1);
            imageInfos.emplace_back(*sharedData.fallbackTexture.sampler, *sharedData.fallbackTexture.imageView, vk::ImageLayout::eShaderReadOnlyOptimal);
            imageInfos.append_range(sharedData.gltfAsset->textures.descriptorInfos);

            boost::container::static_vector<vk::WriteDescriptorSet, 2 + dsl::Asset::bindingCount> descriptorWrites {
                mousePickingSet.getWrite<1>(mousePickingResultBufferDescriptorInfo),
                multiNodeMousePickingSet.getWrite<0>(multiNodeMousePickingResultBufferDescriptorInfo),
                assetDescriptorSet.getWrite<0>(sharedData.gltfAsset->primitiveBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<1>(inner.nodeBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<5>(sharedData.gltfAsset->materialBuffer.getDescriptorInfo()),
                assetDescriptorSet.getWrite<6>(imageInfos),
            };
            if (inner.morphTargetWeightBuffer) {
                descriptorWrites.push_back(assetDescriptorSet.getWrite<2>(inner.morphTargetWeightBuffer->getDescriptorInfo()));
            }
            if (sharedData.gltfAsset->skinJointIndexAndInverseBindMatrixBuffer) {
                const auto &[skinJointIndexBuffer, inverseBindMatrixBuffer] = *sharedData.gltfAsset->skinJointIndexAndInverseBindMatrixBuffer;
                descriptorWrites.push_back(assetDescriptorSet.getWrite<3>(skinJointIndexBuffer.getDescriptorInfo()));
                descriptorWrites.push_back(assetDescriptorSet.getWrite<4>(inverseBindMatrixBuffer.getDescriptorInfo()));
            }

            sharedData.gpu.device.updateDescriptorSets(descriptorWrites, {});
        }

    private:
        struct PassthruResources {
            struct JumpFloodResources {
                vku::AllocatedImage image;
                vk::raii::ImageView imageView;
                vk::raii::ImageView pingImageView;
                vk::raii::ImageView pongImageView;

                JumpFloodResources(const Gpu &gpu LIFETIMEBOUND, const vk::Extent2D &extent);
            };

            vk::Extent2D extent;

            // Mouse picking.
            ag::MousePicking mousePickingAttachmentGroup;
            vk::raii::Framebuffer mousePickingFramebuffer;

            // Outline calculation using JFA.
            JumpFloodResources hoveringNodeOutlineJumpFloodResources;
            ag::JumpFloodSeed hoveringNodeJumpFloodSeedAttachmentGroup;
            JumpFloodResources selectedNodeOutlineJumpFloodResources;
            ag::JumpFloodSeed selectedNodeJumpFloodSeedAttachmentGroup;

            // Scene rendering.
            ag::SceneOpaque sceneOpaqueAttachmentGroup;
            ag::SceneWeightedBlended sceneWeightedBlendedAttachmentGroup;

            // Bloom.
            vku::AllocatedImage bloomImage;
            vk::raii::ImageView bloomImageView;
            std::vector<vk::raii::ImageView> bloomMipImageViews;

            // Framebuffers.
            vk::raii::Framebuffer sceneFramebuffer;
            vk::raii::Framebuffer bloomApplyFramebuffer;

            PassthruResources(const SharedData &sharedData LIFETIMEBOUND, const vk::Extent2D &extent, vk::CommandBuffer graphicsCommandBuffer);
        };

        struct RenderingNodes {
            std::map<CommandSeparationCriteria, buffer::IndirectDrawCommands> indirectDrawCommandBuffers;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> mousePickingIndirectDrawCommandBuffers;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> multiNodeMousePickingIndirectDrawCommandBuffers;
        };

        struct SelectedNodes {
            std::unordered_set<std::size_t> indices;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> jumpFloodSeedIndirectDrawCommandBuffers;
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        struct HoveringNode {
            std::size_t index;
            std::map<CommandSeparationCriteriaNoShading, buffer::IndirectDrawCommands> jumpFloodSeedIndirectDrawCommandBuffers;
            glm::vec4 outlineColor;
            float outlineThickness;
        };

        // Buffer, image and image views.
        std::optional<PassthruResources> passthruResources;

        // Descriptor/command pools.
        vk::raii::DescriptorPool descriptorPool;
        vk::raii::CommandPool computeCommandPool;
        vk::raii::CommandPool graphicsCommandPool;

        // Descriptor sets.
        vku::DescriptorSet<MousePickingRenderer::DescriptorSetLayout> mousePickingSet;
        vku::DescriptorSet<dsl::MultiNodeMousePicking> multiNodeMousePickingSet;
        vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> hoveringNodeJumpFloodSet;
        vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> selectedNodeJumpFloodSet;
        vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> hoveringNodeOutlineSet;
        vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> selectedNodeOutlineSet;
        vku::DescriptorSet<WeightedBlendedCompositionRenderer::DescriptorSetLayout> weightedBlendedCompositionSet;
        vku::DescriptorSet<InverseToneMappingRenderer::DescriptorSetLayout> inverseToneMappingSet;
        vku::DescriptorSet<bloom::BloomComputer::DescriptorSetLayout> bloomSet;
        vku::DescriptorSet<BloomApplyRenderer::DescriptorSetLayout> bloomApplySet;

        // Command buffers.
        vk::CommandBuffer scenePrepassCommandBuffer;
        vk::CommandBuffer sceneRenderingCommandBuffer;
        vk::CommandBuffer compositionCommandBuffer;
        vk::CommandBuffer jumpFloodCommandBuffer;

        // Synchronization stuffs.
        vk::raii::Semaphore scenePrepassFinishSema;
        vk::raii::Semaphore sceneRenderingFinishSema;
        vk::raii::Semaphore jumpFloodFinishSema;

        vk::Offset2D passthruOffset;
        glm::mat4 projectionViewMatrix;
        glm::vec3 viewPosition;
        glm::mat4 translationlessProjectionViewMatrix;
        std::variant<std::monostate, vk::Offset2D, vk::Rect2D> mousePickingInput;
        std::optional<RenderingNodes> renderingNodes;
        std::optional<SelectedNodes> selectedNodes;
        std::optional<HoveringNode> hoveringNode;
        std::variant<vku::DescriptorSet<dsl::Skybox>, glm::vec3> background;

        [[nodiscard]] vk::raii::DescriptorPool createDescriptorPool() const;

        void recordScenePrepassCommands(vk::CommandBuffer cb) const;
        // Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
        [[nodiscard]] bool recordJumpFloodComputeCommands(vk::CommandBuffer cb, const vku::Image &image, vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSet, std::uint32_t initialSampleOffset) const;
        void recordSceneOpaqueMeshDrawCommands(vk::CommandBuffer cb) const;
        bool recordSceneBlendMeshDrawCommands(vk::CommandBuffer cb) const;
        void recordSkyboxDrawCommands(vk::CommandBuffer cb) const;
        void recordNodeOutlineCompositionCommands(vk::CommandBuffer cb, std::optional<bool> hoveringNodeJumpFloodForward, std::optional<bool> selectedNodeJumpFloodForward) const;
        void recordImGuiCompositionCommands(vk::CommandBuffer cb, std::uint32_t swapchainImageIndex) const;
    };
}