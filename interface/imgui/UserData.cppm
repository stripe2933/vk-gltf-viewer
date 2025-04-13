export module vk_gltf_viewer:imgui.UserData;

import imgui;

namespace vk_gltf_viewer::imgui {
    export struct UserData {
        ImTextureID checkerboardTextureID;

        virtual ~UserData() = default;
    };
}