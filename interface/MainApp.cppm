module;

#include <fastgltf/core.hpp>

export module vk_gltf_viewer:MainApp;

import std;
import vku;
import :control.AppWindow;
import :gltf.AssetResources;
import :gltf.AssetTextures;
import :gltf.SceneResources;
import :vulkan.dsl.Asset;
import :vulkan.dsl.ImageBasedLighting;
import :vulkan.dsl.Scene;
import :vulkan.dsl.Skybox;
import :vulkan.Frame;
import :vulkan.Gpu;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define ARRAY_OF(N, ...) INDEX_SEQ(Is, N, { return std::array { ((void)Is, __VA_ARGS__)... }; })

namespace vk_gltf_viewer {
    export class MainApp {
    public:
        explicit MainApp();
        ~MainApp();

        auto run() -> void;

    private:
        class GltfAsset {
            struct DataBufferLoader {
                fastgltf::GltfDataBuffer dataBuffer;

                explicit DataBufferLoader(const std::filesystem::path &path);
            };

            DataBufferLoader dataBufferLoader;

        public:
            std::filesystem::path assetDir;
            fastgltf::Expected<fastgltf::Asset> assetExpected;

        private:
            /**
             * Intermediate buffer data that would be dropped after the field initialization. DO NOT use this outside the constructor!
             */
            std::unique_ptr<gltf::AssetExternalBuffers> assetExternalBuffers;

        public:
            gltf::AssetResources assetResources;
            gltf::AssetTextures assetTextures;
            std::unordered_map<std::size_t, vk::raii::ImageView> imageViews;
            gltf::SceneResources sceneResources;

            GltfAsset(fastgltf::Parser &parser, const std::filesystem::path &path, const vulkan::Gpu &gpu [[clang::lifetimebound]]);

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
        };

        struct ImageBasedLightingResources {
            vku::AllocatedBuffer cubemapSphericalHarmonicsBuffer;
            vku::AllocatedImage prefilteredmapImage;
            vk::raii::ImageView prefilteredmapImageView;
        };

        static constexpr fastgltf::Extensions supportedExtensions = fastgltf::Extensions::KHR_texture_basisu;

        AppState appState;

        vk::raii::Context context;
        vk::raii::Instance instance = createInstance();
        control::AppWindow window { instance, appState };
        vulkan::Gpu gpu { instance, window.getSurface() };

        fastgltf::Parser parser { supportedExtensions };

        // Buffers, images, image views and samplers.
        vku::AllocatedImage assetFallbackImage = createAssetFallbackImage();
        vk::raii::ImageView assetFallbackImageView { gpu.device, assetFallbackImage.getViewCreateInfo() };
        vk::raii::Sampler assetDefaultSampler = createAssetDefaultSampler();
        std::optional<GltfAsset> gltfAsset;
        ImageBasedLightingResources imageBasedLightingResources = createDefaultImageBasedLightingResources();
        std::optional<SkyboxResources> skyboxResources{};
        vku::AllocatedImage brdfmapImage = createBrdfmapImage();
        vk::raii::ImageView brdfmapImageView { gpu.device, brdfmapImage.getViewCreateInfo() };
        vk::raii::Sampler reducedEqmapSampler = createEqmapSampler();

        // Descriptor pools.
        vk::raii::DescriptorPool imGuiDescriptorPool = createImGuiDescriptorPool();

        // Descriptor sets.
        std::vector<vk::DescriptorSet> assetTextureDescriptorSets;

        // Frames.
        vulkan::SharedData sharedData { gpu, window.getSurface(), vk::Extent2D { static_cast<std::uint32_t>(window.getFramebufferSize().x), static_cast<std::uint32_t>(window.getFramebufferSize().y) } };
        std::array<vulkan::Frame, 2> frames{ vulkan::Frame { gpu, sharedData }, vulkan::Frame { gpu, sharedData } };
        
        [[nodiscard]] auto createInstance() const -> vk::raii::Instance;
        [[nodiscard]] auto createAssetFallbackImage() const -> vku::AllocatedImage;
        [[nodiscard]] auto createAssetDefaultSampler() const -> vk::raii::Sampler;
        [[nodiscard]] auto createDefaultImageBasedLightingResources() const -> ImageBasedLightingResources;
        [[nodiscard]] auto createEqmapSampler() const -> vk::raii::Sampler;
        [[nodiscard]] auto createBrdfmapImage() const -> decltype(brdfmapImage);
        [[nodiscard]] auto createImGuiDescriptorPool() -> decltype(imGuiDescriptorPool);

        auto processEqmapChange(const std::filesystem::path &eqmapPath) -> void;
    };
}