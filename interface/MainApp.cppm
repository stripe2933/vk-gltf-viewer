module;

#include <array>
#include <compare>
#include <memory>
#include <optional>

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

export import vulkan_hpp; // have to be exported for initializing DispatchLoader.
import :control.AppWindow;
import :control.Camera;
import :vulkan.frame.Frame;
import :vulkan.Gpu;

namespace vk_gltf_viewer {
	export class MainApp {
	public:
	    AppState appState;

		fastgltf::GltfDataBuffer gltfDataBuffer{};
		fastgltf::Expected<fastgltf::Asset> assetExpected = loadAsset(std::getenv("GLTF_PATH"));

		vk::raii::Context context;
		vk::raii::Instance instance = createInstance();
		control::AppWindow window { instance, appState };
		vulkan::Gpu gpu { instance, *window.surface };
    	vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();

		explicit MainApp();
		~MainApp();

		auto run() -> void;

	private:
		static constexpr std::size_t MAX_FRAMES_IN_FLIGHT = 2;

    	[[nodiscard]] auto loadAsset(const std::filesystem::path &path) -> decltype(assetExpected);

		[[nodiscard]] auto createInstance() const -> decltype(instance);
    	[[nodiscard]] auto createImGuiDescriptorPool() const -> decltype(imGuiDescriptorPool);

		[[nodiscard]] auto update(float timeDelta) -> vulkan::Frame::OnLoopTask;
		auto handleOnLoopResult(const vulkan::Frame::OnLoopResult &onLoopResult) -> void;
	};
}