module;

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

import std;
import vku;
import :control.AppWindow;
import :vulkan.Frame;
import :vulkan.Gpu;

namespace vk_gltf_viewer {
	export class MainApp {
	public:
		explicit MainApp();
		~MainApp();

		auto run() -> void;

	private:
	    AppState appState;

		fastgltf::GltfDataBuffer gltfDataBuffer{};
		fastgltf::Expected<fastgltf::Asset> assetExpected = loadAsset(std::getenv("GLTF_PATH"));

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		control::AppWindow window { instance, appState };
		vulkan::Gpu gpu { instance, window.getSurface() };

		std::list<vku::MappedBuffer> stagingBuffers{};

		gltf::AssetResources assetResources { assetExpected.get(), std::filesystem::path { std::getenv("GLTF_PATH") }.parent_path(), gpu, { .supportUint8Index = false /* TODO: change this value depend on vk::PhysicalDeviceIndexTypeUint8FeaturesKHR */ } };
    	gltf::SceneResources sceneResources { assetResources, assetExpected->scenes[assetExpected->defaultScene.value_or(0)], gpu };

		// Buffers, images, image views and samplers.
		vku::AllocatedImage eqmapImage = createEqmapImage();
		vk::raii::ImageView eqmapImageView { gpu.device, eqmapImage.getViewCreateInfo() };
		vk::raii::Sampler eqmapSampler = createEqmapSampler();

		// Descriptor/command pools.
    	vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();

		// Descriptor sets.
		vk::DescriptorSet eqmapImageImGuiDescriptorSet;

    	[[nodiscard]] auto loadAsset(const std::filesystem::path &path) -> decltype(assetExpected);

		[[nodiscard]] auto createInstance() const -> decltype(instance);
		[[nodiscard]] auto createEqmapImage() -> decltype(eqmapImage);
		[[nodiscard]] auto createEqmapSampler() const -> decltype(eqmapSampler);
    	[[nodiscard]] auto createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool);

		auto recordEqmapStagingCommands(vk::CommandBuffer transferCommandBuffer) -> void;
		auto recordImageMipmapGenerationCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;

		[[nodiscard]] auto update(float timeDelta) -> vulkan::Frame::ExecutionTask;
		auto handleExecutionResult(const vulkan::Frame::ExecutionResult &onLoopResult) -> void;
	};
}