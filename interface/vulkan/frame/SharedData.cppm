module;

#include <compare>
#include <vector>

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.frame.SharedData;

export import vku;
import :gltf.AssetResources;
import :gltf.SceneResources;
import :io.ktxvk;
export import :vulkan.Gpu;
export import :vulkan.pipelines.DepthRenderer;
export import :vulkan.pipelines.MeshRenderer;
export import :vulkan.pipelines.SkyboxRenderer;

namespace vk_gltf_viewer::vulkan::inline frame {
    export class SharedData {
    public:
		// CPU resources.
    	const fastgltf::Asset &asset;
		gltf::AssetResources assetResources;
    	gltf::SceneResources sceneResources;

    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

		// Pipelines.
		pipelines::MeshRenderer meshRenderer;
		pipelines::DepthRenderer depthRenderer;
		pipelines::SkyboxRenderer skyboxRenderer;

    	// Attachment groups.
    	std::vector<vku::AttachmentGroup> swapchainAttachmentGroups;

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool, transferCommandPool;

    	// Buffer, image and image views.
    	io::ktxvk::DeviceInfo deviceInfo;
    	io::ktxvk::Texture cubemapTexture, prefilteredmapTexture;
    	vk::raii::ImageView cubemapImageView, prefilteredmapImageView;
    	vku::MappedBuffer cubemapSphericalHarmonicsBuffer;
    	vku::AllocatedImage brdfmapImage;
    	vk::raii::ImageView brdfmapImageView;

    	SharedData(const fastgltf::Asset &asset, const std::filesystem::path &assetDir, const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent, const shaderc::Compiler &compiler = {});

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createSwapchainAttachmentGroups(const vk::raii::Device &device) const -> decltype(swapchainAttachmentGroups);

    	auto generateAssetResourceMipmaps(vk::CommandBuffer commandBuffer) const -> void;
    	auto initAttachmentLayouts(vk::CommandBuffer commandBuffer) const -> void;
    };
}
