module;

#include <fastgltf/types.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.Frame;

import std;
import type_variant;
export import vku;
export import :AppState;
export import :gltf.SceneResources;
export import :vulkan.SharedData;
import :vulkan.ag.DepthPrepass;
import :vulkan.ag.JumpFloodSeed;
import :vulkan.ag.Scene;
export import :vulkan.dsl.Asset;
export import :vulkan.dsl.Scene;
export import :vulkan.dsl.Skybox;

namespace vk_gltf_viewer::vulkan {
    export class Frame {
    public:
    	struct ExecutionTask {
    		struct Gltf {
    			const fastgltf::Asset &asset;
    			const std::unordered_map<vk::IndexType, vku::AllocatedBuffer> &indexBuffers;
    			const gltf::SceneResources &sceneResources;
    			std::optional<std::uint32_t> hoveringNodeIndex;
    			std::unordered_set<std::size_t> selectedNodeIndices;
    			std::unordered_set<std::size_t> renderingNodeIndices;
    			vku::DescriptorSet<dsl::Asset> assetDescriptorSet;
    			vku::DescriptorSet<dsl::Scene> sceneDescriptorSet;
    		};

    		vk::Rect2D passthruRect;
    		struct { glm::mat4 view, projection; } camera;
    		std::optional<vk::Offset2D> mouseCursorOffset;
    		std::optional<AppState::Outline> hoveringNodeOutline;
    		std::optional<AppState::Outline> selectedNodeOutline;
    		std::optional<Gltf> gltf;
    		vku::DescriptorSet<dsl::ImageBasedLighting> imageBasedLightingDescriptorSet;
    		std::variant<glm::vec3 /*solid color*/, vku::DescriptorSet<dsl::Skybox>> background;
    		std::optional<std::pair<vk::SurfaceKHR, vk::Extent2D>> swapchainResizeHandleInfo;
    	};

		struct ExecutionResult {
			std::optional<std::uint32_t> hoveringNodeIndex;
			bool presentSuccess;
		};

    	enum class ExecutionError {
    		SwapchainAcquireFailed,
    	};

    	Frame(const Gpu &gpu [[clang::lifetimebound]], const SharedData &sharedData [[clang::lifetimebound]]);

    	[[nodiscard]] auto execute(const ExecutionTask &task) -> std::expected<ExecutionResult, ExecutionError>;

    private:
        struct CommandSeparationCriteria {
            fastgltf::AlphaMode alphaMode;
        	bool faceted;
            bool doubleSided;
            std::optional<vk::IndexType> indexType;

            [[nodiscard]] constexpr auto operator<=>(const CommandSeparationCriteria&) const noexcept -> std::strong_ordering = default;
        };

		struct CommandSeparationCriteriaComparator {
			using is_transparent = void;

			[[nodiscard]] auto operator()(const CommandSeparationCriteria &lhs, const CommandSeparationCriteria &rhs) const noexcept -> bool { return lhs < rhs; }
			[[nodiscard]] auto operator()(const CommandSeparationCriteria &lhs, fastgltf::AlphaMode rhs) const noexcept -> bool { return lhs.alphaMode < rhs; }
			[[nodiscard]] auto operator()(fastgltf::AlphaMode lhs, const CommandSeparationCriteria &rhs) const noexcept -> bool { return lhs < rhs.alphaMode; }
		};

    	class PassthruResources {
    	public:
    		struct JumpFloodResources {
    			vku::AllocatedImage image;
    			vk::raii::ImageView pingImageView, pongImageView;

    			JumpFloodResources(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent);
    		};

    		JumpFloodResources hoveringNodeOutlineJumpFloodResources;
    		JumpFloodResources selectedNodeOutlineJumpFloodResources;

    		// Attachment groups.
    		ag::DepthPrepass depthPrepassAttachmentGroup;
    		ag::JumpFloodSeed hoveringNodeJumpFloodSeedAttachmentGroup;
    		ag::JumpFloodSeed selectedNodeJumpFloodSeedAttachmentGroup;

    		PassthruResources(const Gpu &gpu [[clang::lifetimebound]], const vk::Extent2D &extent, vk::CommandBuffer graphicsCommandBuffer);

    	private:
    		auto recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    	};

    	struct ResourceBindingState {
    		type_variant<std::monostate, DepthRenderer, AlphaMaskedDepthRenderer, JumpFloodSeedRenderer, AlphaMaskedJumpFloodSeedRenderer> boundPipeline{};
    		std::optional<vk::CullModeFlagBits> cullMode{};
    		std::optional<vk::IndexType> indexBuffer;

    		// DepthRenderer, AlphaMaskedDepthRenderer, JumpFloodSeedRenderer and AlphaMaskedJumpFloodSeedRenderer have:
    		// - compatible scene descriptor set in set #0,
    		// - compatible asset descriptor set in set #1 (AlphaMaskedDepthRenderer and AlphaMaskedJumpFloodSeedRenderer only),
    		// - compatible push constant range.
    		bool sceneDescriptorSetBound = false;
    		bool assetDescriptorSetBound = false;
    		bool pushConstantBound = false;
    	};

    	const Gpu &gpu;
    	const SharedData &sharedData;

    	// Buffer, image and image views.
    	std::unordered_set<std::size_t> renderingNodeIndices;
    	std::map<CommandSeparationCriteria, vku::MappedBuffer, CommandSeparationCriteriaComparator> renderingNodeIndirectDrawCommandBuffers; /// Draw commands for rendering nodes (in both depth prepass and main pass)
    	std::optional<std::uint32_t> hoveringNodeIndex;
    	std::map<CommandSeparationCriteria, vku::MappedBuffer, CommandSeparationCriteriaComparator> hoveringNodeIndirectDrawCommandBuffers; /// Depth prepass draw commands for hovering nodes
    	std::unordered_set<std::size_t> selectedNodeIndices;
    	std::map<CommandSeparationCriteria, vku::MappedBuffer, CommandSeparationCriteriaComparator> selectedNodeIndirectDrawCommandBuffers; /// Depth prepass draw commands for selected nodes
    	vku::MappedBuffer hoveringNodeIndexBuffer;
    	std::optional<vk::Extent2D> passthruExtent = std::nullopt;
		std::optional<PassthruResources> passthruResources = std::nullopt;
    	vku::AllocatedImage sceneMsaaImage = createSceneMsaaImage();
    	vku::AllocatedImage sceneDepthImage = createSceneDepthImage();

    	// Attachment groups.
    	std::vector<ag::Scene> sceneAttachmentGroups = createSceneAttachmentGroups();

        // Descriptor/command pools.
    	vk::raii::DescriptorPool descriptorPool = createDescriptorPool();
        vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
    	vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

    	// Descriptor sets.
    	vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> hoveringNodeJumpFloodSets;
		vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> selectedNodeJumpFloodSets;
    	vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> hoveringNodeOutlineSets;
    	vku::DescriptorSet<OutlineRenderer::DescriptorSetLayout> selectedNodeOutlineSets;

    	// Command buffers.
    	vk::CommandBuffer scenePrepassCommandBuffer;
    	vk::CommandBuffer sceneRenderingCommandBuffer;
    	vk::CommandBuffer compositionCommandBuffer;
    	vk::CommandBuffer jumpFloodCommandBuffer;

		// Synchronization stuffs.
		vk::raii::Semaphore depthPrepassFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore swapchainImageAcquireSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore drawFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore compositeFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
    	vk::raii::Semaphore jumpFloodFinishSema { gpu.device, vk::SemaphoreCreateInfo{} };
		vk::raii::Fence inFlightFence { gpu.device, vk::FenceCreateInfo { vk::FenceCreateFlagBits::eSignaled } };

    	[[nodiscard]] auto createSceneMsaaImage() const -> vku::AllocatedImage;
    	[[nodiscard]] auto createSceneDepthImage() const -> vku::AllocatedImage;
    	[[nodiscard]] auto createSceneAttachmentGroups() const -> std::vector<ag::Scene>;
    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;
    	auto update(const ExecutionTask &task, ExecutionResult &result) -> void;

    	auto recordScenePrepassCommands(vk::CommandBuffer cb, const ExecutionTask &task) const -> void;
    	// Return true if last jump flood calculation direction is forward (result is in pong image), false if backward.
		[[nodiscard]] auto recordJumpFloodComputeCommands(vk::CommandBuffer cb, const vku::Image &image, vku::DescriptorSet<JumpFloodComputer::DescriptorSetLayout> descriptorSets, std::uint32_t initialSampleOffset) const -> bool;
    	auto recordSceneDrawCommands(vk::CommandBuffer cb, const ExecutionTask &task) const -> void;
    	auto recordSkyboxDrawCommands(vk::CommandBuffer cb, const ExecutionTask &task) const -> void;
    	auto recordNodeOutlineCompositionCommands(vk::CommandBuffer cb, std::optional<bool> hoveringNodeJumpFloodForward, std::optional<bool> selectedNodeJumpFloodForward, std::uint32_t swapchainImageIndex, const ExecutionTask &task) const -> void;
    	auto recordImGuiCompositionCommands(vk::CommandBuffer cb, std::uint32_t swapchainImageIndex) const -> void;
    };
}