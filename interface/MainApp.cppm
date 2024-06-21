module;

#include <array>
#include <compare>
#include <memory>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

export import vulkan_hpp; // have to be exported for initializing DispatchLoader.
import :control.AppWindow;
import :AppState;
import :vulkan.frame.Frame;
import :vulkan.Gpu;

namespace vk_gltf_viewer {
	export class MainApp {
		static constexpr std::size_t MAX_FRAMES_IN_FLIGHT = 2;

	public:
	    AppState appState;

		fastgltf::GltfDataBuffer gltfDataBuffer{};
		fastgltf::Expected<fastgltf::Asset> assetExpected = loadAsset(std::getenv("GLTF_PATH"));

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		control::AppWindow window { instance, appState };
		vulkan::Gpu gpu { instance, *window.surface };
    	vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();
		std::shared_ptr<vulkan::SharedData> sharedData = createSharedData();
		std::array<vulkan::Frame, MAX_FRAMES_IN_FLIGHT> frames = createFrames();

		explicit MainApp();
		~MainApp();

		auto run() -> void;

	private:
    	[[nodiscard]] auto loadAsset(const std::filesystem::path &path) -> decltype(assetExpected);

		[[nodiscard]] auto createInstance() const -> decltype(instance);
    	[[nodiscard]] auto createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool);
		// TODO: should be const qualified, but cannot because fastgltf:Expected::get is not const qualified. Re-qualify
		//  when fastgltf::Expected::get is const qualified.
		[[nodiscard]] auto createSharedData() -> decltype(sharedData);
		[[nodiscard]] auto createFrames() const -> decltype(frames);

		auto update(float timeDelta) -> void;
		auto handleOnLoopResult(const vulkan::Frame::OnLoopResult &onLoopResult) -> void;
	};
}