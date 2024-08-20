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
		struct GltfAsset {
			struct DataBufferLoader {
				fastgltf::GltfDataBuffer dataBuffer;

				explicit DataBufferLoader(const std::filesystem::path &path);
			};

			DataBufferLoader dataBufferLoader;
			fastgltf::Expected<fastgltf::Asset> assetExpected;

			explicit GltfAsset(const std::filesystem::path &path);

			[[nodiscard]] auto get() noexcept -> fastgltf::Asset&;
		};
		
		struct SkyboxResources {
			vku::AllocatedImage reducedEqmapImage;
			vk::raii::ImageView reducedEqmapImageView;
			vku::AllocatedImage cubemapImage;
			vk::raii::ImageView cubemapImageView;
			vk::DescriptorSet imGuiEqmapTextureDescriptorSet;
			vku::DescriptorSet<vulkan::dsl::Skybox> descriptorSet;
		};

		struct ImageBasedLightingResources {
			vku::AllocatedBuffer cubemapSphericalHarmonicsBuffer;
			vku::AllocatedImage prefilteredmapImage;
			vk::raii::ImageView prefilteredmapImageView;
		};

	    AppState appState;

		GltfAsset gltfAsset { std::getenv("GLTF_PATH") };

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		control::AppWindow window { instance, appState };
		vulkan::Gpu gpu { instance, window.getSurface() };

		gltf::AssetResources assetResources { gltfAsset.get(), std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path(), gpu, { .supportUint8Index = false /* TODO: change this value depend on vk::PhysicalDeviceIndexTypeUint8FeaturesKHR */ } };
    	gltf::SceneResources sceneResources { assetResources, gltfAsset.get().scenes[gltfAsset.get().defaultScene.value_or(0)], gpu };
		ImageBasedLightingResources imageBasedLightingResources = createDefaultImageBasedLightingResources();
		std::optional<SkyboxResources> skyboxResources{};

		// Buffers, images, image views and samplers.
		vku::AllocatedImage brdfmapImage = createBrdfmapImage();
		vk::raii::ImageView brdfmapImageView { gpu.device, brdfmapImage.getViewCreateInfo() };
		vk::raii::Sampler reducedEqmapSampler = createEqmapSampler();
		vulkan::CubemapSampler cubemapSampler { gpu.device };
		vulkan::BrdfLutSampler brdfLutSampler { gpu.device };

		// Descriptor set layouts.
		vulkan::dsl::Asset assetDescriptorSetLayout { gpu.device, static_cast<std::uint32_t>(1 /*fallback texture*/ + gltfAsset.get().textures.size()) };
		vulkan::dsl::ImageBasedLighting imageBasedLightingDescriptorSetLayout { gpu.device, cubemapSampler, brdfLutSampler };
		vulkan::dsl::Scene sceneDescriptorSetLayout { gpu.device };
		vulkan::dsl::Skybox skyboxDescriptorSetLayout { gpu.device, cubemapSampler };

		// Descriptor/command pools.
    	vk::raii::DescriptorPool descriptorPool = createDescriptorPool();
    	vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();
		vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
		vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
		vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

		// Descriptor sets.
		vku::DescriptorSet<vulkan::dsl::ImageBasedLighting> imageBasedLightingDescriptorSet;
		std::vector<vk::DescriptorSet> assetTextureDescriptorSets;
		
		[[nodiscard]] auto createInstance() const -> decltype(instance);
		[[nodiscard]] auto createDefaultImageBasedLightingResources() const -> ImageBasedLightingResources;
		[[nodiscard]] auto createEqmapSampler() const -> vk::raii::Sampler;
    	[[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);
    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);
    	[[nodiscard]] auto createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool);

		auto initializeImageBasedLightingResourcesByDefault(vk::CommandBuffer graphicsCommandBuffer) const -> void;
		auto processEqmapChange(const std::filesystem::path &eqmapPath) -> void;
	};
}