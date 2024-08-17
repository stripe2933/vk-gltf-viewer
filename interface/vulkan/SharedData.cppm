module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import vku;
export import :vulkan.ag.ImGuiSwapchain;
export import :vulkan.ag.Swapchain;
export import :vulkan.Gpu;
export import :vulkan.pipeline.AlphaMaskedDepthRenderer;
export import :vulkan.pipeline.AlphaMaskedFacetedPrimitiveRenderer;
export import :vulkan.pipeline.AlphaMaskedJumpFloodSeedRenderer;
export import :vulkan.pipeline.AlphaMaskedPrimitiveRenderer;
export import :vulkan.pipeline.DepthRenderer;
export import :vulkan.pipeline.FacetedPrimitiveRenderer;
export import :vulkan.pipeline.JumpFloodComputer;
export import :vulkan.pipeline.JumpFloodSeedRenderer;
export import :vulkan.pipeline.OutlineRenderer;
export import :vulkan.pipeline.PrimitiveRenderer;
export import :vulkan.pipeline.SkyboxRenderer;
import :vulkan.sampler.SingleTexelSampler;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
		// CPU resources.
    	const fastgltf::Asset &asset;

		const Gpu &gpu;

    public:
		struct DescriptorSetLayouts {
			const dsl::Asset &asset;
			const dsl::ImageBasedLighting &imageBasedLighting;
			const dsl::Scene &scene;
			const dsl::Skybox &skybox;
		};

    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

    	// Buffer, image and image views and samplers.
    	vku::AllocatedImage gltfFallbackImage = createGltfFallbackImage();
    	vk::raii::ImageView gltfFallbackImageView { gpu.device, gltfFallbackImage.getViewCreateInfo() };
    	buffer::CubeIndices cubeIndices { gpu.allocator };
		SingleTexelSampler singleTexelSampler { gpu.device };

    	// Pipeline layouts.
    	pl::SceneRendering sceneRenderingPipelineLayout;

		// Pipelines.
		AlphaMaskedDepthRenderer alphaMaskedDepthRenderer;
    	AlphaMaskedFacetedPrimitiveRenderer alphaMaskedFacetedPrimitiveRenderer;
    	AlphaMaskedJumpFloodSeedRenderer alphaMaskedJumpFloodSeedRenderer;
    	AlphaMaskedPrimitiveRenderer alphaMaskedPrimitiveRenderer;
		DepthRenderer depthRenderer;
		FacetedPrimitiveRenderer facetedPrimitiveRenderer;
		JumpFloodComputer jumpFloodComputer;
    	JumpFloodSeedRenderer jumpFloodSeedRenderer;
		OutlineRenderer outlineRenderer;
		PrimitiveRenderer primitiveRenderer;
		SkyboxRenderer skyboxRenderer;

    	// Attachment groups.
    	std::vector<ag::Swapchain> swapchainAttachmentGroups = createSwapchainAttachmentGroups();
    	std::vector<ag::ImGuiSwapchain> imGuiSwapchainAttachmentGroups = createImGuiSwapchainAttachmentGroups();

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool = createCommandPool(gpu.queueFamilies.graphicsPresent);

    	SharedData(
    		const fastgltf::Asset &asset [[clang::lifetimebound]],
    		const Gpu &gpu [[clang::lifetimebound]],
    		vk::SurfaceKHR surface,
    		const vk::Extent2D &swapchainExtent,
    		const DescriptorSetLayouts &descriptorSetLayouts);

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createGltfFallbackImage() const -> decltype(gltfFallbackImage);
    	[[nodiscard]] auto createSwapchainAttachmentGroups() const -> std::vector<ag::Swapchain>;
    	[[nodiscard]] auto createImGuiSwapchainAttachmentGroups() const -> std::vector<ag::ImGuiSwapchain>;
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto recordGltfFallbackImageClearCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    	auto recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    };
}
