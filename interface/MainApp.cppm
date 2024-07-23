module;

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

import std;
import vku;
export import vulkan_hpp; // have to be exported for initializing DispatchLoader.
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

		[[nodiscard]] auto update(float timeDelta) -> vulkan::Frame::OnLoopTask;
		auto handleOnLoopResult(const vulkan::Frame::OnLoopResult &onLoopResult) -> void;
	};
}