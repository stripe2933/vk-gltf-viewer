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
		class GltfAsset {
		public:
			struct DataBufferLoader {
				fastgltf::GltfDataBuffer dataBuffer;

				explicit DataBufferLoader(const std::filesystem::path &path);
			};

			DataBufferLoader dataBufferLoader;
			fastgltf::Expected<fastgltf::Asset> assetExpected;
			gltf::AssetResources assetResources;
	        std::unordered_map<std::size_t, vk::raii::ImageView> imageViews;
    		gltf::SceneResources sceneResources;

			explicit GltfAsset(const std::filesystem::path &path, const vulkan::Gpu &gpu [[clang::lifetimebound]], vk::CommandPool graphicsCommandPool);

			[[nodiscard]] auto get() noexcept -> fastgltf::Asset&;

		private:
			[[nodiscard]] auto createAssetImageViews(const vk::raii::Device &device) -> std::unordered_map<std::size_t, vk::raii::ImageView>;
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

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		control::AppWindow window { instance, appState };
		vulkan::Gpu gpu { instance, window.getSurface() };

		// Command pools.
		vk::raii::CommandPool transferCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.transfer } };
		vk::raii::CommandPool computeCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.compute } };
		vk::raii::CommandPool graphicsCommandPool { gpu.device, vk::CommandPoolCreateInfo { {}, gpu.queueFamilies.graphicsPresent } };

		vku::AllocatedImage assetFallbackImage = createAssetFallbackImage();
		vk::raii::ImageView assetFallbackImageView { gpu.device, assetFallbackImage.getViewCreateInfo() };
		vk::raii::Sampler assetDefaultSampler = createAssetDefaultSampler();
		GltfAsset gltfAsset { std::getenv("GLTF_PATH"), gpu, *graphicsCommandPool };
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

		// Descriptor pools.
    	vk::raii::DescriptorPool descriptorPool = createDescriptorPool();
    	vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();

		// Descriptor sets.
		vku::DescriptorSet<vulkan::dsl::ImageBasedLighting> imageBasedLightingDescriptorSet;
		std::vector<vk::DescriptorSet> assetTextureDescriptorSets;
		
		[[nodiscard]] auto createInstance() const -> decltype(instance);
		[[nodiscard]] auto createAssetFallbackImage() const -> vku::AllocatedImage;
		[[nodiscard]] auto createAssetDefaultSampler() const -> vk::raii::Sampler;
		[[nodiscard]] auto createDefaultImageBasedLightingResources() const -> ImageBasedLightingResources;
		[[nodiscard]] auto createEqmapSampler() const -> vk::raii::Sampler;
    	[[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);
    	[[nodiscard]] auto createDescriptorPool() const -> decltype(descriptorPool);
    	[[nodiscard]] auto createImGuiDescriptorPool() -> decltype(imGuiDescriptorPool);

		auto initializeImageBasedLightingResourcesByDefault(vk::CommandBuffer graphicsCommandBuffer) const -> void;
		auto processEqmapChange(const std::filesystem::path &eqmapPath) -> void;
	};
}