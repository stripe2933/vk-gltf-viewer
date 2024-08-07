module;

#include <fastgltf/types.hpp>

export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import vku;
import :vulkan.attachment_groups;
export import :vulkan.Gpu;
export import :vulkan.pipeline.AlphaMaskedDepthRenderer;
export import :vulkan.pipeline.AlphaMaskedPrimitiveRenderer;
export import :vulkan.pipeline.DepthRenderer;
export import :vulkan.pipeline.JumpFloodComputer;
export import :vulkan.pipeline.OutlineRenderer;
export import :vulkan.pipeline.PrimitiveRenderer;
export import :vulkan.pipeline.Rec709Renderer;
export import :vulkan.pipeline.SkyboxRenderer;
export import :vulkan.pipeline.SphericalHarmonicsRenderer;
import :vulkan.sampler.SingleTexelSampler;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
		// CPU resources.
    	const fastgltf::Asset &asset;

		const Gpu &gpu;

    public:
    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

    	// Buffer, image and image views.
    	vku::AllocatedImage gltfFallbackImage = createGltfFallbackImage();
    	vk::raii::ImageView gltfFallbackImageView { gpu.device, gltfFallbackImage.getViewCreateInfo() };
    	buffer::CubeIndices cubeIndices { gpu.allocator };

    	// Samplers.
    	BrdfLutSampler brdfLutSampler { gpu.device };
    	CubemapSampler cubemapSampler { gpu.device };
    	SingleTexelSampler singleTexelSampler { gpu.device };

    	// Descriptor set layouts.
    	dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout { gpu.device, brdfLutSampler, cubemapSampler };
    	dsl::Asset assetDescriptorSetLayout { gpu.device, static_cast<std::uint32_t>(1 /* fallback texture */ + asset.textures.size()) };
    	dsl::Scene sceneDescriptorSetLayout { gpu.device };

		// Pipelines.
		pipeline::AlphaMaskedDepthRenderer alphaMaskedDepthRenderer { gpu.device, std::tie(sceneDescriptorSetLayout, assetDescriptorSetLayout) };
		pipeline::DepthRenderer depthRenderer { gpu.device, std::tie(sceneDescriptorSetLayout) };
		pipeline::JumpFloodComputer jumpFloodComputer { gpu.device };
		pipeline::OutlineRenderer outlineRenderer { gpu.device };
		pipeline::PrimitiveRenderer primitiveRenderer { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout) };
    	pipeline::AlphaMaskedPrimitiveRenderer alphaMaskedPrimitiveRenderer { gpu.device, std::tie(imageBasedLightingDescriptorSetLayout, assetDescriptorSetLayout, sceneDescriptorSetLayout) };
    	pipeline::Rec709Renderer rec709Renderer { gpu.device };
		pipeline::SkyboxRenderer skyboxRenderer { gpu.device, cubemapSampler, cubeIndices };
		pipeline::SphericalHarmonicsRenderer sphericalHarmonicsRenderer { gpu.device, imageBasedLightingDescriptorSetLayout, cubeIndices };

    	// Attachment groups.
    	std::vector<SwapchainAttachmentGroup> swapchainAttachmentGroups = createSwapchainAttachmentGroups();
    	std::vector<vku::AttachmentGroup> imGuiSwapchainAttachmentGroups = createImGuiSwapchainAttachmentGroups();

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool = createCommandPool(gpu.queueFamilies.graphicsPresent);

    	SharedData(const fastgltf::Asset &asset [[clang::lifetimebound]], const Gpu &gpu [[clang::lifetimebound]], vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent);

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createGltfFallbackImage() const -> decltype(gltfFallbackImage);
    	[[nodiscard]] auto createSwapchainAttachmentGroups() const -> decltype(swapchainAttachmentGroups);
    	[[nodiscard]] auto createImGuiSwapchainAttachmentGroups() const -> decltype(imGuiSwapchainAttachmentGroups);
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto recordGltfFallbackImageClearCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    	auto recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    };
}
