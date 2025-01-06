export module vk_gltf_viewer:MainApp;

import std;
import :control.AppWindow;
import :gltf.algorithm.miniball;
import :gltf.AssetExternalBuffers;
import :gltf.AssetGpuBuffers;
import :gltf.AssetGpuTextures;
import :gltf.AssetGpuFallbackTexture;
import :gltf.AssetSceneGpuBuffers;
import :gltf.AssetSceneHierarchy;
import :gltf.MaterialVariantsMapping;
import :vulkan.dsl.Asset;
import :vulkan.dsl.ImageBasedLighting;
import :vulkan.dsl.Scene;
import :vulkan.dsl.Skybox;
import :vulkan.Frame;

namespace vk_gltf_viewer {
    export class MainApp {
    public:
        explicit MainApp();
        ~MainApp();

        void run();

    private:
        static constexpr fastgltf::Extensions SUPPORTED_EXTENSIONS
            = fastgltf::Extensions::KHR_materials_unlit
            | fastgltf::Extensions::KHR_materials_variants
            | fastgltf::Extensions::KHR_texture_basisu
            | fastgltf::Extensions::KHR_texture_transform
            | fastgltf::Extensions::EXT_mesh_gpu_instancing;
        static constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

        /**
         * @brief Bundle of glTF asset and additional resources necessary for the rendering.
         */
        class Gltf {
            fastgltf::GltfDataBuffer dataBuffer;

        public:
            /**
             * @brief The directory where the glTF file is located.
             */
            std::filesystem::path directory;

            /**
             * @brief The glTF asset that is loaded from the file.
             *
             * This MUST not be assigned (Gltf and fastgltf::Asset is one-to-one relationship). The reason this field
             * type is mutable reference is to allow the user to change some trivial properties.
             */
            fastgltf::Asset asset;

            /**
             * @brief Associative data structure for KHR_materials_variants.
             */
            gltf::MaterialVariantsMapping materialVariantsMapping { asset };

        private:
            const vulkan::Gpu &gpu;

        public:
            /**
			 * @brief External buffers that are not embedded in the glTF file, such like .bin files.
             * 
             * If you specified <tt>fastgltf::Options::LoadExternalBuffers</tt>, this should be omitted.
             */
            gltf::AssetExternalBuffers assetExternalBuffers{ asset, directory };

            gltf::AssetGpuBuffers assetGpuBuffers;
            gltf::AssetGpuTextures assetGpuTextures;

            /**
             * @brief The glTF scene that is currently used by.
             *
             * This could be changed, but direct assignment is forbidden (because changing this field requires the additional
             * modification of <tt>sceneGpuBuffers</tt> and <tt>sceneMiniball</tt>). Use <tt>setScene</tt> for the purpose.
             */
            fastgltf::Scene &scene { asset.scenes[asset.defaultScene.value_or(0)] };

            /**
             * @brief Hierarchy information of current scene.
             */
            gltf::AssetSceneHierarchy sceneHierarchy { asset, scene };

			/**
			 * @brief GPU buffers that are used for rendering the current scene.
			 */
            gltf::AssetSceneGpuBuffers sceneGpuBuffers;

			/**
			 * @brief Smallest enclosing sphere of all meshes (a.k.a. miniball) in the scene.
             * 
			 * The first of the pair is the center, and the second is the radius of the miniball.
			 */
            std::pair<fastgltf::math::dvec3, double> sceneMiniball;

            Gltf(
                fastgltf::Parser &parser,
                const std::filesystem::path &path,
                const vulkan::Gpu &gpu [[clang::lifetimebound]],
                BS::thread_pool<> threadPool = {});

            void setScene(std::size_t sceneIndex);
        };
        
        struct SkyboxResources {
            vku::AllocatedImage reducedEqmapImage;
            vk::raii::ImageView reducedEqmapImageView;
            vku::AllocatedImage cubemapImage;
            vk::raii::ImageView cubemapImageView;
            vk::DescriptorSet imGuiEqmapTextureDescriptorSet;
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

        // --------------------
        // Vulkan swapchain.
        // --------------------

        vk::Extent2D swapchainExtent = getSwapchainExtent();
        vk::raii::SwapchainKHR swapchain = createSwapchain();
        std::vector<vk::Image> swapchainImages = swapchain.getImages();

        // --------------------
        // glTF resources.
        // --------------------

        fastgltf::Parser parser { SUPPORTED_EXTENSIONS };
        std::optional<Gltf> gltf;

        // --------------------
        // Buffers, images, image views and samplers.
        // --------------------

        ImageBasedLightingResources imageBasedLightingResources = createDefaultImageBasedLightingResources();
        std::optional<SkyboxResources> skyboxResources{};
        vku::AllocatedImage brdfmapImage = createBrdfmapImage();
        vk::raii::ImageView brdfmapImageView { gpu.device, brdfmapImage.getViewCreateInfo() };
        vk::raii::Sampler reducedEqmapSampler = createEqmapSampler();
        gltf::AssetGpuFallbackTexture gpuFallbackTexture { gpu };

        // --------------------
        // Descriptor sets.
        // --------------------

        std::vector<vk::DescriptorSet> assetTextureDescriptorSets;

        // --------------------
        // Frames.
        // --------------------

        vulkan::SharedData sharedData { gpu, swapchainExtent, swapchainImages };
        std::array<vulkan::Frame, FRAMES_IN_FLIGHT> frames{ vulkan::Frame { gpu, sharedData }, vulkan::Frame { gpu, sharedData } };
        
        [[nodiscard]] vk::raii::Instance createInstance() const;
        [[nodiscard]] vk::raii::SwapchainKHR createSwapchain(vk::SwapchainKHR oldSwapchain = {}) const;

        [[nodiscard]] auto createDefaultImageBasedLightingResources() const -> ImageBasedLightingResources;
        [[nodiscard]] auto createEqmapSampler() const -> vk::raii::Sampler;
        [[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);

        void loadGltf(const std::filesystem::path &path);
        void loadEqmap(const std::filesystem::path &eqmapPath);

        [[nodiscard]] vk::Extent2D getSwapchainExtent() const {
            const glm::ivec2 framebufferSize = window.getFramebufferSize();
            return { static_cast<std::uint32_t>(framebufferSize.x), static_cast<std::uint32_t>(framebufferSize.y) };
        }

        void recordSwapchainImageLayoutTransitionCommands(vk::CommandBuffer cb) const;
    };
}