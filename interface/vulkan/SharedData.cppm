module;

#include <fastgltf/types.hpp>
#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.SharedData;

import std;
export import vku;
import :gltf.AssetResources;
import :gltf.SceneResources;
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

// TODO: this should not be in here... use proper namespace.
struct ImageBasedLightingResources {
	vku::AllocatedImage cubemapImage; vk::raii::ImageView cubemapImageView;
	vku::MappedBuffer cubemapSphericalHarmonicsBuffer;
	vku::AllocatedImage prefilteredmapImage; vk::raii::ImageView prefilteredmapImageView;
};

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
    public:
		// CPU resources.
    	const fastgltf::Asset &asset;
    	shaderc::Compiler compiler;

		const Gpu &gpu;

		gltf::AssetResources assetResources;
    	gltf::SceneResources sceneResources { assetResources, asset.scenes[asset.defaultScene.value_or(0)], gpu };

    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

    	// Buffer, image and image views.
    	vku::AllocatedImage gltfFallbackImage = createGltfFallbackImage();
    	vk::raii::ImageView gltfFallbackImageView { gpu.device, gltfFallbackImage.getViewCreateInfo() };
    	vku::AllocatedImage brdfmapImage = createBrdfmapImage();
    	vk::raii::ImageView brdfmapImageView { gpu.device, brdfmapImage.getViewCreateInfo() };
    	std::optional<ImageBasedLightingResources> imageBasedLightingResources = std::nullopt;
    	buffer::CubeIndices cubeIndices { gpu.allocator };

		// Pipelines.
		pipeline::AlphaMaskedDepthRenderer alphaMaskedDepthRenderer { gpu.device, static_cast<std::uint32_t>(assetResources.textures.size()) };
		pipeline::DepthRenderer depthRenderer { gpu.device };
		pipeline::JumpFloodComputer jumpFloodComputer { gpu.device };
		pipeline::OutlineRenderer outlineRenderer { gpu.device };
		pipeline::PrimitiveRenderer primitiveRenderer { gpu.device, static_cast<std::uint32_t>(assetResources.textures.size()) };
    	pipeline::AlphaMaskedPrimitiveRenderer alphaMaskedPrimitiveRenderer { gpu.device, *primitiveRenderer.pipelineLayout };
    	pipeline::Rec709Renderer rec709Renderer { gpu.device };
		pipeline::SkyboxRenderer skyboxRenderer { gpu, cubeIndices };
		pipeline::SphericalHarmonicsRenderer sphericalHarmonicsRenderer { gpu, cubeIndices };

    	// Attachment groups.
    	std::vector<SwapchainAttachmentGroup> swapchainAttachmentGroups = createSwapchainAttachmentGroups();
    	std::vector<vku::AttachmentGroup> imGuiSwapchainAttachmentGroups = createImGuiSwapchainAttachmentGroups();

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool = createCommandPool(gpu.queueFamilies.graphicsPresent);

    	SharedData(const fastgltf::Asset &asset [[clang::lifetimebound]], const std::filesystem::path &assetDir, const Gpu &gpu [[clang::lifetimebound]], vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent, const vku::Image &eqmapImage);

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createGltfFallbackImage() const -> decltype(gltfFallbackImage);
    	[[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);
    	[[nodiscard]] auto createSwapchainAttachmentGroups() const -> decltype(swapchainAttachmentGroups);
    	[[nodiscard]] auto createImGuiSwapchainAttachmentGroups() const -> decltype(imGuiSwapchainAttachmentGroups);
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto recordGltfFallbackImageClearCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    	auto recordImageMipmapGenerationCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    	auto recordInitialImageLayoutTransitionCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
    };
}
