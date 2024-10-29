export module vk_gltf_viewer:MainApp;

import std;
import :control.AppWindow;
import :gltf.algorithm.miniball;
import :gltf.AssetGpuBuffers;
import :gltf.AssetGpuTextures;
import :gltf.AssetGpuFallbackTexture;
import :gltf.AssetSceneGpuBuffers;
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

        auto run() -> void;

    private:
        /**
         * @brief Bundle of glTF asset and additional resources necessary for the rendering.
         */
        class Gltf {
        public:
            /**
             * @brief The directory where the glTF file is located.
             */
            std::filesystem::path directory;

        private:
            fastgltf::Expected<fastgltf::Asset> assetExpected;
            const vulkan::Gpu &gpu;

        public:
            /**
             * @brief The glTF asset that is loaded from the file.
             *
             * This MUST not be assigned (Gltf and fastgltf::Asset is one-to-one relationship). The reason this field
             * type is mutable reference is to allow the user to change some trivial properties.
             */
            fastgltf::Asset &asset { assetExpected.get() };

        private:
            /**
             * Intermediate buffer data that would be dropped after the field initialization. DO NOT use this outside the constructor!
             */
            std::unique_ptr<gltf::AssetExternalBuffers> assetExternalBuffers;

        public:
            gltf::AssetGpuBuffers assetGpuBuffers;
            gltf::AssetGpuTextures assetGpuTextures;

            /**
             * @brief The glTF scene that is currently used by.
             *
             * This could be changed, but direct assignment is forbidden (because changing this field requires the additional
             * modification of <tt>sceneGpuBuffers</tt> and <tt>sceneMiniball</tt>). Use <tt>setScene</tt> for the purpose.
             */
            fastgltf::Scene &scene { asset.scenes[asset.defaultScene.value_or(0)] };
            gltf::AssetSceneGpuBuffers sceneGpuBuffers;
            std::pair<glm::dvec3, double> sceneMiniball { gltf::algorithm::getMiniball(asset, scene, sceneGpuBuffers.nodeWorldTransformBuffer.asRange<const glm::mat4>()) };

            Gltf(
                fastgltf::Parser &parser,
                const std::filesystem::path &path,
                const vulkan::Gpu &gpu [[clang::lifetimebound]],
                fastgltf::GltfDataBuffer dataBuffer = fastgltf::GltfDataBuffer{});

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

        fastgltf::Parser parser { fastgltf::Extensions::KHR_texture_basisu };
        std::optional<Gltf> gltf;

        // Buffers, images, image views and samplers.
        ImageBasedLightingResources imageBasedLightingResources = createDefaultImageBasedLightingResources();
        std::optional<SkyboxResources> skyboxResources{};
        vku::AllocatedImage brdfmapImage = createBrdfmapImage();
        vk::raii::ImageView brdfmapImageView { gpu.device, brdfmapImage.getViewCreateInfo() };
        vk::raii::Sampler reducedEqmapSampler = createEqmapSampler();
        gltf::AssetGpuFallbackTexture gpuFallbackTexture { gpu };

        // Descriptor pools.
        vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();

        // Descriptor sets.
        std::vector<vk::DescriptorSet> assetTextureDescriptorSets;

        // Frames.
        vulkan::SharedData sharedData { gpu, window.getSurface(), vk::Extent2D { static_cast<std::uint32_t>(window.getFramebufferSize().x), static_cast<std::uint32_t>(window.getFramebufferSize().y) } };
        std::array<vulkan::Frame, 2> frames{ vulkan::Frame { gpu, sharedData }, vulkan::Frame { gpu, sharedData } };
        
        [[nodiscard]] auto createInstance() const -> vk::raii::Instance;
        [[nodiscard]] auto createDefaultImageBasedLightingResources() const -> ImageBasedLightingResources;
        [[nodiscard]] auto createEqmapSampler() const -> vk::raii::Sampler;
        [[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);
        [[nodiscard]] auto createImGuiDescriptorPool() -> decltype(imGuiDescriptorPool);

        auto processEqmapChange(const std::filesystem::path &eqmapPath) -> void;
    };
}