module;

#include <array>
#include <compare>
#include <list>
#include <memory>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

export import vulkan_hpp; // have to be exported for initializing DispatchLoader.
import :control.AppWindow;
import :control.Camera;
import :vulkan.frame.Frame;
import :vulkan.Gpu;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { (Is, __VA_ARGS__)... }; })

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
		vulkan::Gpu gpu { instance, *window.surface };

		std::list<vku::MappedBuffer> stagingBuffers{};

		// Buffers, images, image views and samplers.
		vku::AllocatedImage eqmapImage = createEqmapImage();
		vk::raii::ImageView eqmapImageView = createEqmapImageView();
		vk::raii::Sampler eqmapSampler = createEqmapSampler();

		// Descriptor/command pools.
    	vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();

		// Descriptor sets.
		vk::DescriptorSet eqmapImageImGuiDescriptorSet;

		// Frame related stuffs.
		std::shared_ptr<vulkan::frame::SharedData> frameSharedData = createFrameSharedData();
		std::array<vulkan::frame::Frame, 2> frames = ARRAY_OF(2, vulkan::Frame { frameSharedData, gpu });

    	[[nodiscard]] auto loadAsset(const std::filesystem::path &path) -> decltype(assetExpected);

		[[nodiscard]] auto createInstance() const -> decltype(instance);
		[[nodiscard]] auto createEqmapImage() -> decltype(eqmapImage);
		[[nodiscard]] auto createEqmapImageView() const -> decltype(eqmapImageView);
		[[nodiscard]] auto createEqmapSampler() const -> decltype(eqmapSampler);
    	[[nodiscard]] auto createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool);
    	[[nodiscard]] auto createFrameSharedData() -> decltype(frameSharedData);

		[[nodiscard]] auto update(float timeDelta) -> vulkan::Frame::OnLoopTask;
		auto handleOnLoopResult(const vulkan::Frame::OnLoopResult &onLoopResult) -> void;

		auto recordImageMipmapGenerationCommands(vk::CommandBuffer graphicsCommandBuffer) const -> void;
	};
}