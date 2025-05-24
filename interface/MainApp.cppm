export module vk_gltf_viewer:MainApp;

import std;
import vk_gltf_viewer.data_structure.ImmutableRing;
import vk_gltf_viewer.helpers;
import :AppState;
import :control.AppWindow;
import :gltf.Animation;
import :gltf.AssetExternalBuffers;
import :gltf.data_structure.MaterialVariantsMapping;
import :gltf.data_structure.SceneInverseHierarchy;
import :gltf.NodeWorldTransforms;
import :gltf.SceneNodeLevels;
import :gltf.StateCachedNodeVisibilityStructure;
import :gltf.TextureUsages;
import :helpers.fastgltf;
import :imgui.UserData;
import :vulkan.dsl.Asset;
import :vulkan.dsl.ImageBasedLighting;
import :vulkan.dsl.Skybox;
import :vulkan.Frame;

namespace vk_gltf_viewer {
    export class MainApp {
    public:
        explicit MainApp();

        void run();

    private:
        static constexpr fastgltf::Extensions SUPPORTED_EXTENSIONS
            = fastgltf::Extensions::KHR_materials_emissive_strength
            | fastgltf::Extensions::KHR_materials_ior
            | fastgltf::Extensions::KHR_materials_unlit
            | fastgltf::Extensions::KHR_materials_variants
            | fastgltf::Extensions::KHR_mesh_quantization
#ifdef SUPPORT_KHR_TEXTURE_BASISU
            | fastgltf::Extensions::KHR_texture_basisu
#endif
            | fastgltf::Extensions::KHR_texture_transform
            | fastgltf::Extensions::EXT_mesh_gpu_instancing;
        static constexpr std::uint32_t FRAMES_IN_FLIGHT = 2;

        struct ImGuiContext {
            imgui::UserData userData;

            ImGuiContext(const control::AppWindow &window, vk::Instance instance, const vulkan::Gpu &gpu);
            ~ImGuiContext();
        };

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
            gltf::ds::MaterialVariantsMapping materialVariantsMapping { asset };

            gltf::TextureUsages textureUsages { asset };

            /**
             * @brief Indices of glTF asset materials whose emissive strength is greater than 1.0.
             *
             * This is used for determine whether enable renderer's bloom effect or not (by checking this container is empty).
             */
            std::unordered_set<std::size_t> bloomMaterials;

            /**
			 * @brief External buffers that are not embedded in the glTF file, such like .bin files.
             * 
             * If you specified <tt>fastgltf::Options::LoadExternalBuffers</tt>, this should be omitted.
             */
            gltf::AssetExternalBuffers assetExternalBuffers{ asset, directory };

            gltf::OrderedPrimitives orderedPrimitives;

            std::vector<gltf::Animation> animations;
            std::shared_ptr<std::vector<bool>> animationEnabled;

            std::size_t sceneIndex;

            gltf::NodeWorldTransforms nodeWorldTransforms;
            std::shared_ptr<gltf::ds::SceneInverseHierarchy> sceneInverseHierarchy;
            gltf::SceneNodeLevels sceneNodeLevels;

            gltf::StateCachedNodeVisibilityStructure nodeVisibilities;
            std::unordered_set<std::size_t> selectedNodes;
            std::optional<std::size_t> hoveringNode;

			/**
			 * @brief Smallest enclosing sphere of all meshes (a.k.a. miniball) in the scene.
             * 
			 * The first of the pair is the center, and the second is the radius of the miniball.
			 */
            Lazy<std::pair<fastgltf::math::fvec3, float>> sceneMiniball;

            Gltf(fastgltf::Parser &parser, const std::filesystem::path &path);

            void setScene(std::size_t sceneIndex);
        };
        
        struct SkyboxResources {
            vku::AllocatedImage reducedEqmapImage;
            vk::raii::ImageView reducedEqmapImageView;
            vku::AllocatedImage cubemapImage;
            vk::raii::ImageView cubemapImageView;
            vk::DescriptorSet imGuiEqmapTextureDescriptorSet;

            ~SkyboxResources();
        };

        struct ImageBasedLightingResources {
            vku::AllocatedBuffer cubemapSphericalHarmonicsBuffer;
            vku::AllocatedImage prefilteredmapImage;
            vk::raii::ImageView prefilteredmapImageView;
        };

        AppState appState;

        vk::raii::Context context;
        vk::raii::Instance instance = createInstance();

        control::AppWindow window { instance };
        std::optional<glm::dvec2> lastMouseDownPosition;
        bool drawSelectionRectangle = false;

        vulkan::Gpu gpu { instance, window.getSurface() };

        ImGuiContext imGuiContext { window, *instance, gpu };

        // --------------------
        // Vulkan swapchain.
        // --------------------

        vk::Extent2D swapchainExtent = getSwapchainExtent();
        vk::raii::SwapchainKHR swapchain = createSwapchain();
        std::vector<vk::Image> swapchainImages = swapchain.getImages();

        /**
         * @brief Semaphores that will be signaled when swapchain images are acquired, i.e. the images are ready to be used for rendering.
         *
         * Semaphores in the container is NOT 1-to-1 mapped to swapchain images. Instead, non-pending semaphore can be
         * retrieved on-demand by calling <tt>current()</tt> from the container. After the retrieved semaphore is being
         * pending state, <tt>advance()</tt> must be called to make the next swapchain image acquirement use the fresh
         * semaphore.
         */
        ds::ImmutableRing<vk::raii::Semaphore> swapchainImageAcquireSemaphores;

        /**
         * @brief Semaphores that will be signaled when their corresponding swapchain images are ready to be presented, i.e. all rendering is done in the image.
         *
         * These semaphores are 1-to-1 mapped to swapchain images and the semaphore of the same index should be used for
         * presenting the corresponding swapchain image.
         */
        std::vector<vk::raii::Semaphore> swapchainImageReadySemaphores;

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

        // --------------------
        // Frames.
        // --------------------

        vulkan::SharedData sharedData { gpu, swapchainExtent, swapchainImages };
        std::array<vulkan::Frame, FRAMES_IN_FLIGHT> frames{ vulkan::Frame { sharedData }, vulkan::Frame { sharedData } };
        
        [[nodiscard]] vk::raii::Instance createInstance() const;
        [[nodiscard]] vk::raii::SwapchainKHR createSwapchain(vk::SwapchainKHR oldSwapchain = {}) const;

        [[nodiscard]] ImageBasedLightingResources createDefaultImageBasedLightingResources() const;
        [[nodiscard]] vk::raii::Sampler createEqmapSampler() const;
        [[nodiscard]] vku::AllocatedImage createBrdfmapImage() const;

        void loadGltf(const std::filesystem::path &path);
        void closeGltf();
        void loadEqmap(const std::filesystem::path &eqmapPath);

        [[nodiscard]] vk::Extent2D getSwapchainExtent() const {
            const glm::ivec2 framebufferSize = window.getFramebufferSize();
            return { static_cast<std::uint32_t>(framebufferSize.x), static_cast<std::uint32_t>(framebufferSize.y) };
        }

        void handleSwapchainResize();
        void recordSwapchainImageLayoutTransitionCommands(vk::CommandBuffer cb) const;
    };
}