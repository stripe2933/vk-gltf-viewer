module;

#include <compare>
#include <optional>
#include <vector>

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.frame.SharedData;

export import vku;
import :gltf.AssetResources;
import :gltf.SceneResources;
export import :vulkan.Gpu;
export import :vulkan.pipelines.DepthRenderer;
export import :vulkan.pipelines.JumpFloodComputer;
export import :vulkan.pipelines.OutlineRenderer;
export import :vulkan.pipelines.PrimitiveRenderer;
export import :vulkan.pipelines.Rec709Renderer;
export import :vulkan.pipelines.SkyboxRenderer;

// TODO: this should not be in here... use proper namespace.
struct ImageBasedLightingResources {
	vku::AllocatedImage cubemapImage; vk::raii::ImageView cubemapImageView;
	vku::MappedBuffer cubemapSphericalHarmonicsBuffer;
	vku::AllocatedImage prefilteredmapImage; vk::raii::ImageView prefilteredmapImageView;
};

namespace vk_gltf_viewer::vulkan::inline frame {
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
    	vku::AllocatedImage brdfmapImage = createBrdfmapImage();
    	vk::raii::ImageView brdfmapImageView = createBrdfmapImageView();
    	std::optional<ImageBasedLightingResources> imageBasedLightingResources = std::nullopt;

    	// Render passes.
    	vk::raii::RenderPass compositionRenderPass = createCompositionRenderPass();

		// Pipelines.
		pipelines::DepthRenderer depthRenderer { gpu.device, compiler };
		pipelines::JumpFloodComputer jumpFloodComputer { gpu.device, compiler };
		pipelines::PrimitiveRenderer primitiveRenderer { gpu.device, static_cast<std::uint32_t>(assetResources.textures.size()), compiler };
		pipelines::SkyboxRenderer skyboxRenderer { gpu, compiler };
    	pipelines::Rec709Renderer rec709Renderer { gpu.device, *compositionRenderPass, 0, compiler };
		pipelines::OutlineRenderer outlineRenderer { gpu.device, *compositionRenderPass, 1, compiler };

    	// Attachment groups.
    	std::vector<vku::AttachmentGroup> swapchainAttachmentGroups = createSwapchainAttachmentGroups();

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool = createCommandPool(gpu.queueFamilies.graphicsPresent);

    	SharedData(const fastgltf::Asset &asset, const std::filesystem::path &assetDir, const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent);

    	auto handleSwapchainResize(vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);
    	[[nodiscard]] auto createBrdfmapImageView() const -> decltype(brdfmapImageView);
    	[[nodiscard]] auto createCompositionRenderPass() const -> decltype(compositionRenderPass);
    	[[nodiscard]] auto createSwapchainAttachmentGroups() const -> decltype(swapchainAttachmentGroups);
    	[[nodiscard]] auto createCommandPool(std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto generateAssetResourceMipmaps(vk::CommandBuffer commandBuffer) const -> void;
    	auto initAttachmentLayouts(vk::CommandBuffer commandBuffer) const -> void;
    };
}
