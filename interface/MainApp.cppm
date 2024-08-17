module;

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

import std;
import vku;
import :control.AppWindow;
import :gltf.AssetResources;
import :gltf.SceneResources;
import :vulkan.dsl.Asset;
import :vulkan.dsl.ImageBasedLighting;
import :vulkan.dsl.Scene;
import :vulkan.dsl.Skybox;
import :vulkan.Gpu;

namespace vk_gltf_viewer {
	export class MainApp {
	public:
		explicit MainApp();
		~MainApp();

		auto run() -> void;

	private:
		struct SkyboxResources {
			vku::AllocatedImage cubemapImage;
			vk::raii::ImageView cubemapImageView;
			vku::DescriptorSet<vulkan::dsl::Skybox> descriptorSet;
		};

		struct ImageBasedLightingResources {
			vku::MappedBuffer cubemapSphericalHarmonicsBuffer;
			vku::AllocatedImage prefilteredmapImage;
			vk::raii::ImageView prefilteredmapImageView;
			vku::DescriptorSet<vulkan::dsl::ImageBasedLighting> descriptorSet;
		};

	    AppState appState;

		fastgltf::GltfDataBuffer gltfDataBuffer{};
		fastgltf::Expected<fastgltf::Asset> assetExpected = loadAsset(std::getenv("GLTF_PATH"));

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		control::AppWindow window { instance, appState };
		vulkan::Gpu gpu { instance, window.getSurface() };

		gltf::AssetResources assetResources { assetExpected.get(), std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path(), gpu, { .supportUint8Index = false /* TODO: change this value depend on vk::PhysicalDeviceIndexTypeUint8FeaturesKHR */ } };
    	gltf::SceneResources sceneResources { assetResources, assetExpected->scenes[assetExpected->defaultScene.value_or(0)], gpu };
		std::optional<SkyboxResources> skyboxResources{};
		std::optional<ImageBasedLightingResources> imageBasedLightingResources{};
		vku::AllocatedImage brdfmapImage = createBrdfmapImage();
		vk::raii::ImageView brdfmapImageView { gpu.device, brdfmapImage.getViewCreateInfo() };

		// Buffers, images, image views and samplers.
		vku::AllocatedImage reducedEqmapImage = createReducedEqmapImage({ 4096, 2048 } /* TODO */);
		vk::raii::ImageView reducedEqmapImageView { gpu.device, reducedEqmapImage.getViewCreateInfo() };
		vk::raii::Sampler reducedEqmapSampler = createEqmapSampler();
		vulkan::BrdfLutSampler brdfLutSampler { gpu.device };
		vulkan::CubemapSampler cubemapSampler { gpu.device };

		// Descriptor set layouts.
		vulkan::dsl::Asset assetDescriptorSetLayout { gpu.device, static_cast<std::uint32_t>(1 /*fallback texture*/ + assetExpected.get().textures.size()) };
		vulkan::dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout { gpu.device, brdfLutSampler, cubemapSampler };
		vulkan::dsl::Scene sceneDescriptorSetLayout { gpu.device };
		vulkan::dsl::Skybox skyboxDescriptorSetLayout { gpu.device, cubemapSampler };

		// Descriptor/command pools.
    	vk::raii::DescriptorPool descriptorPool = createDescriptorPool();
    	vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();
		vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
		vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
		vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

		// Descriptor sets.
		vk::DescriptorSet eqmapImageImGuiDescriptorSet;
		std::vector<vk::DescriptorSet> assetTextureDescriptorSets;

    	[[nodiscard]] auto loadAsset(const std::filesystem::path &path) -> decltype(assetExpected);

		[[nodiscard]] auto createInstance() const -> decltype(instance);
		[[nodiscard]] auto createReducedEqmapImage(const vk::Extent2D &eqmapLastMipImageExtent) -> vku::AllocatedImage;
		[[nodiscard]] auto createEqmapSampler() const -> vk::raii::Sampler;
    	[[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);
    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);
    	[[nodiscard]] auto createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool);

		auto processEqmapChange(const std::filesystem::path &eqmapPath, bool usePreviousDescriptorSets) -> void;
	};
}