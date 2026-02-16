module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.imgui:UserData;

import std;
import imgui.internal;

namespace vk_gltf_viewer::imgui {
    export class UserData final {
    public:
        bool resolveAnimationCollisionAutomatically = false;

        std::list<std::u8string> recentAssetPaths;
        std::list<std::u8string> recentSkyboxPaths;

        [[nodiscard]] ImGuiSettingsHandler createSettingsHandler() LIFETIMEBOUND {
            ImGuiSettingsHandler result;
            result.TypeName = "UserData";
            result.TypeHash = ImHashStr("UserData");
            result.ReadOpenFn = [](ImGuiContext*, ImGuiSettingsHandler *handler, const char *name) -> void* {
                if (std::strcmp(name, "Settings") == 0) {
                    return reinterpret_cast<void*>(Section::Settings);
                }
                if (std::strcmp(name, "RecentAssets") == 0) {
                    static_cast<UserData*>(handler->UserData)->recentAssetPaths.clear();
                    return reinterpret_cast<void*>(Section::RecentAssets);
                }
                if (std::strcmp(name, "RecentSkyboxes") == 0) {
                    static_cast<UserData*>(handler->UserData)->recentSkyboxPaths.clear();
                    return reinterpret_cast<void*>(Section::RecentSkyboxes);
                }

                return nullptr;
            };
            result.ReadLineFn = [](ImGuiContext*, ImGuiSettingsHandler *handler, void *entry, const char *line) -> void {
                switch (static_cast<Section>(reinterpret_cast<std::underlying_type_t<Section>>(entry))) {
                    case Section::Settings:
                        if (int value; std::sscanf(line, "ResolveAnimationCollisionAutomatically=%d", &value) == 1) {
                            static_cast<UserData*>(handler->UserData)->resolveAnimationCollisionAutomatically = value == 1;
                        }
                        break;
                    case Section::RecentAssets:
                        if (line[0] != '\0') {
                            static_cast<UserData*>(handler->UserData)->recentAssetPaths.emplace_back(reinterpret_cast<const char8_t*>(line));
                        }
                        break;
                    case Section::RecentSkyboxes:
                        if (line[0] != '\0') {
                            static_cast<UserData*>(handler->UserData)->recentSkyboxPaths.emplace_back(reinterpret_cast<const char8_t*>(line));
                        }
                        break;
                    default:
                        break;
                }
            };
            result.WriteAllFn = [](ImGuiContext*, ImGuiSettingsHandler *handler, ImGuiTextBuffer* out_buf) {
                out_buf->appendf("[%s][Settings]\n", handler->TypeName);
                out_buf->appendf("ResolveAnimationCollisionAutomatically=%d\n", static_cast<UserData*>(handler->UserData)->resolveAnimationCollisionAutomatically ? 1 : 0);
                out_buf->appendf("\n");

                out_buf->appendf("[%s][RecentAssets]\n", handler->TypeName);
                for (const std::u8string &path : static_cast<UserData*>(handler->UserData)->recentAssetPaths) {
                    out_buf->appendf("%s\n", reinterpret_cast<const char*>(path.c_str()));
                }
                out_buf->append("\n");

                out_buf->appendf("[%s][RecentSkyboxes]\n", handler->TypeName);
                for (const std::u8string &path : static_cast<UserData*>(handler->UserData)->recentSkyboxPaths) {
                    out_buf->appendf("%s\n", reinterpret_cast<const char*>(path.c_str()));
                }
            };
            result.UserData = this;

            return result;
        }
        
        void pushRecentAssetPath(std::u8string path) noexcept {
            if (auto it = std::ranges::find(recentAssetPaths, path); it == recentAssetPaths.end()) {
                recentAssetPaths.emplace_front(std::move(path));
            }
            else {
                // The selected file is already in the list. Move it to the front.
                recentAssetPaths.splice(recentAssetPaths.begin(), recentAssetPaths, it);
            }
        }
        
        void pushRecentSkyboxPath(std::u8string path) noexcept {
            if (auto it = std::ranges::find(recentSkyboxPaths, path); it == recentSkyboxPaths.end()) {
                recentSkyboxPaths.emplace_front(std::move(path));
            }
            else {
                // The selected file is already in the list. Move it to the front.
                recentSkyboxPaths.splice(recentSkyboxPaths.begin(), recentSkyboxPaths, it);
            }
        }

    private:
        enum class Section : std::intptr_t {
            Settings,
            RecentAssets,
            RecentSkyboxes,
        };
    };
}