module;

#include <cassert>

export module vk_gltf_viewer.imgui.UserData;

import std;
export import imgui;
import imgui.internal;

namespace vk_gltf_viewer::imgui {
    export struct UserData final {
        struct PlatformResource {
            ImTextureID checkerboardTextureID;

            virtual ~PlatformResource() = default;
        };

        bool resolveAnimationCollisionAutomatically = false;

        std::unique_ptr<PlatformResource> platformResource;

        void registerSettingHandler() {
            ImGuiSettingsHandler handler;
            handler.TypeName = "UserData";
            handler.TypeHash = ImHashStr("UserData");
            handler.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler *handler, const char *name) -> void* {
                assert(std::strcmp(handler->TypeName, "UserData") == 0 && std::strcmp(name, "UserData") == 0);
                return handler->UserData;
            };
            handler.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler*, void *entry, const char *line) -> void {
                if (int value; std::sscanf(line, "ResolveAnimationCollisionAutomatically=%d", &value) == 1) {
                    static_cast<UserData*>(entry)->resolveAnimationCollisionAutomatically = value == 1;
                }
            };
            handler.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler *handler, ImGuiTextBuffer* out_buf) {
                assert(std::strcmp(handler->TypeName, "UserData") == 0);
                out_buf->appendf("[UserData][UserData]\n");
                out_buf->appendf("ResolveAnimationCollisionAutomatically=%d\n", static_cast<UserData*>(handler->UserData)->resolveAnimationCollisionAutomatically ? 1 : 0);
            };
            handler.UserData = this;
            ImGui::AddSettingsHandler(&handler);
        }
    };
}