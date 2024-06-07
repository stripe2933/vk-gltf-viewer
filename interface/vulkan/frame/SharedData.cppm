module;

#include <compare>
#include <unordered_map>
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
		fastgltf::Parser parser;
		gltf::AssetResources assetResources { "/Users/stripe2933/Downloads/glTF-Sample-Assets/Models/ABeautifulGame/glTF/ABeautifulGame.gltf", parser };
    	gltf::SceneResources sceneResources { assetResources.asset, assetResources.asset.scenes[assetResources.asset.defaultScene.value_or(0)] };

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

    	// Buffer, Image and ImageViews.
    	std::unordered_map<const fastgltf::Primitive*, std::pair<vku::MappedBuffer, vku::MappedBuffer>> primitiveBuffers;

    	SharedData(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &swapchainExtent, const shaderc::Compiler &compiler = {});

    	auto handleSwapchainResize(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &newExtent) -> void;

    private:
    	[[nodiscard]] auto createSwapchain(const Gpu &gpu, vk::SurfaceKHR surface, const vk::Extent2D &extent, vk::SwapchainKHR oldSwapchain = {}) const -> decltype(swapchain);
    	[[nodiscard]] auto createSwapchainAttachmentGroups(const vk::raii::Device &device) const -> decltype(swapchainAttachmentGroups);
    	[[nodiscard]] auto createCommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex) const -> vk::raii::CommandPool;
    	[[nodiscard]] auto createPrimitiveBuffers(const Gpu &gpu) const -> decltype(primitiveBuffers);

    	auto initAttachmentLayouts(const Gpu &gpu) const -> void;
    };
}
