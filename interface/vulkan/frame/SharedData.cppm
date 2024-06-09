module;

#include <compare>
#include <vector>

#include <fastgltf/core.hpp>
#include <shaderc/shaderc.hpp>

export module vk_gltf_viewer:vulkan.frame.SharedData;

export import vku;
import :gltf;
export import :vulkan.Gpu;
export import :vulkan.pipelines;

namespace vk_gltf_viewer::vulkan {
    export class SharedData {
    public:
		// CPU resources.
    	fastgltf::Expected<fastgltf::GltfDataBuffer> gltfDataBufferExpected = loadGltfDataBuffer(std::getenv("GLTF_PATH"));
    	fastgltf::Expected<fastgltf::Asset> assetExpected = loadAsset(std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path());
		gltf::AssetResources assetResources;
    	gltf::SceneResources sceneResources;

    	// Swapchain.
		vk::raii::SwapchainKHR swapchain;
		vk::Extent2D swapchainExtent;
		std::vector<vk::Image> swapchainImages = swapchain.getImages();

		// Pipelines.
		MeshRenderer meshRenderer;

    	// Attachment groups.
    	std::vector<vku::AttachmentGroup> swapchainAttachmentGroups;

    	// Descriptor/command pools.
    	vk::raii::CommandPool graphicsCommandPool;

    	SharedData(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent, const shaderc::Compiler &compiler = {});

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto loadGltfDataBuffer(const std::filesystem::path &path) const -> decltype(gltfDataBufferExpected);
    	[[nodiscard]] auto loadAsset(const std::filesystem::path &parentPath) -> decltype(assetExpected);
    	[[nodiscard]] auto createSwapchain(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createSwapchainAttachmentGroups(const vk::raii::Device &device) const -> decltype(swapchainAttachmentGroups);
    	[[nodiscard]] auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;

    	auto acquireResourceQueueFamilyOwnership(const Gpu::QueueFamilies &queueFamilies, vk::CommandBuffer commandBuffer) const -> void;
    	auto generateAssetResourceMipmaps(vk::CommandBuffer commandBuffer) const -> void;
    	auto initAttachmentLayouts(vk::CommandBuffer commandBuffer) const -> void;
    };
}
