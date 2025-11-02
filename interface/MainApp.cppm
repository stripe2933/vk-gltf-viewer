export module vk_gltf_viewer.MainApp;

import std;

import vk_gltf_viewer.AppState;
import vk_gltf_viewer.gltf.AssetExtended;
import vk_gltf_viewer.control.AppWindow;
import vk_gltf_viewer.imgui.UserData;
import vk_gltf_viewer.Renderer;
import vk_gltf_viewer.vulkan.Frame;

namespace vk_gltf_viewer {
    export class MainApp {
    public:
        explicit MainApp();

        void run();

    private:
        static constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

        struct ImGuiContext {
            imgui::UserData userData;

            ImGuiContext(const control::AppWindow &window, vk::Instance instance, const vulkan::Gpu &gpu);
            ~ImGuiContext();
        };
        
        struct SkyboxResources {
            vku::raii::AllocatedImage reducedEqmapImage;
            vk::raii::ImageView reducedEqmapImageView;
            vku::raii::AllocatedImage cubemapImage;
            vk::raii::ImageView cubemapImageView;
            vk::DescriptorSet imGuiEqmapTextureDescriptorSet;

            ~SkyboxResources();
        };

        struct ImageBasedLightingResources {
            vku::raii::AllocatedBuffer cubemapSphericalHarmonicsBuffer;
            vku::raii::AllocatedImage prefilteredmapImage;
            vk::raii::ImageView prefilteredmapImageView;
        };

        AppState appState;

        vk::raii::Context context;
        vk::raii::Instance instance = createInstance();

        control::AppWindow window { instance };
        std::optional<glm::dvec2> lastMouseDownPosition;
        bool drawSelectionRectangle;
        std::size_t lastMouseEnteredViewIndex;

        vulkan::Gpu gpu;
        std::shared_ptr<Renderer> renderer;

        ImGuiContext imGuiContext { window, *instance, gpu };

        std::shared_ptr<gltf::AssetExtended> assetExtended;

        // --------------------
        // Buffers, images, image views and samplers.
        // --------------------

        ImageBasedLightingResources imageBasedLightingResources = createDefaultImageBasedLightingResources();
        std::optional<SkyboxResources> skyboxResources{};
        vku::raii::AllocatedImage brdfmapImage = createBrdfmapImage();
        vk::raii::ImageView brdfmapImageView { gpu.device, brdfmapImage.getViewCreateInfo(vk::ImageViewType::e2D) };
        vk::raii::Sampler reducedEqmapSampler = createEqmapSampler();

        // --------------------
        // Frames.
        // --------------------

        vulkan::SharedData sharedData;
        std::array<vulkan::Frame, FRAMES_IN_FLIGHT> frames;
        
        [[nodiscard]] vk::raii::Instance createInstance() const;

        [[nodiscard]] ImageBasedLightingResources createDefaultImageBasedLightingResources() const;
        [[nodiscard]] vk::raii::Sampler createEqmapSampler() const;
        [[nodiscard]] vku::raii::AllocatedImage createBrdfmapImage() const;

        void loadGltf(const std::filesystem::path &path);
        void closeGltf();
        void loadEqmap(const std::filesystem::path &eqmapPath);
    };
}