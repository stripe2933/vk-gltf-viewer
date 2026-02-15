module;

#include <cassert>
#include <version>

#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>
#include <IconsFontAwesome4.h>
#include <nfd.hpp>

module vk_gltf_viewer.imgui.TaskCollector;

import fmt;

import vk_gltf_viewer.global;
import vk_gltf_viewer.gltf.algorithm.miniball;
import vk_gltf_viewer.gltf.util;
import vk_gltf_viewer.gui.popup;
import vk_gltf_viewer.gui.utils;
import vk_gltf_viewer.helpers.concepts;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.formatter.ByteSize;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.optional;
import vk_gltf_viewer.helpers.PairHasher;
import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.helpers.TempStringBuffer;
import vk_gltf_viewer.imgui;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }
#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#define MOVE_CAP(X) X = std::move(X)
#ifdef _WIN32
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

using namespace std::string_view_literals;

int boundFpPrecision = 2;

[[nodiscard]] std::optional<std::filesystem::path> processFileDialog(std::span<const nfdfilteritem_t> filterItems, const nfdwindowhandle_t &windowHandle) {
    static NFD::Guard nfdGuard;

    NFD::UniquePath outPath;
    if (nfdresult_t nfdResult = OpenDialog(outPath, filterItems.data(), filterItems.size(), nullptr, windowHandle); nfdResult == NFD_OKAY) {
        return outPath.get();
    }
    else if (nfdResult == NFD_CANCEL) {
        return std::nullopt;
        // Do nothing.
    }
    else {
        throw std::runtime_error { fmt::format("File dialog error: {}", NFD::GetError() ) };
    }
}

void attributeTable(const fastgltf::Asset &asset, std::ranges::viewable_range auto const &attributes) {
    vk_gltf_viewer::imgui::widget::Table<false>(
        "attributes-table",
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit,
        attributes,
        vk_gltf_viewer::imgui::widget::TableColumnInfo { "Attribute", [](const fastgltf::Attribute &attribute) {
            vk_gltf_viewer::imgui::widget::TextUnformatted(attribute.name);
        } },
        vk_gltf_viewer::imgui::widget::TableColumnInfo { "Type", [&](const fastgltf::Attribute &attribute) {
            const fastgltf::Accessor &accessor = asset.accessors[attribute.accessorIndex];
            vk_gltf_viewer::imgui::widget::TextUnformatted(tempStringBuffer.write("{} ({})", accessor.type, accessor.componentType));
        } },
        vk_gltf_viewer::imgui::widget::TableColumnInfo { "Count", [&](const fastgltf::Attribute &attribute) {
            const fastgltf::Accessor &accessor = asset.accessors[attribute.accessorIndex];
            vk_gltf_viewer::imgui::widget::TextUnformatted(tempStringBuffer.write(accessor.count));
        } },
        vk_gltf_viewer::imgui::widget::TableColumnInfo { "Bound", [&](const fastgltf::Attribute &attribute) {
            const fastgltf::Accessor &accessor = asset.accessors[attribute.accessorIndex];

            fastgltf::AccessorBoundsArray::BoundsType boundsType;
            std::size_t size;
            if (accessor.min && accessor.max &&
                (boundsType = accessor.min->type()) == accessor.max->type() &&
                (size = accessor.min->size()) == accessor.max->size()) {
                switch (boundsType) {
                    case fastgltf::AccessorBoundsArray::BoundsType::float64: {
                        const std::span min { accessor.min->data<double>(), size };
                        const std::span max { accessor.max->data<double>(), size };
                        if (size == 1) {
                            vk_gltf_viewer::imgui::widget::TextUnformatted(tempStringBuffer.write("[{1:.{0}f}, {2:.{0}f}]", boundFpPrecision, min[0], max[0]));
                        }
                        else {
                            vk_gltf_viewer::imgui::widget::TextUnformatted(tempStringBuffer.write("{1::.{0}f}x{2::.{0}f}", boundFpPrecision, min, max));
                        }
                        break;
                    }
                    case fastgltf::AccessorBoundsArray::BoundsType::int64: {
                        const std::span min { accessor.min->data<std::int64_t>(), size };
                        const std::span max { accessor.max->data<std::int64_t>(), size };
                        if (size == 1) {
                            vk_gltf_viewer::imgui::widget::TextUnformatted(tempStringBuffer.write("[{}, {}]", min[0], max[0]));
                        } else {
                            vk_gltf_viewer::imgui::widget::TextUnformatted(tempStringBuffer.write("{}x{}", min, max));
                        }
                        break;
                    }
                }
            }
            else {
                vk_gltf_viewer::imgui::widget::TextUnformatted("-"sv);
            }
        } },
        vk_gltf_viewer::imgui::widget::TableColumnInfo { "Normalized", [&](const fastgltf::Attribute &attribute) {
            vk_gltf_viewer::imgui::widget::TextUnformatted(asset.accessors[attribute.accessorIndex].normalized ? "Yes"sv : "No"sv);
        }, ImGuiTableColumnFlags_DefaultHide },
        vk_gltf_viewer::imgui::widget::TableColumnInfo { "Sparse", [&](const fastgltf::Attribute &attribute) {
            vk_gltf_viewer::imgui::widget::TextUnformatted(asset.accessors[attribute.accessorIndex].sparse ? "Yes"sv : "No"sv);
        }, ImGuiTableColumnFlags_DefaultHide },
        vk_gltf_viewer::imgui::widget::TableColumnInfo { "Buffer View", [&](const fastgltf::Attribute &attribute) {
            const fastgltf::Accessor &accessor = asset.accessors[attribute.accessorIndex];
            if (accessor.bufferViewIndex) {
                if (ImGui::TextLink(tempStringBuffer.write(*accessor.bufferViewIndex).view().c_str())) {
                    vk_gltf_viewer::gui::makeWindowVisible(ImGui::FindWindowByName("Buffer Views"));
                }
            }
            else {
                ImGui::TextDisabled("-");
                ImGui::SameLine();
                vk_gltf_viewer::imgui::widget::HelperMarker("(?)", "Zero will be used for accessor data.");
            }
        } });
}

void makeDefaultDockState(ImGuiID viewportDockSpace) {
    // ------------------------------------
    // |       |                  |       |
    // |  LST  |                  |  RST  |
    // |       | centralDockSpace |       |
    // |-------|                  |--------
    // |       |                  |       |
    // |  LSB  |------------------|  RSB  |
    // |       |  bottomSidebar   |       |
    // ------------------------------------

    ImGuiID leftSidebar, leftSidebarTop, leftSidebarBottom, rightSidebar, rightSidebarTop, rightSidebarBottom, bottomSidebar, centralDockSpace;
    ImGui::DockBuilderSplitNode(viewportDockSpace, ImGuiDir_Left, 0.25f, &leftSidebar, &rightSidebar);
    ImGui::DockBuilderSplitNode(leftSidebar, ImGuiDir_Up, 0.5f, &leftSidebarTop, &leftSidebarBottom);
    ImGui::DockBuilderSplitNode(rightSidebar, ImGuiDir_Right, 0.33f, &rightSidebar, &bottomSidebar),
    ImGui::DockBuilderSplitNode(rightSidebar, ImGuiDir_Up, 0.5f, &rightSidebarTop, &rightSidebarBottom);
    ImGui::DockBuilderSplitNode(bottomSidebar, ImGuiDir_Down, 0.3f, &bottomSidebar, &centralDockSpace);

    // leftSidebarTop
    ImGui::DockBuilderDockWindow("Asset Info", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Buffers", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Buffer Views", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Images", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Samplers", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Textures", leftSidebarTop);

    // leftSidebarBottom
    ImGui::DockBuilderDockWindow("Scene Hierarchy", leftSidebarBottom);
    ImGui::DockBuilderDockWindow("IBL", leftSidebarBottom);

    // rightSidebarTop
    ImGui::DockBuilderDockWindow("Renderer Setting", rightSidebarTop);

    // rightSidebarBottom
    ImGui::DockBuilderDockWindow("Node Inspector", rightSidebarBottom);

    // bottomSidebar
    ImGui::DockBuilderDockWindow("Material Editor", bottomSidebar);
    ImGui::DockBuilderDockWindow("Material Variants", bottomSidebar);
    ImGui::DockBuilderDockWindow("Animation", bottomSidebar);

    ImGui::DockBuilderFinish(viewportDockSpace);
}

vk_gltf_viewer::control::ImGuiTaskCollector::ImGuiTaskCollector(std::queue<Task> &tasks, const ImRect &oldPassthruRect, const imgui::GuiTextures &guiTextures)
    : tasks { tasks }
    , guiTextures { guiTextures } {
    // If there is no imgui.ini file, make default dock state to avoid the initial window sprawling.
    // This should be called before any ImGui::NewFrame() call, because that will create the imgui.ini file.
    bool shouldMakeDefaultDockState = false;
    if (static bool init = true; init) {
        if (!std::filesystem::exists(ImGui::GetIO().IniFilename)) {
            shouldMakeDefaultDockState = true;
        }
        init = false;
    }

    ImGui::NewFrame();

    // Enable global docking.
    ImGuiID dockSpace = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_NoDockingOverCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
    if (shouldMakeDefaultDockState) {
        makeDefaultDockState(dockSpace);
    }

    // Get central node region.
    centerNodeRect = ImGui::DockBuilderGetCentralNode(dockSpace)->Rect();
    if (centerNodeRect.ToVec4() != oldPassthruRect.ToVec4()) {
        tasks.emplace(std::in_place_type<task::ChangePassthruRect>, centerNodeRect);
    }
}

vk_gltf_viewer::control::ImGuiTaskCollector::~ImGuiTaskCollector() {
    if (!assetInspectorCalled) {
        for (auto name : { "Asset Info", "Buffers", "Buffer Views", "Images", "Samplers", "Textures" }) {
            imgui::widget::WindowWithCenteredText(name, "Asset is not loaded."sv);
        }
    }
    if (!materialEditorCalled) {
        imgui::widget::WindowWithCenteredText("Material Editor", "Asset is not loaded."sv);
    }
    if (!sceneHierarchyCalled) {
        imgui::widget::WindowWithCenteredText("Scene Hierarchy", "Asset is not loaded."sv);
    }
    if (!nodeInspectorCalled) {
        imgui::widget::WindowWithCenteredText("Node Inspector", "Asset is not loaded."sv);
    }
    if (!imageBasedLightingCalled) {
        imgui::widget::WindowWithCenteredText("IBL", "Input equirectangular map is not loaded."sv);
    }

    ImGui::Render();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::menuBar(
    std::list<std::filesystem::path> &recentGltfs,
    std::list<std::filesystem::path> &recentSkyboxes,
    nfdwindowhandle_t windowHandle
) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open glTF File", "Ctrl+O")) {
                constexpr std::array filterItems {
                    nfdfilteritem_t { "All Supported Files", "gltf,glb" },
                    nfdfilteritem_t { "glTF File", "gltf,glb" },
                };

                if (auto filename = processFileDialog(filterItems, windowHandle)) {
                    tasks.emplace(std::in_place_type<task::LoadGltf>, *filename);
                }
            }
            if (ImGui::BeginMenu("Recent glTF Files")) {
                if (recentGltfs.empty()) {
                    ImGui::MenuItem("<empty>", nullptr, false, false);
                }
                else {
                    static std::string needle;
                    imgui::widget::InputTextWithHint("Search", "Find from recent files...", &needle);

                    ImGui::Separator();

                    for (auto it = recentGltfs.begin(); it != recentGltfs.end(); ++it) {
                        const std::filesystem::path &path = *it;
                    #ifdef _WIN32
                        const std::u8string pathOwnedStr = path.u8string();
                        const cpp_util::cstring_view pathStr { cpp_util::cstring_view::null_terminated, reinterpret_cast<const char*>(pathOwnedStr.c_str()), pathOwnedStr.size() };
                    #else
                        const cpp_util::cstring_view pathStr { path.c_str() };
                    #endif

                        bool hasOccurrence = false;
                        cpp_util::cstring_view haystack = pathStr;
                        while (auto found = std::ranges::search(haystack, needle, {}, LIFT(std::tolower), LIFT(std::tolower))) {
                            hasOccurrence = true;

                            ImVec2 highlightOffset = ImGui::GetCursorScreenPos();
                            highlightOffset.x += ImGui::CalcTextSize(pathStr.data(), found.data()).x;;
                            highlightOffset.y += 1.f; // TODO
                            const ImVec2 highlightSize = imgui::CalcTextSize(std::string_view { found });

                            ImGui::GetWindowDrawList()->AddRectFilled(highlightOffset, highlightOffset + highlightSize, 0xFF00AABB);
                            haystack = haystack.substr(found.end() - haystack.begin());
                        }

                        const bool isFileExists = exists(path);
                        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, isFileExists);
                        if ((needle.empty() || hasOccurrence) && ImGui::MenuItem(pathStr.c_str())) {
                            if (isFileExists) {
                                tasks.emplace(std::in_place_type<task::LoadGltf>, path);
                                needle.clear();
                            }
                            else {
                                gui::popup::open(static_cast<std::string>(PopupNames::fileNotExists), [&recentGltfs, it] {
                                    imgui::widget::TextUnformatted("The file you selected does not exist anymore. Do you want to remove it from the recent files list?");
                                    ImGui::Separator();
                                    if (ImGui::Button("Remove")) {
                                        recentGltfs.erase(it);
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::SetItemDefaultFocus();
                                    ImGui::SameLine();
                                    if (ImGui::Button("Cancel")) {
                                        ImGui::CloseCurrentPopup();
                                    }
                                });
                            }
                        }
                        ImGui::PopItemFlag();
                    }

                    gui::popup::process(PopupNames::fileNotExists);
                }

                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Close glTF File", "Ctrl+W")) {
                tasks.emplace(std::in_place_type<task::CloseGltf>);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Skybox")) {
            if (ImGui::MenuItem("Open Skybox")) {
                constexpr std::array filterItems {
                    nfdfilteritem_t { 
                        "All Supported Images", 
                        "hdr,jpg,jpeg,png"
                    #ifdef SUPPORT_EXR_SKYBOX
                        ",exr",
                    #endif
                    },
                    nfdfilteritem_t { "HDR Image", "hdr" },
                    nfdfilteritem_t { "LDR Image", "jpg,jpeg,png" },
                #ifdef SUPPORT_EXR_SKYBOX
                    nfdfilteritem_t { "EXR Image", "exr" },
                #endif
                };

                if (auto filename = processFileDialog(filterItems, windowHandle)) {
                    tasks.emplace(std::in_place_type<task::LoadEqmap>, *filename);
                }
            }
            if (ImGui::BeginMenu("Recent Skyboxes")) {
                if (recentSkyboxes.empty()) {
                    ImGui::MenuItem("<empty>", nullptr, false, false);
                }
                else {
                    static std::string needle;
                    imgui::widget::InputTextWithHint("Search", "Find from recent files...", &needle);

                    ImGui::Separator();

                    for (auto it = recentSkyboxes.begin(); it != recentSkyboxes.end(); ++it) {
                        const std::filesystem::path &path = *it;
                    #ifdef _WIN32
                        const std::u8string pathOwnedStr = path.u8string();
                        const cpp_util::cstring_view pathStr { cpp_util::cstring_view::null_terminated, reinterpret_cast<const char*>(pathOwnedStr.c_str()), pathOwnedStr.size() };
                    #else
                        const cpp_util::cstring_view pathStr { path.c_str() };
                    #endif

                        bool hasOccurrence = false;
                        cpp_util::cstring_view haystack = pathStr;
                        while (auto found = std::ranges::search(haystack, needle, {}, LIFT(std::tolower), LIFT(std::tolower))) {
                            hasOccurrence = true;

                            ImVec2 highlightOffset = ImGui::GetCursorScreenPos();
                            highlightOffset.x += ImGui::CalcTextSize(pathStr.data(), found.data()).x;
                            highlightOffset.y += 1.f; // TODO
                            const ImVec2 highlightSize = imgui::CalcTextSize(std::string_view { found });

                            ImGui::GetWindowDrawList()->AddRectFilled(highlightOffset, highlightOffset + highlightSize, 0xFF00AABB);
                            haystack = haystack.substr(found.end() - haystack.begin());
                        }

                        const bool isFileExists = exists(path);
                        ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, isFileExists);
                        if ((needle.empty() || hasOccurrence) && ImGui::MenuItem(pathStr.c_str())) {
                            if (isFileExists) {
                                tasks.emplace(std::in_place_type<task::LoadEqmap>, path);
                                needle.clear();
                            }
                            else {
                                gui::popup::open(static_cast<std::string>(PopupNames::fileNotExists), [&recentSkyboxes, it] {
                                    imgui::widget::TextUnformatted("The file you selected does not exist anymore. Do you want to remove it from the recent files list?");
                                    ImGui::Separator();
                                    if (ImGui::Button("Remove")) {
                                        recentSkyboxes.erase(it);
                                        ImGui::CloseCurrentPopup();
                                    }
                                    ImGui::SetItemDefaultFocus();
                                    ImGui::SameLine();
                                    if (ImGui::Button("Cancel")) {
                                        ImGui::CloseCurrentPopup();
                                    }
                                });
                            }
                        }
                        ImGui::PopItemFlag();
                    }

                    gui::popup::process(PopupNames::fileNotExists);
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Setting")) {
            ImGui::MenuItem("Automatically resolve animation collision", nullptr, &static_cast<imgui::UserData*>(ImGui::GetIO().UserData)->resolveAnimationCollisionAutomatically);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void vk_gltf_viewer::control::ImGuiTaskCollector::animations(gltf::AssetExtended &assetExtended) {
    if (ImGui::Begin("Animation")) {
        for (std::size_t animationIndex : ranges::views::upto(assetExtended.asset.animations.size())) {
            auto &[animation, enabled] = assetExtended.animations[animationIndex];
            if (ImGui::Checkbox(gui::getDisplayName(assetExtended.asset.animations, animationIndex).c_str(), &enabled) && enabled) {
                // Collect the indices of colliding animations.
                std::vector<std::size_t> collisionIndices;
                for (std::size_t otherAnimationIndex : ranges::views::upto(assetExtended.asset.animations.size())) {
                    const auto &[otherAnimation, otherAnimationEnabled] = assetExtended.animations[otherAnimationIndex];
                    if (!otherAnimationEnabled) continue;

                    // Exclude self from collision check.
                    if (otherAnimationIndex == animationIndex) continue;

                    for (const auto &[nodeIndex, path] : otherAnimation.nodeUsages) {
                        auto it = animation.nodeUsages.find(nodeIndex);
                        if (it == animation.nodeUsages.end()) continue;

                        if (it->second & path) {
                            collisionIndices.push_back(otherAnimationIndex);
                            break;
                        }
                    }
                }

                if (!collisionIndices.empty()) {
                    if (static_cast<imgui::UserData*>(ImGui::GetIO().UserData)->resolveAnimationCollisionAutomatically) {
                        // Do not open the collision resolve dialog, instead just disable the colliding animations.
                        for (std::size_t collidingAnimationIndex : collisionIndices) {
                            assetExtended.animations[collidingAnimationIndex].second = false;
                        }
                    }
                    else {
                        enabled = false;

                        std::map<std::size_t /* animation index */, std::map<std::size_t /* node index */, Flags<gltf::NodeAnimationUsage>>> collisionByAnimation;
                        for (std::size_t collidingAnimationIndex : collisionIndices) {
                            // Collect which paths are colliding.
                            auto &collisions = collisionByAnimation[collidingAnimationIndex];
                            for (const auto &[nodeIndex, path] : assetExtended.animations[collidingAnimationIndex].first.nodeUsages) {
                                const auto it = animation.nodeUsages.find(nodeIndex);
                                if (it != animation.nodeUsages.end()) {
                                    collisions[nodeIndex] |= it->second & path;
                                }
                            }
                        }

                        gui::popup::open(static_cast<std::string>(PopupNames::resolveAnimationCollision), [&assetExtended, collisionByAnimation = std::move(collisionByAnimation), animationIndex] {
                            imgui::widget::TextUnformatted("The animation you're trying to enable is colliding by other enabled animations."sv);

                            for (const auto &[i, collisionList] : collisionByAnimation) {
                                if (ImGui::TreeNode(gui::getDisplayName(assetExtended.asset.animations, i).c_str())) {
                                    imgui::widget::Table<false>(
                                        "animation-collision-table",
                                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg,
                                        collisionList,
                                        imgui::widget::TableColumnInfo { "Node", decomposer([](std::size_t nodeIndex, auto) {
                                            ImGui::Text("%zu", nodeIndex);
                                        }) },
                                        imgui::widget::TableColumnInfo { "Path", decomposer([](auto, Flags<gltf::NodeAnimationUsage> usage) {
                                            imgui::widget::TextUnformatted(tempStringBuffer.write("{::s}", usage).view());
                                        }) });
                                    ImGui::TreePop();
                                }
                            }

                            imgui::widget::TextUnformatted("Would you like to disable these animations?"sv);

                            ImGui::Separator();

                            ImGui::Checkbox("Don't ask me and resolve automatically", &static_cast<imgui::UserData*>(ImGui::GetIO().UserData)->resolveAnimationCollisionAutomatically);

                            if (ImGui::Button("Yes")) {
                                // Resolve the colliding animations.
                                for (std::size_t collidingAnimationIndex : collisionByAnimation | std::views::keys) {
                                    assetExtended.animations[collidingAnimationIndex].second = false;
                                }
                                assetExtended.animations[animationIndex].second = true;

                                ImGui::CloseCurrentPopup();
                            }
                            ImGui::SetItemDefaultFocus();
                            ImGui::SameLine();
                            if (ImGui::Button("No")) {
                                ImGui::CloseCurrentPopup();
                            }

                        }, true);
                    }
                }
            }
        }

        gui::popup::process(PopupNames::resolveAnimationCollision, ImGuiWindowFlags_AlwaysAutoResize);
    }
    ImGui::End();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetInspector(gltf::AssetExtended &assetExtended) {
    if (ImGui::Begin("Asset Info")) {
        imgui::widget::InputTextWithHint("glTF Version", "<empty>", &assetExtended.asset.assetInfo->gltfVersion);
        imgui::widget::InputTextWithHint("Generator", "<empty>", &assetExtended.asset.assetInfo->generator);
        imgui::widget::InputTextWithHint("Copyright", "<empty>", &assetExtended.asset.assetInfo->copyright);

        ImGui::SeparatorText("Extension Usage");

        imgui::widget::Table<false>(
            "asset-extensions-table",
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            assetExtended.asset.extensionsUsed,
            imgui::widget::TableColumnInfo { "Name", [](cpp_util::cstring_view name) {
                constexpr auto getVendorPath = [](std::string_view name) noexcept -> std::string_view {
                    if (name.starts_with("KHR_")) {
                        if (name == "KHR_materials_pbrSpecularGlossiness" ||
                            name == "KHR_techniques_webgl" ||
                            name == "KHR_xmp") {
                            return "Archived";
                        }

                        return "Khronos";
                    }

                    return "Vendor";
                };

                ImGui::AlignTextToFramePadding();
                ImGui::TextLinkOpenURL(name.c_str(), tempStringBuffer.write("https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/{}/{}", getVendorPath(name), name).view().c_str());
            }, ImGuiTableColumnFlags_WidthStretch },
            imgui::widget::TableColumnInfo { "Required", [&](std::size_t i, std::string_view name) {
                const auto it = std::ranges::find(assetExtended.asset.extensionsRequired, name);
                bool required = it != assetExtended.asset.extensionsRequired.end();
                if (ImGui::Checkbox(tempStringBuffer.write("##{}", i).view().c_str(), &required)) {
                    if (required) {
                        assetExtended.asset.extensionsRequired.emplace_back(name);
                    }
                    else {
                        assetExtended.asset.extensionsRequired.erase(it);
                    }
                }
            }, ImGuiTableColumnFlags_WidthFixed });
    }
    ImGui::End();

    if (ImGui::Begin("Buffers")) {
        imgui::widget::Table(
            "gltf-buffers-table",
            ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
            assetExtended.asset.buffers,
            imgui::widget::TableColumnInfo { "Name", [&](std::size_t bufferIndex, fastgltf::Buffer &buffer) {
                imgui::WithID(bufferIndex, [&]() {
                    ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                    imgui::widget::InputTextWithHint("##name", gui::getDisplayName(assetExtended.asset.buffers, bufferIndex), &buffer.name);
                });
            }, ImGuiTableColumnFlags_WidthStretch },
            imgui::widget::TableColumnInfo { "Size", [](const fastgltf::Buffer &buffer) {
                imgui::widget::TextUnformatted(tempStringBuffer.write(ByteSize { buffer.byteLength }));
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "MIME", [](const fastgltf::Buffer &buffer) {
                visit([](const auto &source) {
                    if constexpr (requires { { source.mimeType } -> std::convertible_to<fastgltf::MimeType>; }) {
                        imgui::widget::TextUnformatted(format_as(source.mimeType));
                    }
                    else {
                        ImGui::TextDisabled("-");
                    }
                }, buffer.data);
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Location", [&](std::size_t row, const fastgltf::Buffer &buffer) {
                visit(fastgltf::visitor {
                    [](const fastgltf::sources::Array&) {
                        imgui::widget::TextUnformatted("Embedded (Array)"sv);
                    },
                    [](const fastgltf::sources::BufferView &bufferView) {
                        imgui::widget::TextUnformatted(tempStringBuffer.write("BufferView ({})", bufferView.bufferViewIndex));
                    },
                    [&](const fastgltf::sources::URI &uri) {
                        imgui::WithID(row, [&]() {
                            ImGui::TextLinkOpenURL(ICON_FA_EXTERNAL_LINK, PATH_C_STR(assetExtended.directory / uri.uri.fspath()));
                        });
                    },
                    [](const auto&) {
                        ImGui::TextDisabled("-");
                    }
                }, buffer.data);
            }, ImGuiTableColumnFlags_WidthFixed });
    }
    ImGui::End();

    if (ImGui::Begin("Buffer Views")) {
        imgui::widget::TableWithVirtualization(
            "gltf-buffer-views-table",
            ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
            assetExtended.asset.bufferViews,
            imgui::widget::TableColumnInfo { "Name", [&](std::size_t bufferViewIndex, fastgltf::BufferView &bufferView) {
                imgui::WithID(bufferViewIndex, [&] {
                    ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                    imgui::widget::InputTextWithHint("##name", gui::getDisplayName(assetExtended.asset.bufferViews, bufferViewIndex), &bufferView.name);
                });
            }, ImGuiTableColumnFlags_WidthStretch },
            imgui::widget::TableColumnInfo { "Buffer", [](std::size_t i, const fastgltf::BufferView &bufferView) {
                ImGui::PushID(i);
                if (ImGui::TextLink(tempStringBuffer.write(bufferView.bufferIndex).view().c_str())) {
                    gui::makeWindowVisible(ImGui::FindWindowByName("Buffers"));
                }
                ImGui::PopID();
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Range", [](const fastgltf::BufferView &bufferView) {
                imgui::widget::TextUnformatted(tempStringBuffer.write(
                    "[{}, {}]", bufferView.byteOffset, bufferView.byteOffset + bufferView.byteLength));
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Size", [](const fastgltf::BufferView &bufferView) {
                imgui::widget::TextUnformatted(tempStringBuffer.write(ByteSize(bufferView.byteLength)));
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Stride", [](const fastgltf::BufferView &bufferView) {
                if (const auto &byteStride = bufferView.byteStride) {
                    imgui::widget::TextUnformatted(tempStringBuffer.write(*byteStride));
                }
                else {
                    ImGui::TextDisabled("-");
                }
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Target", [](const fastgltf::BufferView &bufferView) {
                if (const auto &bufferViewTarget = bufferView.target) {
                    imgui::widget::TextUnformatted(format_as(*bufferViewTarget));
                }
                else {
                    ImGui::TextDisabled("-");
                }
            }, ImGuiTableColumnFlags_WidthFixed });
    }
    ImGui::End();

    if (ImGui::Begin("Images")) {
        imgui::widget::TableWithVirtualization(
            "gltf-images-table",
            ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
            assetExtended.asset.images,
            imgui::widget::TableColumnInfo { "Name", [&](std::size_t imageIndex, fastgltf::Image &image) {
                imgui::WithID(imageIndex, [&] {
                    ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                    imgui::widget::InputTextWithHint("##name", gui::getDisplayName(assetExtended.asset.images, imageIndex), &image.name);
                });
            }, ImGuiTableColumnFlags_WidthStretch },
            imgui::widget::TableColumnInfo { "MIME", [](const fastgltf::Image &image) {
                visit([]<typename T>(const T &source) {
                    if constexpr (requires { { source.mimeType } -> std::convertible_to<fastgltf::MimeType>; }) {
                        if (source.mimeType != fastgltf::MimeType::None) {
                            imgui::widget::TextUnformatted(format_as(source.mimeType));
                            return;
                        }
                    }

                    if constexpr (std::same_as<T, fastgltf::sources::URI>) {
                        if (source.uri.isLocalPath()) {
                            const std::filesystem::path extension = source.uri.fspath().extension();
                            fastgltf::MimeType inferredMimeType = fastgltf::MimeType::None;
                            if (extension == ".jpg" || extension == ".jpeg") {
                                inferredMimeType = fastgltf::MimeType::JPEG;
                            }
                            else if (extension == ".png") {
                                inferredMimeType = fastgltf::MimeType::PNG;
                            }
                            else if (extension == ".ktx2") {
                                inferredMimeType = fastgltf::MimeType::KTX2;
                            }
                            else if (extension == ".dds") {
                                inferredMimeType = fastgltf::MimeType::DDS;
                            }
                            else if (extension == ".webp") {
                                inferredMimeType = fastgltf::MimeType::WEBP;
                            }

                            if (inferredMimeType != fastgltf::MimeType::None) {
                                imgui::widget::TextUnformatted(format_as(inferredMimeType));
                                ImGui::SameLine();
                                imgui::widget::HelperMarker("(inferred)", "MIME type is not presented in the glTF asset and is inferred from the file extension.");
                                return;
                            }
                        }
                    }

                    imgui::widget::TextUnformatted("-");
                }, image.data);
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Referenced Textures", [&](std::size_t i, const auto&) {
                imgui::WithID(i, [&] {
                    bool multi = false;
                    for (const auto &[textureIndex, texture] : assetExtended.asset.textures | ranges::views::enumerate) {
                        for (const auto &index : { texture.imageIndex, texture.basisuImageIndex, texture.ddsImageIndex, texture.webpImageIndex }) {
                            if (index == i) {
                                if (multi) {
                                    ImGui::SameLine();
                                }
                                if (ImGui::TextLink(tempStringBuffer.write(textureIndex).view().c_str())) {
                                    gui::makeWindowVisible(ImGui::FindWindowByName("Textures"));
                                }
                                multi = true;
                            }
                        }
                    }
                });
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Loaded", [&](std::size_t i, const auto&) {
                // TODO: do not disable this checkbox and load/unload the image on click.
                imgui::WithDisabled([&] {
                    imgui::WithID(i, [&] {
                        bool checked = assetExtended.isImageLoaded(i);
                        ImGui::Checkbox("", &checked);
                    });
                });
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Location", [&](std::size_t i, const fastgltf::Image &image) {
                visit(fastgltf::visitor {
                    [](const fastgltf::sources::Array&) {
                        imgui::widget::TextUnformatted("Embedded (Array)"sv);
                    },
                    [](const fastgltf::sources::BufferView &bufferView) {
                        imgui::widget::TextUnformatted(tempStringBuffer.write("BufferView ({})", bufferView.bufferViewIndex));
                    },
                    [&](const fastgltf::sources::URI &uri) {
                        imgui::WithID(i, [&]() {
                            ImGui::TextLinkOpenURL(ICON_FA_EXTERNAL_LINK, PATH_C_STR(assetExtended.directory / uri.uri.fspath()));
                        });
                    },
                    [](const auto&) {
                        ImGui::TextDisabled("-");
                    }
                }, image.data);
            }, ImGuiTableColumnFlags_WidthFixed });
    }
    ImGui::End();

    if (ImGui::Begin("Samplers")) {
        imgui::widget::Table(
            "gltf-samplers-table",
            ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
            assetExtended.asset.samplers,
            imgui::widget::TableColumnInfo { "Name", [&](std::size_t samplerIndex, fastgltf::Sampler &sampler) {
                imgui::WithID(samplerIndex, [&] {
                    ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                    imgui::widget::InputTextWithHint("##name", gui::getDisplayName(assetExtended.asset.samplers, samplerIndex), &sampler.name);
                });
            }, ImGuiTableColumnFlags_WidthStretch },
            imgui::widget::TableColumnInfo { "Filter (Mag/Min)", [](const fastgltf::Sampler &sampler) {
                imgui::widget::TextUnformatted(tempStringBuffer.write("{} / {}", to_optional(sampler.magFilter), to_optional(sampler.minFilter)));
            }, ImGuiTableColumnFlags_WidthFixed },
            imgui::widget::TableColumnInfo { "Wrap (S/T)", [](const fastgltf::Sampler &sampler) {
                imgui::widget::TextUnformatted(tempStringBuffer.write("{} / {}", sampler.wrapS, sampler.wrapT));
            }, ImGuiTableColumnFlags_WidthFixed });
    }
    ImGui::End();

    if (ImGui::Begin("Textures")) {
        const float windowVisibleX2 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (std::size_t textureIndex : ranges::views::upto(assetExtended.asset.textures.size())) {
            bool buttonClicked;
            const std::string_view label = gui::getDisplayName(assetExtended.asset.textures, textureIndex);
            imgui::WithID(textureIndex, [&] {
                constexpr ImVec2 buttonSize { 64, 64 };
                ImVec2 imageDisplaySize = assetExtended.getTextureSize(textureIndex);
                imageDisplaySize *= buttonSize / std::max(imageDisplaySize.x, imageDisplaySize.y);
                buttonClicked = imgui::widget::ImageButtonWithText("", assetExtended.getTextureID(textureIndex), label, buttonSize, imageDisplaySize, guiTextures);
            });
            if (ImGui::BeginItemTooltip()) {
                imgui::widget::TextUnformatted(label);
                ImGui::EndTooltip();
            }

            if (buttonClicked) {
                gui::popup::open(static_cast<std::string>(PopupNames::textureViewer), [this, &assetExtended, textureIndex, selectedImageIndex = getPreferredImageIndex(assetExtended.asset.textures[textureIndex])] {
                    const ImVec2 textureSize = assetExtended.getTextureSize(textureIndex);
                    const float displayRatio = std::max(textureSize.x, textureSize.y) / 256.f;
                    imgui::widget::HoverableImageCheckerboardBackground(assetExtended.getTextureID(textureIndex), textureSize / displayRatio, displayRatio, guiTextures);

                    ImGui::SameLine();

                    imgui::WithGroup([&] {
                        fastgltf::Texture &texture = assetExtended.asset.textures[textureIndex];
                        imgui::widget::InputTextWithHint("Name", gui::getDisplayName(assetExtended.asset.textures, textureIndex), &texture.name);
                        if (ImGui::BeginCombo("Image Index", tempStringBuffer.write(selectedImageIndex).view().c_str())) {
                            if (texture.imageIndex) {
                                // TODO: do not disable this and load the image when the selection is changed.
                                imgui::WithDisabled([&] {
                                    ImGui::Selectable(tempStringBuffer.write(*texture.imageIndex).view().c_str(), *texture.imageIndex == selectedImageIndex);
                                }, !assetExtended.isImageLoaded(*texture.imageIndex));
                            }
                            if (texture.basisuImageIndex) {
                                imgui::WithDisabled([&] {
                                    ImGui::Selectable(tempStringBuffer.write("{} (Basis Universal)", *texture.basisuImageIndex).view().c_str(), *texture.basisuImageIndex == selectedImageIndex);
                                }, !assetExtended.isImageLoaded(*texture.basisuImageIndex));
                            }
                            if (texture.ddsImageIndex) {
                                imgui::WithDisabled([&] {
                                    ImGui::Selectable(tempStringBuffer.write("{} (DDS)", *texture.ddsImageIndex).view().c_str(), *texture.ddsImageIndex == selectedImageIndex);
                                }/*, assetExtended.isImageLoaded(*texture.ddsImageIndex)*/ /* TODO: currently application does not handle this format at all */);
                            }
                            if (texture.webpImageIndex) {
                                imgui::WithDisabled([&] {
                                    if (ImGui::Selectable(tempStringBuffer.write("{} (WEBP)", *texture.webpImageIndex).view().c_str(), *texture.webpImageIndex == selectedImageIndex)) {
                                        // TODO
                                    }
                                }/*, assetExtended.isImageLoaded(*texture.webpImageIndex)*/ /* TODO: currently application does not handle this format at all */);
                            }

                            ImGui::EndCombo();
                        }
                        if (texture.samplerIndex) {
                            ImGui::LabelText("Sampler Index", "%zu", texture.samplerIndex.value_or(-1));
                        }
                        else {
                            imgui::WithLabel("Sampler Index", [] {
                                ImGui::TextDisabled("-");
                                ImGui::SameLine();
                                imgui::widget::HelperMarker("(?)", "Default sampler will be used.");
                            });
                        }

                        ImGui::SeparatorText("Texture used by:");

                        imgui::widget::Table<false>(
                            "",
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                            assetExtended.textureUsages[textureIndex],
                            imgui::widget::TableColumnInfo { "Material", decomposer([&](std::size_t materialIndex, auto) {
                                imgui::WithID(materialIndex, [&] {
                                    if (ImGui::TextLink(gui::getDisplayName(assetExtended.asset.materials, materialIndex).c_str())) {
                                        gui::makeWindowVisible(ImGui::FindWindowByName("Material Editor"));
                                        assetExtended.imGuiSelectedMaterialIndex.emplace(materialIndex);
                                        ImGui::CloseCurrentPopup();
                                    }
                                });
                            }), ImGuiTableColumnFlags_WidthFixed },
                            imgui::widget::TableColumnInfo { "Type", decomposer([](auto, Flags<fastgltf::TextureUsage> type) {
                                imgui::widget::TextUnformatted(tempStringBuffer.write("{::s}", type).view());
                            }), ImGuiTableColumnFlags_WidthStretch });
                    });

                });
            }

            const float lastButtonX2 = ImGui::GetItemRectMax().x;
            const float nextButtonX2 = lastButtonX2 + ImGui::GetStyle().ItemSpacing.x + 64;
            if (textureIndex + 1 < assetExtended.asset.textures.size() && nextButtonX2 < windowVisibleX2) {
                ImGui::SameLine();
            }
        }

        gui::popup::process(PopupNames::textureViewer);
    }
    ImGui::End();

    assetInspectorCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::materialEditor(gltf::AssetExtended &assetExtended) {
    if (ImGui::Begin("Material Editor")) {
        const char* previewText;
        if (assetExtended.asset.materials.empty()) {
            previewText = "<empty>";
        }
        else if (assetExtended.imGuiSelectedMaterialIndex) {
            previewText = gui::getDisplayName(assetExtended.asset.materials, *assetExtended.imGuiSelectedMaterialIndex).c_str();
        }
        else {
            previewText = "<select...>";
        }

        if (ImGui::BeginCombo("Material", previewText)) {
            for (std::size_t i : ranges::views::upto(assetExtended.asset.materials.size())) {
                const bool isSelected = i == assetExtended.imGuiSelectedMaterialIndex;
                imgui::WithID(i, [&]() {
                    if (ImGui::Selectable(gui::getDisplayName(assetExtended.asset.materials, i).c_str(), isSelected)) {
                        assetExtended.imGuiSelectedMaterialIndex.emplace(i);
                    }
                });
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }
        if (const auto &i = assetExtended.imGuiSelectedMaterialIndex) {
            ImGui::SameLine();
            if (ImGui::Button("Rename...")) {
                gui::popup::open(static_cast<std::string>(PopupNames::renameMaterial), [hintText = std::string { gui::getDisplayName(assetExtended.asset.materials, *i) }, &target = assetExtended.asset.materials[*i].name] {
                    imgui::widget::InputTextWithHint("Name", hintText, &target);
                });
            }
            gui::popup::process(PopupNames::renameMaterial);
        }

        if (const auto &selectedMaterialIndex = assetExtended.imGuiSelectedMaterialIndex) {
            fastgltf::Material &material = assetExtended.asset.materials[*selectedMaterialIndex];
            const auto notifyPropertyChanged = [&](task::MaterialPropertyChanged::Property property) {
                tasks.emplace(std::in_place_type<task::MaterialPropertyChanged>, *selectedMaterialIndex, property);
            };

            if (ImGui::Checkbox("Double sided", &material.doubleSided)) {
                notifyPropertyChanged(task::MaterialPropertyChanged::DoubleSided);
            }

            if (ImGui::Checkbox("KHR_materials_unlit", &material.unlit)) {
                notifyPropertyChanged(task::MaterialPropertyChanged::Unlit);
            }

            if (int alphaMode = static_cast<int>(material.alphaMode);
                ImGui::Combo("Alpha mode", &alphaMode, "OPAQUE\0MASK\0BLEND\0")) {
                material.alphaMode = static_cast<fastgltf::AlphaMode>(alphaMode);
                notifyPropertyChanged(task::MaterialPropertyChanged::AlphaMode);
            }

            imgui::WithDisabled([&]() {
                if (ImGui::DragFloat("Alpha cutoff", &material.alphaCutoff, 0.01f, 0.f, 1.f)) {
                    notifyPropertyChanged(task::MaterialPropertyChanged::AlphaCutoff);
                }
            }, material.alphaMode != fastgltf::AlphaMode::Mask);

            constexpr auto texcoordOverriddenMarker = []() {
                ImGui::SameLine();
                imgui::widget::HelperMarker("(overridden)", "This value is overridden by KHR_texture_transform extension.");
            };
            const auto textureTransformControl = [&](fastgltf::TextureInfo &textureInfo, fastgltf::TextureUsage usage) -> void {
                task::MaterialPropertyChanged::Property changeProp;
                switch (usage) {
                    using enum task::MaterialPropertyChanged::Property;
                    case fastgltf::TextureUsage::BaseColor:
                        changeProp = BaseColorTextureTransform;
                        break;
                    case fastgltf::TextureUsage::MetallicRoughness:
                        changeProp = MetallicRoughnessTextureTransform;
                        break;
                    case fastgltf::TextureUsage::Normal:
                        changeProp = NormalTextureTransform;
                        break;
                    case fastgltf::TextureUsage::Occlusion:
                        changeProp = OcclusionTextureTransform;
                        break;
                    case fastgltf::TextureUsage::Emissive:
                        changeProp = EmissiveTextureTransform;
                        break;
                    default:
                        std::unreachable();
                }

                bool useTextureTransform = textureInfo.transform != nullptr;
                bool isChangePropNotified = false; // prevent double notifying
                if (ImGui::Checkbox("KHR_texture_transform", &useTextureTransform)) {
                    if (useTextureTransform) {
                        textureInfo.transform = std::make_unique<fastgltf::TextureTransform>();
                        const bool emplaced [[maybe_unused]] = assetExtended.transformedTextureInfos.emplace(&textureInfo).second;
                        assert(emplaced);
                    }
                    else {
                        textureInfo.transform.reset();
                        const bool erased [[maybe_unused]] = assetExtended.transformedTextureInfos.erase(&textureInfo) == 1;
                        assert(erased);
                    }
                    notifyPropertyChanged(task::MaterialPropertyChanged::Property::TextureTransformEnabled);

                    notifyPropertyChanged(changeProp);
                    isChangePropNotified = true;
                }

                if (useTextureTransform) {
                    fastgltf::TextureTransform* pTransform = textureInfo.transform.get();
                    if (!pTransform) {
                        static fastgltf::TextureTransform dummyTextureTransform; // avoid null dereference
                        pTransform = &dummyTextureTransform;
                    }

                    bool transformChanged = ImGui::DragFloat2("Scale", pTransform->uvScale.data(), 0.01f);
                    transformChanged |= ImGui::DragFloat("Rotation", &pTransform->rotation, 0.01f);
                    transformChanged |= ImGui::DragFloat2("Offset", pTransform->uvOffset.data(), 0.01f);

                    if (transformChanged && !isChangePropNotified) {
                        notifyPropertyChanged(changeProp);
                    }
                }
            };

            if (ImGui::CollapsingHeader("Physically Based Rendering")) {
                ImGui::SeparatorText("Base Color");
                imgui::WithID("basecolor", [&]() {
                    auto &baseColorTextureInfo = material.pbrData.baseColorTexture;
                    if (baseColorTextureInfo) {
                        imgui::WithGroup([&] {
                            const ImVec2 textureSize = assetExtended.getTextureSize(baseColorTextureInfo->textureIndex);
                            const float displayRatio = std::max(textureSize.x, textureSize.y) / 128.f;
                            imgui::widget::HoverableImageCheckerboardBackground(assetExtended.getTextureID(baseColorTextureInfo->textureIndex), textureSize / displayRatio, displayRatio, guiTextures);
                        });
                        ImGui::SameLine();
                    }
                    imgui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                        imgui::WithGroup([&]() {
                            if (ImGui::DragFloat4("Factor", material.pbrData.baseColorFactor.data(), 0.01f, 0.f, 1.f)) {
                                notifyPropertyChanged(task::MaterialPropertyChanged::BaseColorFactor);
                            }
                            if (baseColorTextureInfo) {
                                ImGui::LabelText("Texture Index", "%zu", baseColorTextureInfo->textureIndex);

                                const std::size_t texcoordIndex = getTexcoordIndex(*baseColorTextureInfo);
                                ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                if (texcoordIndex != baseColorTextureInfo->texCoordIndex) {
                                    texcoordOverriddenMarker();
                                }

                                textureTransformControl(*baseColorTextureInfo, fastgltf::TextureUsage::BaseColor);
                            }
                        }, baseColorTextureInfo.has_value());
                    });
                });

                imgui::WithDisabled([&] {
                    ImGui::SeparatorText("Metallic/Roughness");
                    imgui::WithID("metallicroughness", [&] {
                        auto &metallicRoughnessTextureInfo = material.pbrData.metallicRoughnessTexture;
                        if (metallicRoughnessTextureInfo) {
                            imgui::WithGroup([&] {
                                const ImVec2 textureSize = assetExtended.getTextureSize(metallicRoughnessTextureInfo->textureIndex);
                                const float displayRatio = std::max(textureSize.x, textureSize.y) / 128.f;
                                imgui::widget::HoverableImage(assetExtended.getMetallicTextureID(*selectedMaterialIndex), textureSize / displayRatio, displayRatio);
                                imgui::widget::HoverableImage(assetExtended.getRoughnessTextureID(*selectedMaterialIndex), textureSize / displayRatio, displayRatio);
                            });
                            ImGui::SameLine();
                        }
                        imgui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            imgui::WithGroup([&]() {
                                if (ImGui::DragFloat("Metallic Factor", &material.pbrData.metallicFactor, 0.01f, 0.f, 1.f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::MetallicFactor);
                                }
                                if (ImGui::DragFloat("Roughness Factor", &material.pbrData.roughnessFactor, 0.01f, 0.f, 1.f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::RoughnessFactor);
                                }
                                if (metallicRoughnessTextureInfo) {
                                    ImGui::LabelText("Texture Index", "%zu", metallicRoughnessTextureInfo->textureIndex);

                                    const std::size_t texcoordIndex = getTexcoordIndex(*metallicRoughnessTextureInfo);
                                    ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                    if (texcoordIndex != metallicRoughnessTextureInfo->texCoordIndex) {
                                        texcoordOverriddenMarker();
                                    }

                                    textureTransformControl(*metallicRoughnessTextureInfo, fastgltf::TextureUsage::MetallicRoughness);
                                }
                            });
                        });
                    });
                }, material.unlit);
            }

            imgui::WithID("normal", [&]() {
                if (auto &textureInfo = material.normalTexture; textureInfo && ImGui::CollapsingHeader("Normal Mapping")) {
                    imgui::WithDisabled([&] {
                        imgui::WithGroup([&] {
                            const ImVec2 textureSize = assetExtended.getTextureSize(textureInfo->textureIndex);
                            const float displayRatio = std::max(textureSize.x, textureSize.y) / 128.f;
                            imgui::widget::HoverableImage(assetExtended.getNormalTextureID(*selectedMaterialIndex), textureSize / displayRatio, displayRatio);
                        });
                        ImGui::SameLine();
                        imgui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            imgui::WithGroup([&]() {
                                if (ImGui::DragFloat("Scale", &textureInfo->scale, 0.01f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::NormalScale);
                                }
                                ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);

                                const std::size_t texcoordIndex = getTexcoordIndex(*textureInfo);
                                ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                if (texcoordIndex != textureInfo->texCoordIndex) {
                                    texcoordOverriddenMarker();
                                }

                                textureTransformControl(*textureInfo, fastgltf::TextureUsage::Normal);
                            });
                        });
                    }, material.unlit);
                }
            });

            imgui::WithID("occlusion", [&]() {
                if (auto &textureInfo = material.occlusionTexture; textureInfo && ImGui::CollapsingHeader("Occlusion Mapping")) {
                    imgui::WithDisabled([&] {
                        imgui::WithGroup([&] {
                            const ImVec2 textureSize = assetExtended.getTextureSize(textureInfo->textureIndex);
                            const float displayRatio = std::max(textureSize.x, textureSize.y) / 128.f;
                            imgui::widget::HoverableImage(assetExtended.getOcclusionTextureID(*selectedMaterialIndex), textureSize / displayRatio, displayRatio);
                        });
                        ImGui::SameLine();
                        imgui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            imgui::WithGroup([&]() {
                                if (ImGui::DragFloat("Strength", &textureInfo->strength, 0.01f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::OcclusionStrength);
                                }
                                ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);

                                const std::size_t texcoordIndex = getTexcoordIndex(*textureInfo);
                                ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                if (texcoordIndex != textureInfo->texCoordIndex) {
                                    texcoordOverriddenMarker();
                                }

                                textureTransformControl(*textureInfo, fastgltf::TextureUsage::Occlusion);
                            });
                        });
                    }, material.unlit);
                }
            });

            imgui::WithID("emissive", [&]() {
                if (ImGui::CollapsingHeader("Emissive")) {
                    imgui::WithDisabled([&]() {
                        auto &textureInfo = material.emissiveTexture;
                        if (textureInfo) {
                            imgui::WithGroup([&] {
                                const ImVec2 textureSize = assetExtended.getTextureSize(textureInfo->textureIndex);
                                const float displayRatio = std::max(textureSize.x, textureSize.y) / 128.f;
                                imgui::widget::HoverableImage(assetExtended.getEmissiveTextureID(*selectedMaterialIndex), textureSize / displayRatio, displayRatio);
                            });
                            ImGui::SameLine();
                        }
                        imgui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            imgui::WithGroup([&]() {
                                if (ImGui::DragFloat3("Factor", material.emissiveFactor.data(), 0.01f, 0.f, 1.f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::Emissive);
                                }
                                if (ImGui::DragFloat("Emissive Strength", &material.emissiveStrength, 1.f, 1.f, std::numeric_limits<float>::max())) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::EmissiveStrength);
                                }
                                if (textureInfo) {
                                    ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);

                                    const std::size_t texcoordIndex = getTexcoordIndex(*textureInfo);
                                    ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                    if (texcoordIndex != textureInfo->texCoordIndex) {
                                        texcoordOverriddenMarker();
                                    }

                                    textureTransformControl(*textureInfo, fastgltf::TextureUsage::Emissive);
                                }
                            }, textureInfo.has_value());
                        });
                    }, material.unlit);
                }
            });

            if (ImGui::CollapsingHeader("KHR_materials_ior")) {
                imgui::WithDisabled([&]() {
                    if (ImGui::DragFloat("Index of Refraction", &material.ior, 1e-2f, 1.f, std::numeric_limits<float>::max())) {
                        notifyPropertyChanged(task::MaterialPropertyChanged::Ior);
                    }
                }, material.unlit);
            }
        }
    }
    ImGui::End();

    materialEditorCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::materialVariants(gltf::AssetExtended &assetExtended) {
    if (ImGui::Begin("Material Variants")) {
        int selected = -1;
        if (assetExtended.imGuiSelectedMaterialVariantsIndex) {
            selected = static_cast<int>(*assetExtended.imGuiSelectedMaterialVariantsIndex);
        }

        for (const auto &[variantIndex, variantName] : assetExtended.asset.materialVariants | ranges::views::enumerate) {
            if (ImGui::RadioButton(variantName.c_str(), &selected, variantIndex)) {
                // Apply material variants.
                for (fastgltf::Mesh &mesh : assetExtended.asset.meshes) {
                    for (fastgltf::Primitive &primitive : mesh.primitives) {
                        if (primitive.mappings.empty()) {
                            // Primitive is not affected by KHR_materials_variants.
                            continue;
                        }

                        if (const auto &variantMaterialIndex = primitive.mappings.at(variantIndex)) {
                            primitive.materialIndex.emplace(*variantMaterialIndex);
                        }
                        else {
                            const auto &originalMaterialIndex = assetExtended.originalMaterialIndexByPrimitive.at(&primitive);
                            primitive.materialIndex.emplace(originalMaterialIndex.value());
                        }

                        tasks.emplace(std::in_place_type<task::PrimitiveMaterialChanged>, &primitive);
                    }
                }
                assetExtended.imGuiSelectedMaterialVariantsIndex.emplace(selected);
            }
        }
    }
    ImGui::End();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::sceneHierarchy(Renderer &renderer, gltf::AssetExtended &assetExtended) {
    if (ImGui::Begin("Scene Hierarchy")) {
        if (ImGui::BeginCombo("Scene", gui::getDisplayName(assetExtended.asset.scenes, assetExtended.sceneIndex).c_str())) {
            for (std::size_t i : ranges::views::upto(assetExtended.asset.scenes.size())) {
                const bool isSelected = i == assetExtended.sceneIndex;
                if (ImGui::Selectable(gui::getDisplayName(assetExtended.asset.scenes, i).c_str(), isSelected)) {
                    tasks.emplace(std::in_place_type<task::ChangeScene>, i);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Rename...")) {
            gui::popup::open(static_cast<std::string>(PopupNames::renameScene), [hintText = std::string { gui::getDisplayName(assetExtended.asset.scenes, assetExtended.sceneIndex) }, &target = assetExtended.asset.scenes[assetExtended.sceneIndex].name] {
                imgui::widget::InputTextWithHint("Name", hintText, &target);
            });
        }

        gui::popup::process(PopupNames::renameScene);

        // Searching nodes with name.
        if (std::pair userData { assetExtended.nodeNameSearchText.size() /* nodeNameSearchTextBeforeSize */, false /* insertedAtBeginOrEnd */ };
            imgui::widget::InputTextWithHint("Search", "Search node by name...", &assetExtended.nodeNameSearchText, ImGuiInputTextFlags_CallbackEdit, [](ImGuiInputTextCallbackData *data) {
                auto &[nodeNameSearchTextBeforeSize, insertedAtBeginOrEnd] = *static_cast<decltype(userData)*>(data->UserData);
                insertedAtBeginOrEnd = data->BufTextLen > nodeNameSearchTextBeforeSize // Text is inserted
                    && (data->CursorPos == data->BufTextLen // at the end of the text, or
                        || nodeNameSearchTextBeforeSize + data->CursorPos == data->BufTextLen); // at the beginning of the text.
                return 0;
            }, &userData)) {
            assetExtended.updateNodeNameSearchTextOccurrencePosByNode(userData.first != 0 && userData.second);
        }

        static bool mergeSingleChildNodes = true;
        ImGui::Checkbox("Merge single child nodes", &mergeSingleChildNodes);
        ImGui::SameLine();
        imgui::widget::HelperMarker("(?)", "If a node has only single child and both are not representing any mesh, light or camera, they will be combined and slash-separated name will be shown instead.");

        // Node rendered by ImGui::TreeNode, including merged node indices when merging single child node is enabled.
        // Will last until the next frame's ImGui::BeginTable() call and cleared at the beginning of that.
        static std::vector<std::size_t> renderedNodes;

        // Passed as parameter with ImGui::IsRectVisible() call (see below).
        const ImVec2 treeNodeVisibilityTestSize { 1.f, ImGui::GetFontSize() + 2 * ImGui::GetStyle().FramePadding.y };

        const auto addChildNode = [&](this const auto &self, std::size_t nodeIndex) -> void {
            boost::container::small_vector<std::size_t, 4> mergedNodeIndices;
            if (mergeSingleChildNodes) {
                for (const fastgltf::Node *node = &assetExtended.asset.nodes[nodeIndex];
                    node->children.size() == 1
                        && !node->cameraIndex && !node->lightIndex && !node->meshIndex
                        && !assetExtended.asset.nodes[node->children[0]].cameraIndex && !assetExtended.asset.nodes[node->children[0]].lightIndex && !assetExtended.asset.nodes[node->children[0]].meshIndex
                        && (assetExtended.nodeNameSearchText.empty() || assetExtended.nodeNameSearchTextOccurrencePosByNode.contains(node->children[0]));
                    nodeIndex = node->children[0], node = &assetExtended.asset.nodes[nodeIndex]) {
                    mergedNodeIndices.push_back(nodeIndex);
                }
            }
            mergedNodeIndices.push_back(nodeIndex);

            // If node name search text is nonempty, hide the node when there's no occurrence in the node.
            if (!assetExtended.nodeNameSearchText.empty() && !assetExtended.nodeNameSearchTextOccurrencePosByNode.contains(nodeIndex)) return;

            const fastgltf::Node &node = assetExtended.asset.nodes[nodeIndex];

            // --------------------
            // TreeNode.
            // --------------------

            ImGui::TableNextRow();
            ImGui::TableNextColumn();

            // Make treenode label with node names (or placeholder text) and calculate the node name search text
            // occurrence positions (if the search text is nonempty).
            // For the optimization, this is only done when the tree node is actually visible, tested by
            // ImGui::IsRectVisible(treeNodeVisibilityTestSize) from the current cursor position. For invisible
            // tree node, empty label (but it still has ID of node index to maintain the tree node state) is used.
            tempStringBuffer.clear();
            const char *labelStart = tempStringBuffer.view().data();
            if (ImGui::IsRectVisible(treeNodeVisibilityTestSize)) {
                std::vector<std::size_t> searchTextOccurrencePosInLabel;
                const auto appendNodeLabel = [&](std::size_t nodeIndex) {
                    if (!assetExtended.nodeNameSearchText.empty()) {
                        // Check if the node name has an occurrence of the search text
                        const auto it = assetExtended.nodeNameSearchTextOccurrencePosByNode.find(nodeIndex);
                        if (it != assetExtended.nodeNameSearchTextOccurrencePosByNode.end() &&
                            it->second != assetExtended.asset.nodes[nodeIndex].name.size() /* check if really occurs */) {
                            searchTextOccurrencePosInLabel.push_back(tempStringBuffer.view().size() + it->second);
                        }
                    }

                    const fastgltf::Node &node = assetExtended.asset.nodes[nodeIndex];
                    if (std::string_view name = node.name; name.empty()) {
                        tempStringBuffer.append("Unnamed Node {}", nodeIndex);
                    }
                    else {
                        tempStringBuffer.append(name);
                    }
                };
                for (bool first = true; std::size_t nodeIndex : mergedNodeIndices) {
                    if (first) {
                        first = false;
                    }
                    else {
                        tempStringBuffer.append(" / ");
                    }
                    appendNodeLabel(nodeIndex);
                }

                // (Avail width) = ImGui::GetContentRegionAvail().x - (space occupied by collapsing arrow)
                // Collapsing arrow space calculation code is adapted from
                //   bool ImGui::TreeNodeBehavior(ImGuiID id, ImGuiTreeNodeFlags flags, const char* label, const char* label_end)
                // implementation.
                const float collapsingArrowWidth = ImGui::GetFontSize() + 2 * ImGui::GetStyle().FramePadding.x;
                const float width = ImGui::GetContentRegionAvail().x - collapsingArrowWidth;

                // Truncate text if it is too long to fit in the available width. Left text will be truncated and
                // replaced with ellipsis.
                if (tempStringBuffer.view().size() >= 3 && imgui::CalcTextSize(tempStringBuffer.view()).x > width) {
                    std::span truncated = imgui::EllipsisLeft(tempStringBuffer.mut_view(), width, "...");

                    char* const start = std::max(tempStringBuffer.mut_view().data(), truncated.data() - 3);
                    std::fill(start, truncated.data(), '.');

                    labelStart = start;
                }

                if (!searchTextOccurrencePosInLabel.empty()) {
                    const ImVec2 highlightOffsetBase = ImGui::GetCursorScreenPos() + ImVec2 { collapsingArrowWidth, ImGui::GetStyle().FramePadding.y + 1 /* TODO */ };
                    for (std::size_t occurrencePos : searchTextOccurrencePosInLabel) {
                        const char *occurrenceStart = &tempStringBuffer.view()[occurrencePos];
                        const char* const occurrenceEnd = occurrenceStart + assetExtended.nodeNameSearchText.size();

                        if (occurrenceEnd <= labelStart) continue; // The occurrence is fully truncated.

                        // If the occurrence is partially truncated, find the truncation point (replaced by the ellipsis).
                        // The point can be found by first right-to-left mismatch of [labelStart, occurrenceEnd) and
                        // nodeNameSearchText.
                        // occurrenceStart will be the right next of the mismatch point.
                        occurrenceStart = &*--std::ranges::mismatch(
                            std::ranges::subrange(labelStart, occurrenceEnd) | std::views::reverse,
                            assetExtended.nodeNameSearchText | std::views::reverse,
                            {}, LIFT(std::tolower), LIFT(std::tolower)).in1;

                        // Mismatch may happen at the first element of twos, which means the occurrence is replaced by
                        // the ellipsis. Should be rejected.
                        if (occurrenceStart >= occurrenceEnd) continue;

                        ImVec2 highlightOffset = highlightOffsetBase;
                        highlightOffset.x += ImGui::CalcTextSize(labelStart, occurrenceStart).x;
                        const ImVec2 highlightSize = ImGui::CalcTextSize(occurrenceStart, occurrenceEnd);
                        ImGui::GetWindowDrawList()->AddRectFilled(highlightOffset, highlightOffset + highlightSize, 0xFF00AABB);
                    }
                }
            }

            // Label passed to ImGui::TreeNodeEx() can be changed by truncation. To maintain the opened state
            // of the tree node, its ID must be stable. For the purpose, use ### to separate the label and ID.
            tempStringBuffer.append("###{}", nodeIndex);

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_DrawLinesToNodes | ImGuiTreeNodeFlags_FramePadding;

            bool isNodeSelected = std::ranges::all_of(mergedNodeIndices, LIFT(assetExtended.selectedNodes.contains));
            if (isNodeSelected) flags |= ImGuiTreeNodeFlags_Selected;

            if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_Leaf;

            // When merging single child node is enabled, a single ImGui::TreeNode() represents all nodes in
            // mergedNodeIndices. ImGuiSelectionRequest::Range{First,Last}Item needed to be set as
            //   (mergedNodeIndices.front(), mergedNodeIndices.back())
            // and selection range will be (RangeFirstItem.first, RangeLastItem.second).
            // As ImGuiSelectionUserData is 8-byte data type, the pair can be stored with integer bit-casting.
            ImGui::SetNextItemSelectionUserData(std::bit_cast<ImGuiSelectionUserData>((mergedNodeIndices.front() << 32) | mergedNodeIndices.back()));
            const bool isTreeNodeOpen = ImGui::TreeNodeEx(labelStart, flags);

            renderedNodes.append_range(mergedNodeIndices);

            // Handle hovering tree node.
            if (ImGui::IsItemHovered() && nodeIndex != assetExtended.hoveringNode) {
                tasks.emplace(std::in_place_type<task::HoverNodeFromGui>, nodeIndex);
            }

            // Open context menu when right-click the tree node.
            if (ImGui::BeginPopupContextItem()) {
                const auto nodeContextMenu = [&](std::size_t nodeIndex) {
                    imgui::WithStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), [&] {
                        ImGui::Text("#%zu", nodeIndex);
                    });
                    ImGui::SameLine();
                    imgui::widget::TextUnformatted(assetExtended.asset.nodes[nodeIndex].name);

                    ImGui::Separator();

                    // If the current node is the only selected node, and it is leaf, it is guaranteed that the selection will be not changed.
                    imgui::WithDisabled([&]() {
                        if (ImGui::Selectable("Select from here")) {
                            assetExtended.selectedNodes.clear();
                            traverseNode(assetExtended.asset, nodeIndex, [&](std::size_t nodeIndex) {
                                assetExtended.selectedNodes.emplace(nodeIndex);
                            });

                            tasks.emplace(std::in_place_type<task::NodeSelectionChanged>);
                        }
                    }, assetExtended.selectedNodes.size() == 1 && isNodeSelected && node.children.empty());

                    // Fit camera to the node miniball
                    const auto isMeshlessRecursive = [&](std::size_t nodeIndex) noexcept {
                        // Return true if neither the given node nor the descendants have a mesh, false otherwise.
                        return assetExtended.sceneHierarchy.getVisibilityState(nodeIndex) == gltf::SceneHierarchy::VisibilityState::Indeterminate;
                    };
                    const bool currentNodeHasMeshRecursive = !isMeshlessRecursive(nodeIndex);
                    const bool selectedNodesHaveMeshRecursive
                        = (currentNodeHasMeshRecursive && isNodeSelected) // Use short circuit evaluation to avoid calling isMeshlessRecursive() for all selected nodes
                        || !std::ranges::none_of(assetExtended.selectedNodes, isMeshlessRecursive);
                    if (currentNodeHasMeshRecursive || selectedNodesHaveMeshRecursive) {
                        ImGui::Separator();

                        std::vector<std::size_t> nodeIndicesToFit;

                        // ImGui::Selectable() closes popup by default, but if there are multiple cameras, popup has to
                        // be opened inside the ImGui::BeginPopup() - ImGui::EndPopup() call. Therefore,
                        // ImGuiSelectableFlags_NoAutoClosePopups flag should be used.
                        const ImGuiSelectableFlags flags = (renderer.cameras.size() > 1) ? ImGuiSelectableFlags_NoAutoClosePopups : 0;

                        if (currentNodeHasMeshRecursive && ImGui::Selectable("Fit camera to this", false, flags)) {
                            nodeIndicesToFit.push_back(nodeIndex);
                        }

                        if (selectedNodesHaveMeshRecursive && ImGui::Selectable("Fit camera to the selection", false, flags)) {
                            nodeIndicesToFit.append_range(assetExtended.selectedNodes);

                            // Need to filter the selected nodes.
                            // 1. Leave only the nodes which are not descendants of other nodes in the list.
                            assetExtended.sceneHierarchy.pruneDescendantNodesInPlace(nodeIndicesToFit);

                            // 2. Remove meshless nodes.
                            const auto [begin, end] = std::ranges::remove_if(nodeIndicesToFit, isMeshlessRecursive);
                            nodeIndicesToFit.erase(begin, end);
                        }

                        if (!nodeIndicesToFit.empty()) {
                            auto fitCameraToNodeMiniball = [
                                currentNodeMiniball = gltf::algorithm::getMiniball<false>(assetExtended.asset, std::move(nodeIndicesToFit), assetExtended.sceneHierarchy.getWorldTransforms(), assetExtended.externalBuffers),
                                &cameraOrLightPoints = get<2>(assetExtended.sceneMiniball.get())
                            ](Camera &camera) {
                                const auto &[center, radius] = currentNodeMiniball;
                                const float distance = radius / std::sin(camera.fov / 2.f);
                                camera.position = glm::make_vec3(center.data()) - distance * normalize(camera.direction);
                                camera.targetDistance = distance;

                                camera.tightenNearFar(glm::make_vec3(center.data()), radius, { reinterpret_cast<const glm::vec3*>(cameraOrLightPoints.data()), cameraOrLightPoints.size() });
                            };

                            if (renderer.cameras.size() > 1) {
                                // If multiple viewports, show a popup to select which viewport to apply.
                                gui::popup::open(std::string { PopupNames::selectViewportToApplyNodeFocus }, [&renderer, this, MOVE_CAP(fitCameraToNodeMiniball), oldCameras = boost::container::static_vector<std::optional<Camera>, 4>(renderer.cameras.size())] mutable {
                                    imgui::widget::TextUnformatted(PopupNames::selectViewportToApplyNodeFocus);

                                    // Calculate button size whose aspect ratio is same as the sub-viewports.
                                    ImVec2 buttonSize;
                                    buttonSize.x = ImGui::CalcItemWidth();
                                    if (renderer.cameras.size() >= 2) {
                                        // Make 2 buttons in a row.
                                        buttonSize.x = buttonSize.x / 2.f - ImGui::GetStyle().ItemInnerSpacing.x;
                                    }
                                    buttonSize.y = buttonSize.x * centerNodeRect.GetHeight() / centerNodeRect.GetWidth();
                                    if (renderer.cameras.size() == 2) {
                                        buttonSize.y *= 2;
                                    }

                                    for (auto &&[cameraIndex, camera] : renderer.cameras | ranges::views::enumerate) {
                                        if (cameraIndex % 2 == 1) {
                                            ImGui::SameLine();
                                        }

                                        auto &oldCamera = oldCameras[cameraIndex];
                                        // Mimic toggle button: button will have ImGuiCol_ButtonActive color when the camera is adjusted to fit the node miniball.
                                        imgui::WithStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive), [&] {
                                            if (ImGui::Button(tempStringBuffer.write("Viewport {}", cameraIndex).view().c_str(), buttonSize)) {
                                                if (oldCamera) {
                                                    // Revert the camera to the old one (which is saved in oldCameras).
                                                    camera = *oldCamera;
                                                    oldCamera.reset();
                                                }
                                                else {
                                                    // Save the current camera to oldCameras and adjust current camera
                                                    // to fit the node's miniball.
                                                    oldCamera.emplace(camera);
                                                    fitCameraToNodeMiniball(camera);
                                                }
                                            }
                                        }, oldCamera.has_value());
                                    }
                                });
                            }
                            else {
                                fitCameraToNodeMiniball(renderer.cameras[0]);
                            }
                        }

                        gui::popup::process(PopupNames::selectViewportToApplyNodeFocus);
                    }

                    if (isNodeSelected) {
                        ImGui::Separator();

                        // If node is the only selected node, visibility can be determined in a constant time.
                        std::optional<bool> determinedVisibility{};
                        if (assetExtended.selectedNodes.size() == 1) {
                            determinedVisibility.emplace(assetExtended.sceneHierarchy.getVisibility(nodeIndex));
                        }

                        // If visibility is hidden or cannot be determined, show the menu.
                        if (!determinedVisibility.value_or(false) && ImGui::Selectable("Make all selected nodes visible")) {
                            for (std::size_t nodeIndex : assetExtended.selectedNodes) {
                                if (!assetExtended.asset.nodes[nodeIndex].meshIndex) continue;

                                if (!assetExtended.sceneHierarchy.getVisibility(nodeIndex)) {
                                    assetExtended.sceneHierarchy.setVisibility(nodeIndex, true);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            }
                        }

                        // If visibility is visible or cannot be determined, show the menu.
                        if (determinedVisibility.value_or(true) && ImGui::Selectable("Make all selected nodes invisible")) {
                            for (std::size_t nodeIndex : assetExtended.selectedNodes) {
                                if (!assetExtended.asset.nodes[nodeIndex].meshIndex) continue;

                                if (assetExtended.sceneHierarchy.getVisibility(nodeIndex)) {
                                    assetExtended.sceneHierarchy.setVisibility(nodeIndex, false);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            }
                        }

                        if (!determinedVisibility && ImGui::Selectable("Toggle all selected node visibility")) {
                            for (std::size_t nodeIndex : assetExtended.selectedNodes) {
                                if (!assetExtended.asset.nodes[nodeIndex].meshIndex) continue;

                                assetExtended.sceneHierarchy.flipVisibility(nodeIndex);
                                tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                            }
                        }
                    }
                    else if (auto state = assetExtended.sceneHierarchy.getVisibilityState(nodeIndex); state != gltf::SceneHierarchy::VisibilityState::Indeterminate) {
                        ImGui::Separator();

                        if (state != gltf::SceneHierarchy::VisibilityState::AllVisible && ImGui::Selectable("Make visible from here")) {
                            traverseNode(assetExtended.asset, nodeIndex, [&](std::size_t nodeIndex) {
                                if (!assetExtended.asset.nodes[nodeIndex].meshIndex) return;

                                if (!assetExtended.sceneHierarchy.getVisibility(nodeIndex)) {
                                    assetExtended.sceneHierarchy.setVisibility(nodeIndex, true);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            });
                        }

                        if (state != gltf::SceneHierarchy::VisibilityState::AllInvisible && ImGui::Selectable("Make invisible from here")) {
                            traverseNode(assetExtended.asset, nodeIndex, [&](std::size_t nodeIndex) {
                                if (!assetExtended.asset.nodes[nodeIndex].meshIndex) return;

                                if (assetExtended.sceneHierarchy.getVisibility(nodeIndex)) {
                                    assetExtended.sceneHierarchy.setVisibility(nodeIndex, false);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            });
                        }

                        if (state == gltf::SceneHierarchy::VisibilityState::Intermediate && ImGui::Selectable("Toggle visibility from here")) {
                            traverseNode(assetExtended.asset, nodeIndex, [&](std::size_t nodeIndex) {
                                if (!assetExtended.asset.nodes[nodeIndex].meshIndex) return;

                                assetExtended.sceneHierarchy.flipVisibility(nodeIndex);
                                tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                            });
                        }
                    }
                };

                if (mergedNodeIndices.size() == 1) {
                    nodeContextMenu(nodeIndex);
                }
                else {
                    // Add intermediate context menu for the node selection.
                    for (std::size_t nodeIndex : mergedNodeIndices) {
                        imgui::WithStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), [&] {
                            ImGui::Text("#%zu", nodeIndex);
                        });
                        ImGui::SameLine();
                        if (ImGui::BeginMenu(tempStringBuffer.write(gui::getDisplayName(assetExtended.asset.nodes, nodeIndex)).view().c_str())) {
                            nodeContextMenu(nodeIndex);
                            ImGui::EndMenu();
                        }
                    }
                }

                ImGui::EndPopup();
            }

            if (global::shouldNodeInSceneHierarchyScrolledToBeVisible &&
                assetExtended.selectedNodes.size() == 1 && nodeIndex == *assetExtended.selectedNodes.begin()) {
                ImGui::ScrollToItem();
                global::shouldNodeInSceneHierarchyScrolledToBeVisible = false;
            }

            if (node.meshIndex) {
                ImGui::TableNextColumn();

                const bool visible = assetExtended.sceneHierarchy.getVisibility(nodeIndex);
                imgui::WithStyleColor(ImGuiCol_Button, 0x0, [&] {
                    if (ImGui::Button(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH)) {
                        assetExtended.sceneHierarchy.flipVisibility(nodeIndex);
                        tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                    }
                });
            }

            if (isTreeNodeOpen) {
                for (std::size_t childNodeIndex : node.children) {
                    self(childNodeIndex);
                }
                ImGui::TreePop();
            }
        };

        if (ImGui::BeginTable("scene-hierarchy-table", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Visibility", ImGuiTableColumnFlags_WidthFixed);

            const auto applySelectionRequests = [&](const ImGuiMultiSelectIO &io) {
                bool anySelectionChanged = false;
                for (const ImGuiSelectionRequest &req : io.Requests) {
                    if (req.Type == ImGuiSelectionRequestType_SetAll) {
                        if (req.Selected) {
                            for (std::size_t nodeIndex : renderedNodes) {
                                anySelectionChanged |= assetExtended.selectedNodes.emplace(nodeIndex).second;
                            }
                        }
                        else {
                            for (std::size_t nodeIndex : renderedNodes) {
                                anySelectionChanged |= assetExtended.selectedNodes.erase(nodeIndex) == 1;
                            }
                        }
                    }
                    else if (req.Type == ImGuiSelectionRequestType_SetRange) {
                        // Extract start and last node indices from ImGuiSelectionUserData. See the above
                        //   ImGui::SetNextItemSelectionUserData()
                        // call for details.
                        const std::size_t startNodeIndex = std::bit_cast<std::size_t>(req.RangeFirstItem) >> 32;
                        const std::size_t lastNodeIndex = std::bit_cast<std::size_t>(req.RangeLastItem) & 0xFFFFFFFF;

                        auto it = std::ranges::find(renderedNodes, startNodeIndex);
                        if (req.Selected) {
                            do {
                                assert(it != renderedNodes.end()); // lastNodeIndex not in renderedNodes?
                                anySelectionChanged |= assetExtended.selectedNodes.emplace(*it).second;
                            } while (*it++ != lastNodeIndex);
                        }
                        else {
                            do {
                                assert(it != renderedNodes.end()); // lastNodeIndex not in renderedNodes?
                                anySelectionChanged |= assetExtended.selectedNodes.erase(*it) == 1;
                            } while (*it++ != lastNodeIndex);
                        }
                    }
                }

                if (anySelectionChanged) {
                    tasks.emplace(std::in_place_type<task::NodeSelectionChanged>);
                }
            };

            applySelectionRequests(*ImGui::BeginMultiSelect(ImGuiMultiSelectFlags_ClearOnEscape | ImGuiMultiSelectFlags_BoxSelect2d));
            renderedNodes.clear();
            for (std::size_t nodeIndex : assetExtended.getScene().nodeIndices) {
                addChildNode(nodeIndex);
            }
            applySelectionRequests(*ImGui::EndMultiSelect());

            ImGui::EndTable();
        }
    }
    ImGui::End();

    sceneHierarchyCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::nodeInspector(gltf::AssetExtended &assetExtended) {
    if (assetExtended.selectedNodes.empty()) {
        imgui::widget::WindowWithCenteredText("Node Inspector", "No node selected.");
        nodeInspectorCalled = true;
        return;
    }

    if (ImGui::Begin("Node Inspector")) {
        if (assetExtended.selectedNodes.size() == 1) {
            const std::size_t selectedNodeIndex = *assetExtended.selectedNodes.begin();
            fastgltf::Node &node = assetExtended.asset.nodes[selectedNodeIndex];
            if (imgui::widget::InputTextWithHint("Name", gui::getDisplayName(assetExtended.asset.nodes, selectedNodeIndex), &node.name)) {
                if (!assetExtended.nodeNameSearchText.empty()) {
                    // If node name searching is enabled, occurrences should be updated based on the new name.
                    if (assetExtended.nodeNameSearchTextOccurrencePosByNode.contains(selectedNodeIndex)) {
                        // If the current node is in the occurrence map, update result will always be subset of the previous result.
                        assetExtended.updateNodeNameSearchTextOccurrencePosByNode(true);
                    }
                    else if (std::ranges::search(node.name, assetExtended.nodeNameSearchText, {}, LIFT(std::tolower), LIFT(std::tolower))) {
                        assetExtended.updateNodeNameSearchTextOccurrencePosByNode(false);
                    }
                    // else: neither the previous name nor the new name has occurrence, occurrence map will not be changed.
                }
            }

            ImGui::SeparatorText("Transform");

            bool isTransformUsedInAnimation = false;
            Flags<gltf::NodeAnimationUsage> nodeAnimationUsage{};
            for (const auto &[animation, enabled] : assetExtended.animations) {
                auto it = animation.nodeUsages.find(selectedNodeIndex);
                if (it == animation.nodeUsages.end()) continue;

                const Flags usage = it->second;
                if (usage | (gltf::NodeAnimationUsage::Translation | gltf::NodeAnimationUsage::Rotation | gltf::NodeAnimationUsage::Scale)) {
                    isTransformUsedInAnimation = true;
                }
                if (enabled) {
                    nodeAnimationUsage |= usage;
                }
            }

            // Using transform matrix is prohibited when node transform is used in animation as TRS.
            imgui::WithDisabled([&]() {
                if (bool isTrs = holds_alternative<fastgltf::TRS>(node.transform); ImGui::BeginCombo("Local transform", isTrs ? "TRS" : "Transform Matrix")) {
                    if (ImGui::Selectable("TRS", isTrs) && !isTrs) {
                        fastgltf::TRS trs;
                        decomposeTransformMatrix(get<fastgltf::math::fmat4x4>(node.transform), trs.scale, trs.rotation, trs.translation);
                        node.transform = trs;
                    }
                    if (ImGui::Selectable("Transform Matrix", !isTrs) && isTrs) {
                        const auto &trs = get<fastgltf::TRS>(node.transform);
                        node.transform.emplace<fastgltf::math::fmat4x4>(toMatrix(trs));
                    }
                    ImGui::EndCombo();
                }
            }, isTransformUsedInAnimation);

            bool transformChanged = false;
            std::visit(fastgltf::visitor {
                [&](fastgltf::TRS &trs) {
                    // If node TRS transform is used by an animation now, it cannot be modified by GUI.
                    imgui::WithDisabled([&]() {
                        transformChanged |= ImGui::DragFloat3("Translation", trs.translation.data());
                    }, nodeAnimationUsage & gltf::NodeAnimationUsage::Translation);
                    imgui::WithDisabled([&]() {
                        transformChanged |= ImGui::DragFloat4("Rotation", trs.rotation.data());
                    }, nodeAnimationUsage & gltf::NodeAnimationUsage::Rotation);
                    imgui::WithDisabled([&]() {
                        transformChanged |= ImGui::DragFloat3("Scale", trs.scale.data());
                    }, nodeAnimationUsage & gltf::NodeAnimationUsage::Scale);
                },
                [&](fastgltf::math::fmat4x4 &matrix) {
                    fastgltf::math::fvec4 row;
                    if (ImGui::DragFloat4("Row 1", (row = matrix.row(0)).data())) {
                        INDEX_SEQ(Is, 4, { ((matrix.col(Is).x() = row[Is]), ...); });
                        transformChanged = true;
                    }
                    if (ImGui::DragFloat4("Row 2", (row = matrix.row(1)).data())) {
                        INDEX_SEQ(Is, 4, { ((matrix.col(Is).y() = row[Is]), ...); });
                        transformChanged = true;
                    }
                    if (ImGui::DragFloat4("Row 3", (row = matrix.row(2)).data())) {
                        INDEX_SEQ(Is, 4, { ((matrix.col(Is).z() = row[Is]), ...); });
                        transformChanged = true;
                    }
                    imgui::WithDisabled([&] {
                        row = { 0.f, 0.f, 0.f, 1.f };
                        ImGui::DragFloat4("Row 4", row.data());
                    });
                },
            }, node.transform);

            if (transformChanged) {
                tasks.emplace(std::in_place_type<task::NodeLocalTransformChanged>, selectedNodeIndex);
            }

            if (!node.instancingAttributes.empty() && ImGui::TreeNodeEx("EXT_mesh_gpu_instancing", ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
                imgui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                    attributeTable(assetExtended.asset, node.instancingAttributes);
                });
            }

            if (const std::span morphTargetWeights = getTargetWeights(node, assetExtended.asset); !morphTargetWeights.empty()) {
                ImGui::SeparatorText("Morph Target Weights");

                // If node weights are used by an animation now, they cannot be modified by GUI.
                imgui::WithDisabled([&]() {
                    for (auto &&[i, weight] : morphTargetWeights | ranges::views::enumerate) {
                        if (ImGui::DragFloat(tempStringBuffer.write("Weight {}", i).view().c_str(), &weight, 0.01f)) {
                            tasks.emplace(std::in_place_type<task::MorphTargetWeightChanged>, selectedNodeIndex, i, 1);
                        }
                    }
                }, nodeAnimationUsage & gltf::NodeAnimationUsage::Weights);
            }

            if (ImGui::BeginTabBar("node-tab-bar")) {
                if (node.meshIndex && ImGui::BeginTabItem("Mesh")) {
                    fastgltf::Mesh &mesh = assetExtended.asset.meshes[*node.meshIndex];
                    imgui::widget::InputTextWithHint("Name", gui::getDisplayName(assetExtended.asset.meshes, *node.meshIndex), &mesh.name);

                    for (auto &&[primitiveIndex, primitive]: mesh.primitives | ranges::views::enumerate) {
                        if (ImGui::CollapsingHeader(tempStringBuffer.write("Primitive {}", primitiveIndex).view().c_str())) {
                            ImGui::LabelText("Type", "%s", format_as(primitive.type).c_str());

                            bool primitiveMaterialChanged = false;

                            const char* previewValue = "-";
                            if (primitive.materialIndex) {
                                previewValue = gui::getDisplayName(assetExtended.asset.materials, *primitive.materialIndex).c_str();
                            }

                            imgui::WithID(primitiveIndex, [&] {
                                if (ImGui::BeginCombo("Material", previewValue)) {
                                    if (ImGui::Selectable(primitive.materialIndex ? "[Unassign material...]" : "-", !primitive.materialIndex) && primitive.materialIndex) {
                                        // Unassign material.
                                        primitive.materialIndex.reset();
                                        primitiveMaterialChanged = true;
                                    }

                                    for (const auto &[materialIndex, material] : assetExtended.asset.materials | ranges::views::enumerate) {
                                        imgui::WithID(materialIndex, [&] {
                                            imgui::WithDisabled([&] {
                                                if (ImGui::Selectable(gui::getDisplayName(assetExtended.asset.materials, materialIndex).c_str(), primitive.materialIndex == materialIndex) &&
                                                    primitive.materialIndex != materialIndex) {
                                                    primitive.materialIndex.emplace(materialIndex);
                                                    primitiveMaterialChanged = true;
                                                }
                                            }, !gltf::isMaterialCompatible(material, primitive));
                                        });
                                    }

                                    if (ImGui::Selectable("[Assign new material...]")) {
                                        const std::size_t newMaterialIndex = assetExtended.asset.materials.size();
                                        assetExtended.asset.materials.push_back({});
                                        tasks.emplace(std::in_place_type<task::MaterialAdded>);

                                        primitive.materialIndex.emplace(newMaterialIndex);
                                        primitiveMaterialChanged = true;
                                    }

                                    ImGui::EndCombo();
                                }

                                const auto &originalMaterialIndex = assetExtended.originalMaterialIndexByPrimitive.at(&primitive);
                                if (to_optional(primitive.materialIndex) != originalMaterialIndex) {
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Reset")) {
                                        if (originalMaterialIndex) {
                                            primitive.materialIndex.emplace(*originalMaterialIndex);
                                        }
                                        else {
                                            primitive.materialIndex.reset();
                                        }
                                        primitiveMaterialChanged = true;
                                    }
                                }

                                if (primitiveMaterialChanged) {
                                    if (!primitive.mappings.empty()) {
                                        // If primitive is affected by KHR_materials_variants, current active material variant
                                        // index needed to be recalculated.
                                        assetExtended.imGuiSelectedMaterialVariantsIndex = gltf::getActiveMaterialVariantIndex(
                                            assetExtended.asset,
                                            [&](const fastgltf::Primitive &primitive) {
                                                return assetExtended.originalMaterialIndexByPrimitive.at(&primitive);
                                            });
                                    }

                                    // Assign assetExtended.imGuiSelectedMaterialIndex as primitive material index (maybe nullopt).
                                    if ((assetExtended.imGuiSelectedMaterialIndex = primitive.materialIndex)) {
                                        // If assigned material is not nullopt, open the material editor.
                                        // It will show the assigned material.
                                        gui::makeWindowVisible(ImGui::FindWindowByName("Material Editor"));
                                    }

                                    tasks.emplace(std::in_place_type<task::PrimitiveMaterialChanged>, &primitive);
                                }

                                attributeTable(
                                    assetExtended.asset,
                                    ranges::views::concat(
                                        to_range(primitive.indicesAccessor.transform([](std::size_t accessorIndex) {
                                            return fastgltf::Attribute { "Index", accessorIndex };
                                        })),
                                        primitive.attributes));
                            });
                        }
                    }

                    if (ImGui::InputInt("Bound fp precision", &boundFpPrecision)) {
                        boundFpPrecision = std::clamp(boundFpPrecision, 0, 9);
                    }

                    ImGui::EndTabItem();
                }
                if (node.cameraIndex && ImGui::BeginTabItem("Camera")) {
                    auto &[camera, name] = assetExtended.asset.cameras[*node.cameraIndex];
                    imgui::widget::InputTextWithHint("Name", gui::getDisplayName(assetExtended.asset.cameras, *node.cameraIndex), &name);

                    imgui::WithDisabled([&]() {
                        if (int type = camera.index(); ImGui::Combo("Type", &type, "Perspective\0Orthographic\0")) {
                            // TODO
                        }

                    });

                    constexpr auto noAffectHelperMarker = []() {
                        ImGui::SameLine();
                        imgui::widget::HelperMarker("(?)", "This property will not affect to the actual rendering, as it is calculated from the actual viewport size.");
                    };

                    visit(fastgltf::visitor {
                        [&](fastgltf::Camera::Perspective &camera) {
                            if (camera.aspectRatio) {
                                ImGui::DragFloat("Aspect Ratio", &*camera.aspectRatio, 0.01f, 1e-2f, 1e-2f);
                            }
                            else {
                                imgui::WithDisabled([this]() {
                                    float aspectRatio = centerNodeRect.GetWidth() / centerNodeRect.GetHeight();
                                    ImGui::DragFloat("Aspect Ratio", &aspectRatio);
                                });
                            }
                            noAffectHelperMarker();

                            if (float fovInDegree = glm::degrees(camera.yfov); ImGui::DragFloat("FOV", &fovInDegree, 1.f, 15.f, 120.f, "%.2f deg")) {
                                camera.yfov = glm::radians(fovInDegree);
                            }

                            if (camera.zfar) {
                                ImGui::DragFloatRange2("Near/Far", &camera.znear, &*camera.zfar, 1.f, 1e-6f, 1e-6f, "%.2e", nullptr, ImGuiSliderFlags_Logarithmic);
                            }
                            else {
                                ImGui::PushMultiItemsWidths(2, ImGui::CalcItemWidth());
                                ImGui::DragFloat("##near", &camera.znear, 1.f, 1e-6f, 1e-6f, "%.2e", ImGuiSliderFlags_Logarithmic);
                                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                                imgui::WithDisabled([]() {
                                    float zFar = std::numeric_limits<float>::infinity();
                                    ImGui::DragFloat("##far", &zFar);
                                });
                                ImGui::PopItemWidth();
                                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                                imgui::widget::TextUnformatted("Near/Far");
                            }
                        },
                        [&](fastgltf::Camera::Orthographic &camera) {
                            ImGui::DragFloat("Half Width", &camera.xmag);
                            noAffectHelperMarker();

                            ImGui::DragFloat("Half Height", &camera.ymag);
                            noAffectHelperMarker();

                            ImGui::DragFloatRange2("Near/Far", &camera.znear, &camera.zfar, 1.f, 1e-6f, 1e-6f, "%.2e", nullptr, ImGuiSliderFlags_Logarithmic);
                        },
                    }, camera);

                    ImGui::EndTabItem();
                }
                if (node.lightIndex && ImGui::BeginTabItem("Light")) {
                    fastgltf::Light &light = assetExtended.asset.lights[*node.lightIndex];
                    imgui::widget::InputTextWithHint("Name", gui::getDisplayName(assetExtended.asset.lights, *node.lightIndex), &light.name);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        else {
            imgui::widget::TextUnformatted("Multiple nodes are selected."sv);

            ImGui::BeginListBox("##candidates");

            // TODO: avoid calculation?
            std::vector sortedIndices { std::from_range, assetExtended.selectedNodes };
            std::ranges::sort(sortedIndices);

            for (std::size_t nodeIndex : sortedIndices) {
                bool selected = false;
                imgui::WithID(nodeIndex, [&]() {
                    selected = ImGui::Selectable(gui::getDisplayName(assetExtended.asset.nodes, nodeIndex).c_str());
                });

                if (ImGui::IsItemHovered()) {
                    tasks.emplace(std::in_place_type<task::HoverNodeFromGui>, nodeIndex);
                }

                if (selected) {
                    assetExtended.selectedNodes = { nodeIndex };
                    break;
                }
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();

    nodeInspectorCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::imageBasedLighting(
    const AppState::ImageBasedLighting &info,
    ImTextureRef eqmapTextureRef
) {
    if (ImGui::Begin("IBL")) {
        if (ImGui::CollapsingHeader("Equirectangular map")) {
            const float displayRatio = static_cast<float>(info.eqmap.dimension.x) / ImGui::GetContentRegionAvail().x;
            imgui::widget::HoverableImage(eqmapTextureRef, ImVec2 { static_cast<float>(info.eqmap.dimension.x), static_cast<float>(info.eqmap.dimension.y) } / displayRatio, displayRatio);

            imgui::WithLabel("File"sv, [&]() {
                ImGui::TextLinkOpenURL(PATH_C_STR(info.eqmap.path.filename()), PATH_C_STR(info.eqmap.path));
            });
            ImGui::LabelText("Dimension", "%ux%u", info.eqmap.dimension.x, info.eqmap.dimension.y);
        }

        if (ImGui::CollapsingHeader("Cubemap")) {
            ImGui::LabelText("Size", "%u", info.cubemap.size);
        }

        if (ImGui::CollapsingHeader("Diffuse Irradiance")) {
            imgui::widget::TextUnformatted("Spherical harmonic coefficients (up to 3rd band)"sv);
            constexpr std::array bandLabels { "L0"sv, "L1_1"sv, "L10"sv, "L11"sv, "L2_2"sv, "L2_1"sv, "L20"sv, "L21"sv, "L22"sv };
            imgui::widget::Table<false>(
                "spherical-harmonic-coeffs",
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                std::views::zip(bandLabels, info.diffuseIrradiance.sphericalHarmonicCoefficients),
                imgui::widget::TableColumnInfo { "Band", decomposer([](std::string_view label, const auto&) { imgui::widget::TextUnformatted(label); }) },
                imgui::widget::TableColumnInfo { "x", decomposer([](auto, const glm::vec3 &coeff) { imgui::widget::TextUnformatted(tempStringBuffer.write(coeff.x)); }) },
                imgui::widget::TableColumnInfo { "y", decomposer([](auto, const glm::vec3 &coeff) { imgui::widget::TextUnformatted(tempStringBuffer.write(coeff.y)); }) },
                imgui::widget::TableColumnInfo { "z", decomposer([](auto, const glm::vec3 &coeff) { imgui::widget::TextUnformatted(tempStringBuffer.write(coeff.z)); }) });
        }

        if (ImGui::CollapsingHeader("Prefiltered map")) {
            ImGui::LabelText("Size", "%u", info.prefilteredmap.size);
            ImGui::LabelText("Roughness levels", "%u", info.prefilteredmap.roughnessLevels);
            ImGui::LabelText("Samples", "%u", info.prefilteredmap.sampleCount);
        }
    }
    ImGui::End();

    imageBasedLightingCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::rendererSetting(Renderer &renderer) {
    if (ImGui::Begin("Renderer Setting")){
        if (ImGui::CollapsingHeader("Camera")) {
            char displayText[2] = { static_cast<char>('0' + renderer.cameras.size()), 0 };
            if (ImGui::BeginCombo("Viewport Count", displayText)) {
                for (std::uint32_t viewCount : { 1, 2, 4 }) {
                    displayText[0] = '0' + viewCount;
                    const bool selected = renderer.cameras.size() == viewCount;
                    if (ImGui::Selectable(displayText, selected) && !selected) {
                        tasks.emplace(std::in_place_type<task::ChangeViewCount>, viewCount);
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::SeparatorText("Viewport Arrangement");

            ImVec2 buttonSize;
            buttonSize.x = ImGui::CalcItemWidth();
            if (renderer.cameras.size() >= 2) {
                // Make 2 buttons in a row.
                buttonSize.x = buttonSize.x / 2.f - ImGui::GetStyle().ItemInnerSpacing.x;
            }
            buttonSize.y = buttonSize.x * centerNodeRect.GetHeight() / centerNodeRect.GetWidth();
            if (renderer.cameras.size() == 2) {
                buttonSize.y *= 2;
            }

            for (auto &&[cameraIndex, camera] : renderer.cameras | ranges::views::enumerate) {
                if (cameraIndex % 2 == 1) {
                    ImGui::SameLine();
                }
                ImGui::Button(tempStringBuffer.write("Camera {}##button", cameraIndex + 1).view().c_str(), buttonSize);

                // Drag-and-drop between buttons should be available only if there are multiple cameras.
                if (renderer.cameras.size() > 1) {
                    constexpr auto payloadId = "CAMIDX";
                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                        ImGui::SetDragDropPayload(payloadId, &cameraIndex, sizeof(cameraIndex));
                        ImGui::Text("Swap camera settings");
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(payloadId)) {
                            assert(payload->DataSize == sizeof(cameraIndex));
                            std::swap(camera, renderer.cameras[*static_cast<const std::size_t*>(payload->Data)]);
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
            }

            for (auto &&[cameraIndex, camera] : renderer.cameras | ranges::views::enumerate) {
                if (ImGui::TreeNode(tempStringBuffer.write("Camera {}", cameraIndex + 1).view().c_str())) {
                    ImGui::DragFloat3("Position", value_ptr(camera.position), 0.1f);
                    if (ImGui::DragFloat3("Direction", value_ptr(camera.direction), 0.1f, -1.f, 1.f)) {
                        camera.direction = normalize(camera.direction);
                    }
                    if (ImGui::DragFloat3("Up", value_ptr(camera.up), 0.1f, -1.f, 1.f)) {
                        camera.up = normalize(camera.up);
                    }

                    if (float fovInDegree = glm::degrees(camera.fov); ImGui::DragFloat("FOV", &fovInDegree, 0.1f, 15.f, 120.f, "%.2f deg")) {
                        camera.fov = glm::radians(fovInDegree);
                    }

                    imgui::WithDisabled([&] {
                        ImGui::DragFloatRange2("Near/Far", &camera.zMin, &camera.zMax, 1.f, 1e-6f, 1e-6f, "%.2e", nullptr, ImGuiSliderFlags_Logarithmic);
                    }, camera.automaticNearFarPlaneAdjustment);

                    ImGui::Checkbox("Automatic Near/Far Adjustment", &camera.automaticNearFarPlaneAdjustment);
                    ImGui::SameLine();
                    imgui::widget::HelperMarker("(?)", "Near/Far plane will be automatically tightened to fit the scene bounding box.");

                    ImGui::TreePop();
                }
            }

            // TODO: support multiview frustum culling
            if (renderer.cameras.size() == 1) {
                ImGui::SeparatorText("Frustum Culling");

                constexpr auto to_string = [](Renderer::FrustumCullingMode mode) noexcept -> const char* {
                    switch (mode) {
                        case Renderer::FrustumCullingMode::Off: return "Off";
                        case Renderer::FrustumCullingMode::On: return "On";
                        case Renderer::FrustumCullingMode::OnWithInstancing: return "On with instancing";
                    }
                    std::unreachable();
                };
                if (ImGui::BeginCombo("Mode", to_string(renderer.frustumCullingMode))) {
                    for (auto mode : { Renderer::FrustumCullingMode::Off, Renderer::FrustumCullingMode::On, Renderer::FrustumCullingMode::OnWithInstancing }) {
                        if (ImGui::Selectable(to_string(mode), renderer.frustumCullingMode == mode) && renderer.frustumCullingMode != mode) {
                            renderer.frustumCullingMode = mode;
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
            }
        }

        if (ImGui::CollapsingHeader("Multisample Anti-Aliasing (MSAA)")) {
            if (ImGui::BeginCombo("Sample count", tempStringBuffer.write(renderer.msaaSampleCount).view().c_str())) {
                for (std::uint8_t sampleCount : renderer.capabilities.msaaSampleCounts) {
                    if (ImGui::Selectable(tempStringBuffer.write(sampleCount).view().c_str(), renderer.msaaSampleCount == sampleCount) &&
                        renderer.msaaSampleCount != sampleCount) {
                        renderer.msaaSampleCount = sampleCount;
                        tasks.emplace(std::in_place_type<task::ChangeSampleCount>, sampleCount);
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        if (ImGui::CollapsingHeader("Background")) {
            const bool useSolidBackground = renderer.solidBackground.has_value();
            imgui::WithDisabled([&]() {
                if (ImGui::RadioButton("Use cubemap image from equirectangular map", !useSolidBackground)) {
                    renderer.solidBackground.set_active(false);
                }
            }, !renderer.canSelectSkyboxBackground());

            if (ImGui::RadioButton("Use solid color", useSolidBackground)) {
                renderer.solidBackground.set_active(true);
            }
            imgui::WithDisabled([&]() {
                ImGui::ColorPicker3("Color", value_ptr(renderer.solidBackground.raw()));
            }, !useSolidBackground);
        }

        if (ImGui::CollapsingHeader("Node selection")) {
            bool showHoveringNodeOutline = renderer.hoveringNodeOutline.has_value();
            if (ImGui::Checkbox("Hovering node outline", &showHoveringNodeOutline)) {
                renderer.hoveringNodeOutline.set_active(showHoveringNodeOutline);
            }
            imgui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##hoveringNodeOutline", &renderer.hoveringNodeOutline->thickness, 1.f, 1.f, 1.f);
                ImGui::ColorEdit4("Color##hoveringNodeOutline", value_ptr(renderer.hoveringNodeOutline->color));
            }, !showHoveringNodeOutline);

            bool showSelectedNodeOutline = renderer.selectedNodeOutline.has_value();
            if (ImGui::Checkbox("Selected node outline", &showSelectedNodeOutline)) {
                renderer.selectedNodeOutline.set_active(showSelectedNodeOutline);
            }
            imgui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##selectedNodeOutline", &renderer.selectedNodeOutline->thickness, 1.f, 1.f, 1.f);
                ImGui::ColorEdit4("Color##selectedNodeOutline", value_ptr(renderer.selectedNodeOutline->color));
            }, !showSelectedNodeOutline);
        }

        if (ImGui::CollapsingHeader("Bloom")) {
            bool bloom = renderer.bloom.has_value();
            imgui::WithDisabled([&] {
                if (ImGui::Checkbox("Enable bloom", &bloom)) {
                    renderer.bloom.set_active(bloom);
                }
            }, !bloom && renderer.grid);

            if (!bloom && renderer.grid) {
                ImGui::SameLine();
                imgui::widget::HelperMarker("(?)", "Bloom effect cannot be enabled when grid is enabled.");
            }

            imgui::WithDisabled([&]() {
                const char* const previewValue = [&]() {
                    switch (renderer.bloom.raw().mode) {
                        case Renderer::Bloom::PerMaterial: return "Per-Material";
                        case Renderer::Bloom::PerFragment: return "Per-Fragment";
                    }
                    std::unreachable();
                }();
                if (ImGui::BeginCombo("Mode", previewValue)) {
                    if (ImGui::Selectable("Per-Material", renderer.bloom->mode == Renderer::Bloom::PerMaterial) && renderer.bloom->mode != Renderer::Bloom::PerMaterial) {
                        renderer.bloom->mode = Renderer::Bloom::PerMaterial;
                        tasks.emplace(std::in_place_type<task::BloomModeChanged>);

                        ImGui::SetItemDefaultFocus();
                    }

                    imgui::WithDisabled([&]() {
                        if (ImGui::Selectable("Per-Fragment", renderer.bloom->mode == Renderer::Bloom::PerFragment) && renderer.bloom->mode != Renderer::Bloom::PerFragment) {
                            renderer.bloom->mode = Renderer::Bloom::PerFragment;
                            tasks.emplace(std::in_place_type<task::BloomModeChanged>);

                            ImGui::SetItemDefaultFocus();
                        }
                    }, !renderer.capabilities.perFragmentBloom);
                    ImGui::SameLine();
                    if (renderer.capabilities.perFragmentBloom) {
                        imgui::widget::HelperMarker("(!)", "Rendering performance may be significantly degraded.");
                    }
                    else {
                        imgui::widget::HelperMarker("(?)", "Per-Fragment bloom mode is not supported in this device.");
                    }

                    ImGui::EndCombo();
                }

                ImGui::DragFloat("Intensity", &renderer.bloom.raw().intensity, 1e-2f, 0.f, 0.1f);
            }, !bloom);
        }

        if (ImGui::CollapsingHeader("Grid")) {
            bool grid = renderer.grid.has_value();
            imgui::WithDisabled([&] {
                if (ImGui::Checkbox("Enable grid", &grid)) {
                    renderer.grid.set_active(grid);
                }
            }, !grid && renderer.bloom);

            if (!grid && renderer.bloom) {
                ImGui::SameLine();
                imgui::widget::HelperMarker("(?)", "Grid cannot be enabled when bloom effect is enabled.");
            }

            imgui::WithDisabled([&] {
                ImGui::DragFloat("Size", &renderer.grid.raw().size, 1.f, 0.f, std::numeric_limits<float>::max());
                ImGui::ColorEdit3("Color", value_ptr(renderer.grid.raw().color));
                ImGui::Checkbox("Show minor axes", &renderer.grid.raw().showMinorAxes);
            }, !grid);
        }
    }
    ImGui::End();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::imguizmo(Renderer &renderer, std::size_t viewIndex) {
    // Set ImGuizmo rect.
    ImGuizmo::BeginFrame();

    const ImRect targetViewportRect = renderer.getViewportRect(centerNodeRect, viewIndex);
    ImGuizmo::SetRect(targetViewportRect.Min.x, targetViewportRect.Min.y, targetViewportRect.GetWidth(), targetViewportRect.GetHeight());

    constexpr ImVec2 size { 64.f, 64.f };
    constexpr ImU32 background = 0x00000000; // Transparent.

    Camera &camera = renderer.cameras[viewIndex];
    const glm::mat4 oldView = camera.getViewMatrix();
    glm::mat4 newView = oldView;
    ImGuizmo::ViewManipulate(value_ptr(newView), camera.targetDistance, targetViewportRect.Max - size, size, background);
    if (newView != oldView) {
        const glm::mat4 inverseView = inverse(newView);
        camera.up = inverseView[1];
        camera.position = inverseView[3];
        camera.direction = -inverseView[2];
    }
}

void vk_gltf_viewer::control::ImGuiTaskCollector::imguizmo(Renderer &renderer, std::size_t viewIndex, gltf::AssetExtended &assetExtended) {
    // Set ImGuizmo rect.
    ImGuizmo::BeginFrame();

    const ImRect targetViewportRect = renderer.getViewportRect(centerNodeRect, viewIndex);
    ImGuizmo::SetRect(targetViewportRect.Min.x, targetViewportRect.Min.y, targetViewportRect.GetWidth(), targetViewportRect.GetHeight());

    const auto isNodeUsedByEnabledAnimations = [&](std::size_t nodeIndex) {
        for (const auto &[animation, enabled] : assetExtended.animations) {
            if (!enabled) continue;

            auto it = animation.nodeUsages.find(nodeIndex);
            if (it == animation.nodeUsages.end()) continue;

            const Flags usage = it->second;
            if ((renderer.imGuizmoOperation == ImGuizmo::OPERATION::TRANSLATE && (usage & gltf::NodeAnimationUsage::Translation)) ||
                (renderer.imGuizmoOperation == ImGuizmo::OPERATION::ROTATE && (usage & gltf::NodeAnimationUsage::Rotation)) ||
                (renderer.imGuizmoOperation == ImGuizmo::OPERATION::SCALE && (usage & gltf::NodeAnimationUsage::Scale))) {
                return true;
            }
        }

        return false;
    };

    Camera &camera = renderer.cameras[viewIndex];

    // Enable snap for ImGuizmo::Manipulate() when holding Shift key.
    const float *snap = nullptr;
    if (ImGui::GetIO().KeyShift) {
        static std::array<float, 3> snapData;
        switch (renderer.imGuizmoOperation) {
            case ImGuizmo::OPERATION::TRANSLATE:
                // Same code used in shaders/grid.vert
                snapData.fill(std::pow(10.f, -std::floor(-std::log10(camera.position.y)) - 1.f));
                break;
            case ImGuizmo::OPERATION::ROTATE:
                snapData.fill(45.f);
                break;
            case ImGuizmo::OPERATION::SCALE:
                snapData.fill(1.f);
                break;
            default:
                std::unreachable(); // Only TRANSLATE/ROTATE/SCALE can be in Renderer::imGuizmoOperation.
        }

        snap = snapData.data();
    }

    if (assetExtended.selectedNodes.size() == 1) {
        const std::size_t selectedNodeIndex = *assetExtended.selectedNodes.begin();
        fastgltf::math::fmat4x4 newWorldTransform = assetExtended.sceneHierarchy.getWorldTransform(selectedNodeIndex);

        ImGuizmo::Enable(!isNodeUsedByEnabledAnimations(selectedNodeIndex));

        if (Manipulate(value_ptr(camera.getViewMatrix()), value_ptr(camera.getProjectionMatrixForwardZ()), renderer.imGuizmoOperation, ImGuizmo::MODE::LOCAL, newWorldTransform.data(), nullptr, snap)) {
            const fastgltf::math::fmat4x4 deltaMatrix = affineInverse(assetExtended.sceneHierarchy.getWorldTransform(selectedNodeIndex)) * newWorldTransform;

            updateTransform(assetExtended.asset.nodes[selectedNodeIndex], [&](fastgltf::math::fmat4x4 &transformMatrix) {
                transformMatrix = transformMatrix * deltaMatrix;
            });

            tasks.emplace(std::in_place_type<task::NodeLocalTransformChanged>, selectedNodeIndex);
        }
    }
    else if (assetExtended.selectedNodes.size() >= 2) {
        static std::optional<fastgltf::math::fmat4x4> retainedPivotTransformMatrix;
        if (!retainedPivotTransformMatrix) {
            // Create a virtual pivot at the center among the selected nodes.
            fastgltf::math::fvec3 pivot{};
            for (std::size_t nodeIndex : assetExtended.selectedNodes) {
                pivot += fastgltf::math::fvec3 { assetExtended.sceneHierarchy.getWorldTransform(nodeIndex).col(3) };
            }
            pivot *= 1.f / assetExtended.selectedNodes.size();

            retainedPivotTransformMatrix.emplace(
                fastgltf::math::fvec4 { 1.f, 0.f, 0.f, 0.f },
                fastgltf::math::fvec4 { 0.f, 1.f, 0.f, 0.f },
                fastgltf::math::fvec4 { 0.f, 0.f, 1.f, 0.f },
                fastgltf::math::fvec4 { pivot.x(), pivot.y(), pivot.z(), 1.f });
        }

        ImGuizmo::Enable(std::ranges::none_of(assetExtended.selectedNodes, isNodeUsedByEnabledAnimations));

        if (fastgltf::math::fmat4x4 deltaMatrix;
            Manipulate(value_ptr(camera.getViewMatrix()), value_ptr(camera.getProjectionMatrixForwardZ()), renderer.imGuizmoOperation, ImGuizmo::MODE::WORLD, retainedPivotTransformMatrix->data(), deltaMatrix.data(), snap)) {
            for (std::size_t nodeIndex : assetExtended.selectedNodes) {
                const fastgltf::math::fmat4x4 &oldWorldTransform = assetExtended.sceneHierarchy.getWorldTransform(nodeIndex);
                const fastgltf::math::fmat4x4 inverseOldWorldTransform = affineInverse(oldWorldTransform);

                // Update node's world transform by pre-multiplying the delta matrix.
                assetExtended.sceneHierarchy.setWorldTransformNonPropagated(nodeIndex, deltaMatrix * oldWorldTransform);

                // Update node's local transform to match the world transform.
                const fastgltf::math::fmat4x4 &newWorldTransform = assetExtended.sceneHierarchy.getWorldTransform(nodeIndex);
                updateTransform(assetExtended.asset.nodes[nodeIndex], [&](fastgltf::math::fmat4x4 &localTransform) {
                    // newWorldTransform = oldParentWorldTransform * newLocalTransform
                    //                   = oldParentWorldTransform * (oldLocalTransform * localTransformDelta)
                    //                   = oldWorldTransform * localTransformDelta
                    // Therefore,
                    //     localTransformDelta = oldWorldTransform^-1 * newWorldTransform, and
                    //     newLocalTransform = oldLocalTransform * oldWorldTransform^-1 * newWorldTransform
                    localTransform = localTransform * inverseOldWorldTransform * newWorldTransform;
                });

                // The updated node's immediate descendants local transforms also have to be updated to match their
                // original world transforms.
                const fastgltf::math::fmat4x4 inverseNewWorldTransform = affineInverse(newWorldTransform);
                for (std::size_t childNodeIndex : assetExtended.asset.nodes[nodeIndex].children) {
                    // If the currently processing child is also in the selection, its world transform is changed,
                    // therefore must be processed in the next execution of outer for-loop.
                    if (assetExtended.selectedNodes.contains(childNodeIndex)) {
                        continue;
                    }

                    updateTransform(assetExtended.asset.nodes[childNodeIndex], [&](fastgltf::math::fmat4x4 &localTransform) {
                        // newWorldTransform = parentWorldTransform * newLocalTransform = oldWorldTransform.
                        // Therefore, newLocalTransform = parentWorldTransform^-1 * oldWorldTransform
                        localTransform = inverseNewWorldTransform * assetExtended.sceneHierarchy.getWorldTransform(childNodeIndex);
                    });
                }

                tasks.emplace(std::in_place_type<task::NodeWorldTransformChanged>, nodeIndex);
            }
        }
        else if (!ImGuizmo::IsUsing()) {
            retainedPivotTransformMatrix.reset();
        }
    }

    constexpr ImVec2 size { 64.f, 64.f };
    constexpr ImU32 background = 0x00000000; // Transparent.
    const glm::mat4 oldView = camera.getViewMatrix();
    glm::mat4 newView = oldView;
    ImGuizmo::ViewManipulate(value_ptr(newView), camera.targetDistance, targetViewportRect.Max - size, size, background);
    if (newView != oldView) {
        const glm::mat4 inverseView = inverse(newView);
        camera.up = inverseView[1];
        camera.position = inverseView[3];
        camera.direction = -inverseView[2];
    }
}