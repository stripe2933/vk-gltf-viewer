export module vk_gltf_viewer.imgui:GuiTextures;

export import imgui;

namespace vk_gltf_viewer::imgui {
    /**
     * Abstraction of ImGui-accessible textures that are used in ImGui widgets. The rendering backend specific
     * implementation class will store the actual textures and return <tt>ImTextureID</tt>s through the virtual methods.
     */
    export struct GuiTextures {
        virtual ~GuiTextures() = default;

        /// A 16x16 checkerboard texture, with 8x8 white and black tiles alternating.
        [[nodiscard]] virtual ImTextureID getCheckerboardTextureID() const = 0;
    };
}