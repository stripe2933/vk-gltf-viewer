module;

#include <cassert>
#include <version>

#include <IconsFontAwesome4.h>
#include <nfd.hpp>

module vk_gltf_viewer;
import :imgui.TaskCollector;

import std;
import glm;
import imgui.internal;
import imgui.math;
import ImGuizmo;
import :global;
import :gltf.algorithm.traversal;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.formatter.ByteSize;
import :helpers.functional;
import :helpers.imgui;
import :helpers.optional;
import :helpers.ranges;
import :helpers.TempStringBuffer;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }
#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#ifdef _WIN32
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

using namespace std::string_view_literals;

std::optional<std::size_t> vk_gltf_viewer::control::ImGuiTaskCollector::selectedMaterialIndex = std::nullopt;
int boundFpPrecision = 2;

/**
 * Return \p str if it is not empty, otherwise return the result of \p fallback.
 * @param str Null-terminated non-owning string view to check.
 * @param fallback Fallback function to call if \p str is empty.
 * @return <tt>cpp_util::cstring_view</tt> that contains either the original \p str, or the result of \p fallback.
 */
template <concepts::signature_of<cpp_util::cstring_view()> F>
[[nodiscard]] cpp_util::cstring_view nonempty_or(cpp_util::cstring_view str, F &&fallback) {
    if (str.empty()) return FWD(fallback)();
    else return str;
}

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
        throw std::runtime_error { std::format("File dialog error: {}", NFD::GetError() ) };
    }
}

void makeWindowVisible(const char* window_name) {
    ImGuiWindow *const window = ImGui::FindWindowByName(window_name);
    assert(window && "Unknown window name");

    if (window->DockNode && window->DockNode->TabBar) {
        // If window is docked and within the tab bar, make the tab bar's selected tab index to the current.
        // https://github.com/ocornut/imgui/issues/2887#issuecomment-849779358
        // TODO: if two docked window is in the same tab bar, it is not work.
        window->DockNode->TabBar->NextSelectedTabId = window->TabId;
    }
    else {
        // Otherwise, window is detached, therefore focusing it to make it top most.
        ImGui::FocusWindow(window);
    }
}

void hoverableImage(ImTextureID texture, const ImVec2 &size) {
    const ImVec2 texturePosition = ImGui::GetCursorScreenPos();
    ImGui::Image(texture, size);

    if (ImGui::BeginItemTooltip()) {
        const ImGuiIO &io = ImGui::GetIO();

        const ImVec2 zoomedPortionSize = size / 4.f;
        ImVec2 region = io.MousePos - texturePosition - zoomedPortionSize * 0.5f;
        region.x = std::clamp(region.x, 0.f, size.x - zoomedPortionSize.x);
        region.y = std::clamp(region.y, 0.f, size.y - zoomedPortionSize.y);

        constexpr float zoomScale = 4.0f;
        ImGui::Image(texture, zoomedPortionSize * zoomScale, region / size, (region + zoomedPortionSize) / size);
        ImGui::TextUnformatted(tempStringBuffer.write("Showing: [{:.0f}, {:.0f}]x[{:.0f}, {:.0f}]", region.x, region.y, region.x + zoomedPortionSize.y, region.y + zoomedPortionSize.y));

        ImGui::EndTooltip();
    }
}

void hoverableImageCheckerboardBackground(ImTextureID texture, const ImVec2 &size) {
    const ImVec2 texturePosition = ImGui::GetCursorScreenPos();
    ImGui::ImageCheckerboardBackground(texture, size);

    if (ImGui::BeginItemTooltip()) {
        const ImGuiIO &io = ImGui::GetIO();

        const ImVec2 zoomedPortionSize = size / 4.f;
        ImVec2 region = io.MousePos - texturePosition - zoomedPortionSize * 0.5f;
        region.x = std::clamp(region.x, 0.f, size.x - zoomedPortionSize.x);
        region.y = std::clamp(region.y, 0.f, size.y - zoomedPortionSize.y);

        constexpr float zoomScale = 4.0f;
        ImGui::ImageCheckerboardBackground(texture, zoomedPortionSize * zoomScale, region / size, (region + zoomedPortionSize) / size);
        ImGui::TextUnformatted(tempStringBuffer.write("Showing: [{:.0f}, {:.0f}]x[{:.0f}, {:.0f}]", region.x, region.y, region.x + zoomedPortionSize.y, region.y + zoomedPortionSize.y));

        ImGui::EndTooltip();
    }
}

void attributeTable(std::ranges::viewable_range auto const &attributes) {
    ImGui::Table<false>(
        "attributes-table",
        ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit,
        attributes,
        ImGui::ColumnInfo { "Attribute", decomposer([](std::string_view attributeName, const auto&) {
            ImGui::TextUnformatted(attributeName);
        }) },
        ImGui::ColumnInfo { "Type", decomposer([](auto, const fastgltf::Accessor &accessor) {
            ImGui::TextUnformatted(tempStringBuffer.write("{} ({})", accessor.type, accessor.componentType));
        }) },
        ImGui::ColumnInfo { "Count", decomposer([](auto, const fastgltf::Accessor &accessor) {
            ImGui::TextUnformatted(tempStringBuffer.write(accessor.count));
        }) },
        ImGui::ColumnInfo { "Bound", decomposer([](auto, const fastgltf::Accessor &accessor) {
            std::visit(fastgltf::visitor {
                [](const std::pmr::vector<std::int64_t> &min, const std::pmr::vector<std::int64_t> &max) {
                    assert(min.size() == max.size() && "Different min/max dimension");
                    if (min.size() == 1) ImGui::TextUnformatted(tempStringBuffer.write("[{}, {}]", min[0], max[0]));
                    else ImGui::TextUnformatted(tempStringBuffer.write("{}x{}", min, max));
                },
                [](const std::pmr::vector<double> &min, const std::pmr::vector<double> &max) {
                    assert(min.size() == max.size() && "Different min/max dimension");
                    if (min.size() == 1) ImGui::TextUnformatted(tempStringBuffer.write("[{1:.{0}f}, {2:.{0}f}]", boundFpPrecision, min[0], max[0]));
                    else ImGui::TextUnformatted(tempStringBuffer.write("{1::.{0}f}x{2::.{0}f}", boundFpPrecision, min, max));
                },
                [](const auto&...) {
                    ImGui::TextUnformatted("-"sv);
                }
            }, accessor.min, accessor.max);
        }) },
        ImGui::ColumnInfo { "Normalized", decomposer([](auto, const fastgltf::Accessor &accessor) {
            ImGui::TextUnformatted(accessor.normalized ? "Yes"sv : "No"sv);
        }), ImGuiTableColumnFlags_DefaultHide },
        ImGui::ColumnInfo { "Sparse", decomposer([](auto, const fastgltf::Accessor &accessor) {
            ImGui::TextUnformatted(accessor.sparse ? "Yes"sv : "No"sv);
        }), ImGuiTableColumnFlags_DefaultHide },
        ImGui::ColumnInfo { "Buffer View", decomposer([](auto, const fastgltf::Accessor &accessor) {
            if (accessor.bufferViewIndex) {
                if (ImGui::TextLink(tempStringBuffer.write(*accessor.bufferViewIndex).view().c_str())) {
                    makeWindowVisible("Buffer Views");
                }
            }
            else {
                ImGui::TextDisabled("-");
                ImGui::SameLine();
                ImGui::HelperMarker("(?)", "Zero will be used for accessor data.");
            }
        }) });
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetInfo(fastgltf::AssetInfo &assetInfo) {
    ImGui::InputTextWithHint("glTF Version", "<empty>", &assetInfo.gltfVersion);
    ImGui::InputTextWithHint("Generator", "<empty>", &assetInfo.generator);
    ImGui::InputTextWithHint("Copyright", "<empty>", &assetInfo.copyright);
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetBuffers(std::span<fastgltf::Buffer> buffers, const std::filesystem::path &assetDir) {
    ImGui::Table(
        "gltf-buffers-table",
        ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
        buffers,
        ImGui::ColumnInfo { "Name", [](std::size_t row, fastgltf::Buffer &buffer) {
            ImGui::WithID(row, [&]() {
                ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                ImGui::InputTextWithHint("##name", "<empty>", &buffer.name);

            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "Size", [](const fastgltf::Buffer &buffer) {
            ImGui::TextUnformatted(tempStringBuffer.write(ByteSize { buffer.byteLength }));
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "MIME", [](const fastgltf::Buffer &buffer) {
            visit([](const auto &source) {
                if constexpr (requires { { source.mimeType } -> std::convertible_to<fastgltf::MimeType>; }) {
                    ImGui::TextUnformatted(to_string(source.mimeType));
                }
                else {
                    ImGui::TextDisabled("-");
                }
            }, buffer.data);
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Location", [&](std::size_t row, const fastgltf::Buffer &buffer) {
            visit(fastgltf::visitor {
                [](const fastgltf::sources::Array&) {
                    ImGui::TextUnformatted("Embedded (Array)"sv);
                },
                [](const fastgltf::sources::BufferView &bufferView) {
                    ImGui::TextUnformatted(tempStringBuffer.write("BufferView ({})", bufferView.bufferViewIndex));
                },
                [&](const fastgltf::sources::URI &uri) {
                    ImGui::WithID(row, [&]() {
                        ImGui::TextLinkOpenURL(ICON_FA_EXTERNAL_LINK, PATH_C_STR(assetDir / uri.uri.fspath()));
                    });
                },
                [](const auto&) {
                    ImGui::TextDisabled("-");
                }
            }, buffer.data);
        }, ImGuiTableColumnFlags_WidthFixed });
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetBufferViews(std::span<fastgltf::BufferView> bufferViews, std::span<fastgltf::Buffer> buffers) {
    ImGui::TableWithVirtualization(
        "gltf-buffer-views-table",
        ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
        bufferViews,
        ImGui::ColumnInfo { "Name", [&](std::size_t rowIndex, fastgltf::BufferView &bufferView) {
            ImGui::WithID(rowIndex, [&]() {
                ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                ImGui::InputTextWithHint("##name", "<empty>", &bufferView.name);
            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "Buffer", [&](std::size_t i, const fastgltf::BufferView &bufferView) {
            ImGui::PushID(i);
            if (ImGui::TextLink(tempStringBuffer.write(bufferView.bufferIndex).view().c_str())) {
                makeWindowVisible("Buffers");
            }
            ImGui::PopID();
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Range", [&](const fastgltf::BufferView &bufferView) {
            ImGui::TextUnformatted(tempStringBuffer.write(
                "[{}, {}]", bufferView.byteOffset, bufferView.byteOffset + bufferView.byteLength));
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Size", [&](const fastgltf::BufferView &bufferView) {
            ImGui::TextUnformatted(tempStringBuffer.write(ByteSize(bufferView.byteLength)));
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Stride", [&](const fastgltf::BufferView &bufferView) {
            if (const auto &byteStride = bufferView.byteStride) {
                ImGui::TextUnformatted(tempStringBuffer.write(*byteStride));
            }
            else {
                ImGui::TextDisabled("-");
            }
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Target", [&](const fastgltf::BufferView &bufferView) {
            if (const auto &bufferViewTarget = bufferView.target) {
                ImGui::TextUnformatted(to_string(*bufferViewTarget));
            }
            else {
                ImGui::TextDisabled("-");
            }
        }, ImGuiTableColumnFlags_WidthFixed });
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetImages(std::span<fastgltf::Image> images, const std::filesystem::path &assetDir) {
    ImGui::TableWithVirtualization(
        "gltf-images-table",
        ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
        images,
        ImGui::ColumnInfo { "Name", [](std::size_t rowIndex, fastgltf::Image &image) {
            ImGui::WithID(rowIndex, [&]() {
                ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                ImGui::InputTextWithHint("##name", "<empty>", &image.name);
            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "MIME", [](const fastgltf::Image &image) {
            visit([](const auto &source) {
                if constexpr (requires { { source.mimeType } -> std::convertible_to<fastgltf::MimeType>; }) {
                    ImGui::TextUnformatted(to_string(source.mimeType));
                }
                else {
                    ImGui::TextDisabled("-");
                }
            }, image.data);
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Location", [&](std::size_t i, const fastgltf::Image &image) {
            visit(fastgltf::visitor {
                [](const fastgltf::sources::Array&) {
                    ImGui::TextUnformatted("Embedded (Array)"sv);
                },
                [](const fastgltf::sources::BufferView &bufferView) {
                    ImGui::TextUnformatted(tempStringBuffer.write("BufferView ({})", bufferView.bufferViewIndex));
                },
                [&](const fastgltf::sources::URI &uri) {
                    ImGui::WithID(i, [&]() {
                        ImGui::TextLinkOpenURL(ICON_FA_EXTERNAL_LINK, PATH_C_STR(assetDir / uri.uri.fspath()));
                    });
                },
                [](const auto&) {
                    ImGui::TextDisabled("-");
                }
            }, image.data);
        }, ImGuiTableColumnFlags_WidthFixed });
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetSamplers(std::span<fastgltf::Sampler> samplers) {
    ImGui::Table(
        "gltf-samplers-table",
        ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
        samplers,
        ImGui::ColumnInfo { "Name", [](std::size_t rowIndex, fastgltf::Sampler &sampler) {
            ImGui::WithID(rowIndex, [&]() {
                ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                ImGui::InputTextWithHint("##name", "<empty>", &sampler.name);
            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "Filter (Mag/Min)", [](const fastgltf::Sampler &sampler) {
            ImGui::TextUnformatted(
                tempStringBuffer.write(
                    "{} / {}",
                    to_optional(sampler.magFilter).transform(LIFT(to_string)).value_or("-"),
                    to_optional(sampler.minFilter).transform(LIFT(to_string)).value_or("-")));
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Wrap (S/T)", [](const fastgltf::Sampler &sampler) {
            ImGui::TextUnformatted(tempStringBuffer.write("{} / {}", sampler.wrapS, sampler.wrapT));
        }, ImGuiTableColumnFlags_WidthFixed });
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetTextures(
    fastgltf::Asset &asset,
    const imgui::ColorSpaceAndUsageCorrectedTextures &imGuiTextures,
    const gltf::TextureUsages &textureUsages
) {
    if (ImGui::Begin("Textures")) {
        static std::optional<std::size_t> textureIndex = std::nullopt;

        const float windowVisibleX2 = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for (std::size_t i = 0; i < asset.textures.size(); ++i) {
            bool buttonClicked;
            const std::string_view label = nonempty_or(asset.textures[i].name, [&] { return tempStringBuffer.write("Unnamed texture {}", i).view(); });
            ImGui::WithID(i, [&]() {
                buttonClicked = ImGui::ImageButtonWithText("", imGuiTextures.getTextureID(i), label, { 64, 64 });
            });
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(label);
                ImGui::EndTooltip();
            }

            if (buttonClicked) {
                textureIndex = i;
                ImGui::OpenPopup("Texture Viewer");
            }

            const float lastButtonX2 = ImGui::GetItemRectMax().x;
            const float nextButtonX2 = lastButtonX2 + ImGui::GetStyle().ItemSpacing.x + 64;
            if (i + 1 < asset.textures.size() && nextButtonX2 < windowVisibleX2) {
                ImGui::SameLine();
            }
        }

        if (ImGui::BeginPopup("Texture Viewer")) {
            assert(textureIndex && "Texture index is not set.");
            if (*textureIndex >= asset.textures.size()) {
                textureIndex.reset();
                return;
            }

            hoverableImageCheckerboardBackground(imGuiTextures.getTextureID(*textureIndex), { 256, 256 });

            ImGui::SameLine();

            ImGui::WithGroup([&]() {
                fastgltf::Texture &texture = asset.textures[*textureIndex];
                ImGui::InputTextWithHint("Name", "<empty>", &texture.name);
                ImGui::LabelText("Image Index", "%zu", getPreferredImageIndex(texture));
                if (texture.samplerIndex) {
                    ImGui::LabelText("Sampler Index", "%zu", texture.samplerIndex.value_or(-1));
                }
                else {
                    ImGui::WithLabel("Sampler Index", [&]() {
                        ImGui::TextDisabled("-");
                        ImGui::SameLine();
                        ImGui::HelperMarker("(?)", "Default sampler will be used.");
                    });
                }

                ImGui::SeparatorText("Texture used by:");

                ImGui::Table<false>(
                    "",
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                    textureUsages[*textureIndex],
                    ImGui::ColumnInfo { "Material", decomposer([&](std::size_t materialIndex, auto) {
                        ImGui::WithID(materialIndex, [&]() {
                            if (ImGui::TextLink(nonempty_or(asset.materials[materialIndex].name, [&] { return tempStringBuffer.write("Unnamed material {}", materialIndex).view(); }).c_str())) {
                                makeWindowVisible("Material Editor");
                                selectedMaterialIndex = materialIndex;
                            }
                        });
                    }), ImGuiTableColumnFlags_WidthFixed },
                    ImGui::ColumnInfo { "Type", decomposer([](auto, Flags<gltf::TextureUsage> type) {
                        ImGui::TextUnformatted(tempStringBuffer.write("{::s}", type).view());
                    }), ImGuiTableColumnFlags_WidthStretch });
            });
            ImGui::EndPopup();
        }
    }
    ImGui::End();
}

[[nodiscard]] ImGuiID makeDefaultDockState(ImGuiID dockSpaceOverViewport) {
    // ------------------------------------
    // |       |                  |       |
    // |  LST  |                  |  RST  |
    // |       | centralDockSpace |       |
    // |-------|                  |--------
    // |       |                  |       |
    // |  LSB  |------------------|  RSB  |
    // |       |  bottomSidebar   |       |
    // ------------------------------------

    const ImGuiID leftSidebar = ImGui::DockBuilderSplitNode(dockSpaceOverViewport, ImGuiDir_Left, 0.25f, nullptr, &dockSpaceOverViewport);
    const ImGuiID rightSidebar = ImGui::DockBuilderSplitNode(dockSpaceOverViewport, ImGuiDir_Right, 0.33f, nullptr, &dockSpaceOverViewport);

    ImGuiID leftSidebarBottom;
    const ImGuiID leftSidebarTop = ImGui::DockBuilderSplitNode(leftSidebar, ImGuiDir_Up, 0.5f, nullptr, &leftSidebarBottom);

    // leftSidebarTop
    ImGui::DockBuilderDockWindow("Asset Info", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Buffers", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Buffer Views", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Images", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Samplers", leftSidebarTop);
    ImGui::DockBuilderDockWindow("Textures", leftSidebarTop);

    // leftSidebarBottom
    ImGui::DockBuilderDockWindow("Background", leftSidebarBottom);
    ImGui::DockBuilderDockWindow("Scene Hierarchy", leftSidebarBottom);
    ImGui::DockBuilderDockWindow("IBL", leftSidebarBottom);

    ImGuiID rightSidebarBottom;
    const ImGuiID rightSidebarTop = ImGui::DockBuilderSplitNode(rightSidebar, ImGuiDir_Up, 0.5f, nullptr, &rightSidebarBottom);

    // rightSidebarTop
    ImGui::DockBuilderDockWindow("Input control", rightSidebarTop);

    // rightSidebarBottom
    ImGui::DockBuilderDockWindow("Node Inspector", rightSidebarBottom);

    const ImGuiID bottomSidebar = ImGui::DockBuilderSplitNode(dockSpaceOverViewport, ImGuiDir_Down, 0.3f, nullptr, &dockSpaceOverViewport);

    // bottomSidebar
    ImGui::DockBuilderDockWindow("Material Editor", bottomSidebar);
    ImGui::DockBuilderDockWindow("Material Variants", bottomSidebar);
    ImGui::DockBuilderDockWindow("Animation", bottomSidebar);

    ImGui::DockBuilderFinish(dockSpaceOverViewport);

    return dockSpaceOverViewport; // This will represent the central node.
}

vk_gltf_viewer::control::ImGuiTaskCollector::ImGuiTaskCollector(std::queue<Task> &tasks, const ImRect &oldPassthruRect)
    : tasks { tasks } {
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
    ImGuiID dockSpace = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
    if (shouldMakeDefaultDockState) {
        dockSpace = makeDefaultDockState(dockSpace);
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
            if (ImGui::Begin(name)) {
                ImGui::TextUnformatted("Asset not loaded."sv);
            }
            ImGui::End();
        }
    }
    if (!materialEditorCalled) {
        if (ImGui::Begin("Material Editor")) {
            ImGui::TextUnformatted("Asset not loaded."sv);
        }
        ImGui::End();
    }
    if (!sceneHierarchyCalled) {
        if (ImGui::Begin("Scene Hierarchy")) {
            ImGui::TextUnformatted("Asset not loaded."sv);
        }
        ImGui::End();
    }
    if (!nodeInspectorCalled) {
        if (ImGui::Begin("Node Inspector")) {
            ImGui::TextUnformatted("Asset not loaded."sv);
        }
        ImGui::End();
    }
    if (!imageBasedLightingCalled) {
        if (ImGui::Begin("IBL")) {
            ImGui::TextUnformatted("Input equirectangular map not loaded."sv);
        }
        ImGui::End();
    }

    ImGui::Render();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::menuBar(
    const std::list<std::filesystem::path> &recentGltfs,
    const std::list<std::filesystem::path> &recentSkyboxes,
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
                    for (const std::filesystem::path &path : recentGltfs) {
                        if (ImGui::MenuItem(PATH_C_STR(path))) {
                            tasks.emplace(std::in_place_type<task::LoadGltf>, path);
                        }
                    }
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
                    for (const std::filesystem::path &path : recentSkyboxes) {
                        if (ImGui::MenuItem(PATH_C_STR(path))) {
                            tasks.emplace(std::in_place_type<task::LoadEqmap>, path);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void vk_gltf_viewer::control::ImGuiTaskCollector::animations(const fastgltf::Asset &asset, std::vector<bool> &animationEnabled) {
    if (ImGui::Begin("Animation")) {
        for (auto &&[animation, enabled] : std::views::zip(asset.animations, animationEnabled)) {
            bool enabledBool = enabled;
            if (ImGui::Checkbox(nonempty_or(animation.name, []() -> cpp_util::cstring_view { return "<Unnamed animation>"; }).c_str(), &enabledBool)) {
                enabled = enabledBool;
            }
        }
    }
    ImGui::End();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::assetInspector(
    fastgltf::Asset &asset,
    const std::filesystem::path &assetDir
) {
    if (ImGui::Begin("Asset Info")) {
        assetInfo(*asset.assetInfo);
    }
    ImGui::End();

    if (ImGui::Begin("Buffers")) {
        assetBuffers(asset.buffers, assetDir);
    }
    ImGui::End();

    if (ImGui::Begin("Buffer Views")) {
        assetBufferViews(asset.bufferViews, asset.buffers);
    }
    ImGui::End();

    if (ImGui::Begin("Images")) {
        assetImages(asset.images, assetDir);
    }
    ImGui::End();

    if (ImGui::Begin("Samplers")) {
        assetSamplers(asset.samplers);
    }
    ImGui::End();

    assetInspectorCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::materialEditor(
    fastgltf::Asset &asset,
    const imgui::ColorSpaceAndUsageCorrectedTextures &imGuiTextures
) {
    if (ImGui::Begin("Material Editor")) {
        assert(!selectedMaterialIndex || *selectedMaterialIndex < asset.materials.size()
            && "selectedMaterialIndex exceeds the number of materials. Did you forget to update it after glTF is reloaded?");

        const char* const previewText = [&]() {
            if (asset.materials.empty()) return "<empty>";
            else if (selectedMaterialIndex) return nonempty_or(
                asset.materials[*selectedMaterialIndex].name,
                [&]() { return tempStringBuffer.write("<Unnamed material {}>", *selectedMaterialIndex).view(); }).c_str();
            else return "<select...>";
        }();
        if (ImGui::BeginCombo("Material", previewText)) {
            for (const auto &[i, material] : asset.materials | ranges::views::enumerate) {
                const bool isSelected = i == selectedMaterialIndex;
                ImGui::WithID(i, [&]() {
                    if (ImGui::Selectable(nonempty_or(material.name, [&]() { return tempStringBuffer.write("<Unnamed material {}>", i).view(); }).c_str(), isSelected)) {
                        selectedMaterialIndex.emplace(i);
                    }
                });
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        if (selectedMaterialIndex) {
            fastgltf::Material &material = asset.materials[*selectedMaterialIndex];
            const auto notifyPropertyChanged = [&](task::MaterialPropertyChanged::Property property) {
                tasks.emplace(std::in_place_type<task::MaterialPropertyChanged>, *selectedMaterialIndex, property);
            };

            ImGui::InputTextWithHint("Name", "<empty>", &material.name);

            if (ImGui::Checkbox("Double sided", &material.doubleSided)) {
                notifyPropertyChanged(task::MaterialPropertyChanged::DoubleSided);
            }

            if (ImGui::Checkbox("KHR_materials_unlit", &material.unlit)) {
                notifyPropertyChanged(task::MaterialPropertyChanged::Unlit);
            }

            constexpr std::array alphaModes { "OPAQUE", "MASK", "BLEND" };
            if (int alphaMode = static_cast<int>(material.alphaMode); ImGui::Combo("Alpha mode", &alphaMode, alphaModes.data(), alphaModes.size())) {
                material.alphaMode = static_cast<fastgltf::AlphaMode>(alphaMode);
                notifyPropertyChanged(task::MaterialPropertyChanged::AlphaMode);
            }

            ImGui::WithDisabled([&]() {
                if (ImGui::DragFloat("Alpha cutoff", &material.alphaCutoff, 0.01f, 0.f, 1.f)) {
                    notifyPropertyChanged(task::MaterialPropertyChanged::AlphaCutoff);
                }
            }, material.alphaMode != fastgltf::AlphaMode::Mask);

            constexpr auto texcoordOverriddenMarker = []() {
                ImGui::SameLine();
                ImGui::HelperMarker("(overridden)", "This value is overridden by KHR_texture_transform extension.");
            };
            const auto textureTransformControl = [&](fastgltf::TextureInfo &textureInfo, gltf::TextureUsage usage) -> void {
                const auto [enabledProp, changeProp] = [&]() -> std::array<task::MaterialPropertyChanged::Property, 2> {
                    using enum task::MaterialPropertyChanged::Property;
                    switch (usage) {
                        case gltf::TextureUsage::BaseColor:
                            return { BaseColorTextureTransformEnabled, BaseColorTextureTransform };
                        case gltf::TextureUsage::MetallicRoughness:
                            return { MetallicRoughnessTextureTransformEnabled, MetallicRoughnessTextureTransform };
                        case gltf::TextureUsage::Normal:
                            return { NormalTextureTransformEnabled, NormalTextureTransform };
                        case gltf::TextureUsage::Occlusion:
                            return { OcclusionTextureTransformEnabled, OcclusionTextureTransform };
                        case gltf::TextureUsage::Emissive:
                            return { EmissiveTextureTransformEnabled, EmissiveTextureTransform };
                    }
                    std::unreachable();
                }();

                bool useTextureTransform = textureInfo.transform != nullptr;
                bool isChangePropNotified = false; // prevent double notifying
                if (ImGui::Checkbox("KHR_texture_transform", &useTextureTransform)) {
                    if (useTextureTransform) {
                        textureInfo.transform = std::make_unique<fastgltf::TextureTransform>();
                        // Since the new identity 3x2 matrix has to be written into the GPU buffer, texture transform
                        // change must be notified.
                        notifyPropertyChanged(changeProp);
                        isChangePropNotified = true;
                    }
                    else {
                        textureInfo.transform.reset();
                    }

                    // Always notify texture transform enabled status is changed to request the re-calculate pipelines.
                    notifyPropertyChanged(enabledProp);
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
                ImGui::WithID("basecolor", [&]() {
                    auto &baseColorTextureInfo = material.pbrData.baseColorTexture;
                    if (baseColorTextureInfo) {
                        hoverableImageCheckerboardBackground(imGuiTextures.getTextureID(baseColorTextureInfo->textureIndex), { 128.f, 128.f });
                        ImGui::SameLine();
                    }
                    ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                        ImGui::WithGroup([&]() {
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

                                textureTransformControl(*baseColorTextureInfo, gltf::TextureUsage::BaseColor);
                            }
                        }, baseColorTextureInfo.has_value());
                    });
                });

                ImGui::WithDisabled([&]() {
                    ImGui::SeparatorText("Metallic/Roughness");
                    ImGui::WithID("metallicroughness", [&]() {
                        auto &metallicRoughnessTextureInfo = material.pbrData.metallicRoughnessTexture;
                        if (metallicRoughnessTextureInfo) {
                            hoverableImage(imGuiTextures.getMetallicTextureID(*selectedMaterialIndex), { 128.f, 128.f });
                            ImGui::SameLine();
                            hoverableImage(imGuiTextures.getRoughnessTextureID(*selectedMaterialIndex), { 128.f, 128.f });
                            ImGui::SameLine();
                        }
                        ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            ImGui::WithGroup([&]() {
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

                                    textureTransformControl(*metallicRoughnessTextureInfo, gltf::TextureUsage::MetallicRoughness);
                                }
                            });
                        });
                    });
                }, material.unlit);
            }

            ImGui::WithID("normal", [&]() {
                if (auto &textureInfo = material.normalTexture; textureInfo && ImGui::CollapsingHeader("Normal Mapping")) {
                    ImGui::WithDisabled([&]() {
                        hoverableImage(imGuiTextures.getNormalTextureID(*selectedMaterialIndex), { 128.f, 128.f });
                        ImGui::SameLine();
                        ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            ImGui::WithGroup([&]() {
                                if (ImGui::DragFloat("Scale", &textureInfo->scale, 0.01f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::NormalScale);
                                }
                                ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);

                                const std::size_t texcoordIndex = getTexcoordIndex(*textureInfo);
                                ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                if (texcoordIndex != textureInfo->texCoordIndex) {
                                    texcoordOverriddenMarker();
                                }

                                textureTransformControl(*textureInfo, gltf::TextureUsage::Normal);
                            });
                        });
                    }, material.unlit);
                }
            });

            ImGui::WithID("occlusion", [&]() {
                if (auto &textureInfo = material.occlusionTexture; textureInfo && ImGui::CollapsingHeader("Occlusion Mapping")) {
                    ImGui::WithDisabled([&]() {
                        hoverableImage(imGuiTextures.getOcclusionTextureID(*selectedMaterialIndex), { 128.f, 128.f });
                        ImGui::SameLine();
                        ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            ImGui::WithGroup([&]() {
                                if (ImGui::DragFloat("Strength", &textureInfo->strength, 0.01f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::OcclusionStrength);
                                }
                                ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);

                                const std::size_t texcoordIndex = getTexcoordIndex(*textureInfo);
                                ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                if (texcoordIndex != textureInfo->texCoordIndex) {
                                    texcoordOverriddenMarker();
                                }

                                textureTransformControl(*textureInfo, gltf::TextureUsage::Occlusion);
                            });
                        });
                    }, material.unlit);
                }
            });

            ImGui::WithID("emissive", [&]() {
                if (ImGui::CollapsingHeader("Emissive")) {
                    ImGui::WithDisabled([&]() {
                        auto &textureInfo = material.emissiveTexture;
                        if (textureInfo) {
                            hoverableImage(imGuiTextures.getEmissiveTextureID(*selectedMaterialIndex), { 128.f, 128.f });
                            ImGui::SameLine();
                        }
                        ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                            ImGui::WithGroup([&]() {
                                if (ImGui::DragFloat3("Factor", material.emissiveFactor.data(), 0.01f, 0.f, 1.f)) {
                                    notifyPropertyChanged(task::MaterialPropertyChanged::EmissiveFactor);
                                }
                                if (textureInfo) {
                                    ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);

                                    const std::size_t texcoordIndex = getTexcoordIndex(*textureInfo);
                                    ImGui::LabelText("Texture Coordinate", "%zu", texcoordIndex);
                                    if (texcoordIndex != textureInfo->texCoordIndex) {
                                        texcoordOverriddenMarker();
                                    }

                                    textureTransformControl(*textureInfo, gltf::TextureUsage::Emissive);
                                }
                            }, textureInfo.has_value());
                        });
                    }, material.unlit);
                }
            });
        }
    }
    ImGui::End();

    materialEditorCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::materialVariants(const fastgltf::Asset &asset) {
    assert(!asset.materialVariants.empty());

    if (ImGui::Begin("Material Variants")) {
        static int selectedMaterialVariantIndex = 0;
        if (asset.materialVariants.size() <= selectedMaterialVariantIndex) {
            selectedMaterialVariantIndex = 0;
        }

        for (const auto &[i, variantName] : asset.materialVariants | ranges::views::enumerate) {
            if (ImGui::RadioButton(variantName.c_str(), &selectedMaterialVariantIndex, i)) {
                tasks.emplace(std::in_place_type<task::SelectMaterialVariants>, i);
            }
        }
    }
    ImGui::End();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::sceneHierarchy(
    fastgltf::Asset &asset,
    std::size_t sceneIndex,
    gltf::StateCachedNodeVisibilityStructure &nodeVisibility,
    const std::optional<std::size_t> &hoveringNodeIndex,
    std::unordered_set<std::size_t> &selectedNodeIndices
) {
    if (ImGui::Begin("Scene Hierarchy")) {
        if (ImGui::BeginCombo("Scene", nonempty_or(asset.scenes[sceneIndex].name, [&]() { return tempStringBuffer.write("<Unnamed scene {}>", sceneIndex).view(); }).c_str())) {
            for (const auto &[i, scene] : asset.scenes | ranges::views::enumerate) {
                const bool isSelected = i == sceneIndex;
                if (ImGui::Selectable(nonempty_or(scene.name, [&]() { return tempStringBuffer.write("<Unnamed scene {}>", i).view(); }).c_str(), isSelected)) {
                    tasks.emplace(std::in_place_type<task::ChangeScene>, i);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::InputTextWithHint("Name", "<empty>", &asset.scenes[sceneIndex].name);

        static bool mergeSingleChildNodes = true;
        ImGui::Checkbox("Merge single child nodes", &mergeSingleChildNodes);
        ImGui::SameLine();
        ImGui::HelperMarker("(?)", "If a node has only single child and both are not representing any mesh, light or camera, they will be combined and slash-separated name will be shown instead.");

        // FIXME: due to the Clang 18's explicit object parameter bug, const fastgltf::Asset& is passed (but it is unnecessary). Remove the parameter when fixed.
        const auto addChildNode = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex) -> void {
            std::vector<std::size_t> ancestorNodeIndices;
            if (mergeSingleChildNodes) {
                for (const fastgltf::Node *node = &asset.nodes[nodeIndex];
                    node->children.size() == 1
                        && !node->cameraIndex && !node->lightIndex && !node->meshIndex
                        && !asset.nodes[node->children[0]].cameraIndex && !asset.nodes[node->children[0]].lightIndex && !asset.nodes[node->children[0]].meshIndex;
                    nodeIndex = node->children[0], node = &asset.nodes[nodeIndex]) {
                    ancestorNodeIndices.push_back(nodeIndex);
                }
            }

            const fastgltf::Node &node = asset.nodes[nodeIndex];

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();

            ImGui::WithID(nodeIndex, [&]() {
                // --------------------
                // TreeNode.
                // --------------------

                bool isNodeSelected = std::ranges::all_of(ancestorNodeIndices, LIFT(selectedNodeIndices.contains)) && selectedNodeIndices.contains(nodeIndex);
                const bool isTreeNodeOpen = ImGui::WithStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), [&]() {
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowOverlap;
                    if (nodeIndex == hoveringNodeIndex) flags |= ImGuiTreeNodeFlags_Framed;
                    if (isNodeSelected) flags |= ImGuiTreeNodeFlags_Selected;
                    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_Leaf;

                    ImGui::AlignTextToFramePadding();

                    tempStringBuffer.clear();
                    const auto appendNodeLabel = [&](std::size_t nodeIndex) {
                        const fastgltf::Node &node = asset.nodes[nodeIndex];

                        if (std::string_view name = node.name; name.empty()) {
                            tempStringBuffer.append("<Unnamed node {}>", nodeIndex);
                        }
                        else {
                            tempStringBuffer.append(name);
                        }
                    };
                    for (std::size_t ancestorNodeIndex : ancestorNodeIndices) {
                        appendNodeLabel(ancestorNodeIndex);
                        tempStringBuffer.append(" / ");
                    }
                    appendNodeLabel(nodeIndex);
                    tempStringBuffer.append("##treenode");

                    return ImGui::TreeNodeEx(tempStringBuffer.view().c_str(), flags);
                }, nodeIndex == hoveringNodeIndex);

                // Handle clicking tree node.
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    if (ImGui::GetIO().KeyCtrl) {
                        // Toggle the selection.
                        if (isNodeSelected) {
                            for (std::size_t ancestorNodeIndex : ancestorNodeIndices) {
                                selectedNodeIndices.erase(ancestorNodeIndex);
                            }
                            selectedNodeIndices.erase(nodeIndex);
                            isNodeSelected = false;
                        }
                        else {
                            for (std::size_t ancestorNodeIndex : ancestorNodeIndices) {
                                selectedNodeIndices.emplace(ancestorNodeIndex);
                            }
                            selectedNodeIndices.emplace(nodeIndex);
                            isNodeSelected = true;
                        }
                        tasks.emplace(std::in_place_type<task::NodeSelectionChanged>);
                    }
                    else {
                        selectedNodeIndices = { std::from_range, ancestorNodeIndices };
                        selectedNodeIndices.emplace(nodeIndex);
                        tasks.emplace(std::in_place_type<task::NodeSelectionChanged>);
                    }
                }

                // Handle hovering tree node.
                if (ImGui::IsItemHovered() && nodeIndex != hoveringNodeIndex) {
                    tasks.emplace(std::in_place_type<task::HoverNodeFromGui>, nodeIndex);
                }

                // Open context menu when right-click the tree node.
                if (ImGui::BeginPopupContextItem()) {
                    // If the current node is the only selected node, and it is leaf, it is guaranteed that the selection will be not changed.
                    ImGui::WithDisabled([&]() {
                        if (ImGui::Selectable("Select from here")) {
                            selectedNodeIndices.clear();
                            gltf::algorithm::traverseNode(asset, ancestorNodeIndices.empty() ? nodeIndex : ancestorNodeIndices[0], [&](std::size_t nodeIndex) {
                                selectedNodeIndices.emplace(nodeIndex);
                            });

                            tasks.emplace(std::in_place_type<task::NodeSelectionChanged>);
                        }
                    }, selectedNodeIndices.size() == 1 && isNodeSelected && node.children.empty());

                    if (isNodeSelected) {
                        ImGui::Separator();

                        // If node is the only selected node, visibility can be determined in a constant time.
                        const std::optional<bool> determinedVisibility = value_if<bool>(selectedNodeIndices.size() == 1, nodeVisibility.getVisibility(nodeIndex));

                        // If visibility is hidden or cannot be determined, show the menu.
                        if (!determinedVisibility.value_or(false) && ImGui::Selectable("Make all selected nodes visible")) {
                            for (std::size_t nodeIndex : selectedNodeIndices) {
                                if (!asset.nodes[nodeIndex].meshIndex) continue;

                                if (!nodeVisibility.getVisibility(nodeIndex)) {
                                    nodeVisibility.setVisibility(nodeIndex, true);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            }
                        }

                        // If visibility is visible or cannot be determined, show the menu.
                        if (determinedVisibility.value_or(true) && ImGui::Selectable("Make all selected nodes invisible")) {
                            for (std::size_t nodeIndex : selectedNodeIndices) {
                                if (!asset.nodes[nodeIndex].meshIndex) continue;

                                if (nodeVisibility.getVisibility(nodeIndex)) {
                                    nodeVisibility.setVisibility(nodeIndex, false);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            }
                        }

                        if (!determinedVisibility && ImGui::Selectable("Toggle all selected node visibility")) {
                            for (std::size_t nodeIndex : selectedNodeIndices) {
                                if (!asset.nodes[nodeIndex].meshIndex) continue;

                                nodeVisibility.flipVisibility(nodeIndex);
                                tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                            }
                        }
                    }
                    else if (auto state = nodeVisibility.getState(nodeIndex); state != gltf::StateCachedNodeVisibilityStructure::State::Indeterminate) {
                        ImGui::Separator();

                        if (state != gltf::StateCachedNodeVisibilityStructure::State::AllVisible && ImGui::Selectable("Make visible from here")) {
                            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                                if (!asset.nodes[nodeIndex].meshIndex) return;

                                if (!nodeVisibility.getVisibility(nodeIndex)) {
                                    nodeVisibility.setVisibility(nodeIndex, true);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            });
                        }

                        if (state != gltf::StateCachedNodeVisibilityStructure::State::AllInvisible && ImGui::Selectable("Make invisible from here")) {
                            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                                if (!asset.nodes[nodeIndex].meshIndex) return;

                                if (nodeVisibility.getVisibility(nodeIndex)) {
                                    nodeVisibility.setVisibility(nodeIndex, false);
                                    tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                                }
                            });
                        }

                        if (state == gltf::StateCachedNodeVisibilityStructure::State::Intermediate && ImGui::Selectable("Toggle visibility from here")) {
                            gltf::algorithm::traverseNode(asset, nodeIndex, [&](std::size_t nodeIndex) {
                                if (!asset.nodes[nodeIndex].meshIndex) return;

                                nodeVisibility.flipVisibility(nodeIndex);
                                tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                            });
                        }
                    }

                    ImGui::EndPopup();
                }

                if (global::shouldNodeInSceneHierarchyScrolledToBeVisible &&
                    selectedNodeIndices.size() == 1 && nodeIndex == *selectedNodeIndices.begin()) {
                    ImGui::ScrollToItem();
                    global::shouldNodeInSceneHierarchyScrolledToBeVisible = false;
                }

                // --------------------
                // Node mesh.
                // --------------------

                if (node.meshIndex) {
                    ImGui::TableSetColumnIndex(1);

                    if (bool visible = nodeVisibility.getVisibility(nodeIndex); ImGui::Checkbox("##visibility", &visible)) {
                        nodeVisibility.flipVisibility(nodeIndex);
                        tasks.emplace(std::in_place_type<task::NodeVisibilityChanged>, nodeIndex);
                    }
                }

                // --------------------
                // Node light.
                // --------------------

                if (node.lightIndex) {
                    ImGui::TableSetColumnIndex(2);
                    ImGui::WithDisabled([&]() {
                        bool checked = false;
                        ImGui::Checkbox(ICON_FA_LIGHTBULB_O, &checked); // TODO
                    });
                }

                // --------------------
                // Node camera.
                // --------------------

                if (node.cameraIndex) {
                    ImGui::TableSetColumnIndex(3);
                    ImGui::WithDisabled([&]() {
                        ImGui::Button(ICON_FA_CAMERA); // TODO
                    });
                }

                if (isTreeNodeOpen) {
                    for (std::size_t childNodeIndex : node.children) {
                        self(asset, childNodeIndex);
                    }
                    ImGui::TreePop();
                }
            });
        };

        if (ImGui::BeginTable("scene-hierarchy-table", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(ICON_FA_CUBE, ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(ICON_FA_LIGHTBULB_O, ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn(ICON_FA_CAMERA, ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();

            for (std::size_t nodeIndex : asset.scenes[sceneIndex].nodeIndices) {
                addChildNode(asset, nodeIndex);
            }

            ImGui::EndTable();
        }
    }
    ImGui::End();

    sceneHierarchyCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::nodeInspector(
    fastgltf::Asset &asset,
    const std::vector<bool> &animationEnabled,
    const gltf::NodeAnimationUsages &nodeAnimationUsages,
    std::unordered_set<std::size_t> &selectedNodeIndices
) {
    if (ImGui::Begin("Node Inspector")) {
        if (selectedNodeIndices.empty()) {
            ImGui::TextUnformatted("No nodes are selected."sv);
        }
        else if (selectedNodeIndices.size() == 1) {
            const std::size_t selectedNodeIndex = *selectedNodeIndices.begin();
            fastgltf::Node &node = asset.nodes[selectedNodeIndex];
            ImGui::InputTextWithHint("Name", "<empty>", &node.name);

            ImGui::SeparatorText("Transform");

            bool isTransformUsedInAnimation = false;
            Flags<gltf::NodeAnimationUsage> nodeAnimationUsage{};
            for (const auto &[animationIndex, usage] : nodeAnimationUsages[selectedNodeIndex]) {
                if (usage | (gltf::NodeAnimationUsage::Translation | gltf::NodeAnimationUsage::Rotation | gltf::NodeAnimationUsage::Scale)) {
                    isTransformUsedInAnimation = true;
                }
                if (animationEnabled[animationIndex]) {
                    nodeAnimationUsage |= usage;
                }
            }

            // Using transform matrix is prohibited when node transform is used in animation as TRS.
            ImGui::WithDisabled([&]() {
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

            // If node TRS transform is used by an animation now, it cannot be modified by GUI.
            std::visit(fastgltf::visitor {
                [&](fastgltf::TRS &trs) {
                    bool transformChanged = false;
                    ImGui::WithDisabled([&]() {
                        transformChanged |= ImGui::DragFloat3("Translation", trs.translation.data());
                    }, nodeAnimationUsage & gltf::NodeAnimationUsage::Translation);
                    ImGui::WithDisabled([&]() {
                        transformChanged |= ImGui::DragFloat4("Rotation", trs.rotation.value_ptr());
                    }, nodeAnimationUsage & gltf::NodeAnimationUsage::Rotation);
                    ImGui::WithDisabled([&]() {
                        transformChanged |= ImGui::DragFloat3("Scale", trs.scale.data());
                    }, nodeAnimationUsage & gltf::NodeAnimationUsage::Scale);

                    if (transformChanged) {
                        tasks.emplace(std::in_place_type<task::NodeLocalTransformChanged>, selectedNodeIndex);
                    }
                },
                [&](fastgltf::math::fmat4x4 &matrix) {
                    // | operator cannot be chained, because of the short circuit evaluation.
                    bool transformChanged = ImGui::DragFloat4("Column 0", matrix.col(0).data());
                    transformChanged |= ImGui::DragFloat4("Column 1", matrix.col(1).data());
                    transformChanged |= ImGui::DragFloat4("Column 2", matrix.col(2).data());
                    transformChanged |= ImGui::DragFloat4("Column 3", matrix.col(3).data());

                    if (transformChanged) {
                        tasks.emplace(std::in_place_type<task::NodeLocalTransformChanged>, selectedNodeIndex);
                    }
                },
            }, node.transform);

            if (!node.instancingAttributes.empty() && ImGui::TreeNodeEx("EXT_mesh_gpu_instancing", ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
                ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                    attributeTable(node.instancingAttributes | std::views::transform([&](const fastgltf::Attribute &attribute) {
                        return std::pair<std::string_view, const fastgltf::Accessor&> { attribute.name, asset.accessors[attribute.accessorIndex] };
                    }));
                });
            }

            if (const std::span morphTargetWeights = getTargetWeights(node, asset); !morphTargetWeights.empty()) {
                ImGui::SeparatorText("Morph Target Weights");

                // If node weights are used by an animation now, they cannot be modified by GUI.
                ImGui::WithDisabled([&]() {
                    for (auto &&[i, weight] : morphTargetWeights | ranges::views::enumerate) {
                        if (ImGui::DragFloat(tempStringBuffer.write("Weight {}", i).view().c_str(), &weight, 0.01f)) {
                            tasks.emplace(std::in_place_type<task::MorphTargetWeightChanged>, selectedNodeIndex, i, 1);
                        }
                    }
                }, nodeAnimationUsage & gltf::NodeAnimationUsage::Weights);
            }

            if (ImGui::BeginTabBar("node-tab-bar")) {
                if (node.meshIndex && ImGui::BeginTabItem("Mesh")) {
                    fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &mesh.name);

                    for (auto &&[primitiveIndex, primitive]: mesh.primitives | ranges::views::enumerate) {
                        if (ImGui::CollapsingHeader(tempStringBuffer.write("Primitive {}", primitiveIndex).view().c_str())) {
                            ImGui::LabelText("Type", "%s", to_string(primitive.type).c_str());
                            if (primitive.materialIndex) {
                                ImGui::WithID(*primitive.materialIndex, [&]() {
                                    ImGui::WithLabel("Material"sv, [&]() {
                                        if (ImGui::TextLink(nonempty_or(asset.materials[*primitive.materialIndex].name, [&]() { return tempStringBuffer.write("<Unnamed material {}>", *primitive.materialIndex).view(); }).c_str())) {
                                            makeWindowVisible("Material Editor");
                                            selectedMaterialIndex = *primitive.materialIndex;
                                        }
                                    });
                                });
                            }
                            else {
                                ImGui::WithDisabled([]() {
                                    ImGui::LabelText("Material", "-");
                                });
                            }

                            attributeTable(ranges::views::concat(
                                to_range(to_optional(primitive.indicesAccessor).transform([&](std::size_t accessorIndex) {
                                    return std::pair<std::string_view, const fastgltf::Accessor&> { "Index"sv, asset.accessors[accessorIndex] };
                                })),
                                primitive.attributes | std::views::transform([&](const fastgltf::Attribute &attribute) {
                                    return std::pair<std::string_view, const fastgltf::Accessor&> { attribute.name, asset.accessors[attribute.accessorIndex] };
                                })));
                        }
                    }

                    if (ImGui::InputInt("Bound fp precision", &boundFpPrecision)) {
                        boundFpPrecision = std::clamp(boundFpPrecision, 0, 9);
                    }

                    ImGui::EndTabItem();
                }
                if (node.cameraIndex && ImGui::BeginTabItem("Camera")) {
                    auto &[camera, name] = asset.cameras[*node.cameraIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &name);

                    ImGui::WithDisabled([&]() {
                        if (int type = camera.index(); ImGui::Combo("Type", &type, "Perspective\0Orthographic\0")) {
                            // TODO
                        }

                    });

                    constexpr auto noAffectHelperMarker = []() {
                        ImGui::SameLine();
                        ImGui::HelperMarker("(?)", "This property will not affect to the actual rendering, as it is calculated from the actual viewport size.");
                    };

                    visit(fastgltf::visitor {
                        [&](fastgltf::Camera::Perspective &camera) {
                            if (camera.aspectRatio) {
                                ImGui::DragFloat("Aspect Ratio", &*camera.aspectRatio, 0.01f, 1e-2f, 1e-2f);
                            }
                            else {
                                ImGui::WithDisabled([this]() {
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
                                ImGui::WithDisabled([]() {
                                    float zFar = std::numeric_limits<float>::infinity();
                                    ImGui::DragFloat("##far", &zFar);
                                });
                                ImGui::PopItemWidth();
                                ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                                ImGui::TextUnformatted("Near/Far");
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
                    fastgltf::Light &light = asset.lights[*node.lightIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &light.name);
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }
        }
        else {
            ImGui::TextUnformatted("Multiple nodes are selected."sv);

            ImGui::BeginListBox("##candidates");

            // TODO: avoid calculation?
            std::vector sortedIndices { std::from_range, selectedNodeIndices };
            std::ranges::sort(sortedIndices);

            for (std::size_t nodeIndex : sortedIndices) {
                bool selected = false;
                ImGui::WithID(nodeIndex, [&]() {
                    selected = ImGui::Selectable(nonempty_or(asset.nodes[nodeIndex].name, [&]() {
                        return tempStringBuffer.write("<Unnamed node {}>", nodeIndex).view();
                    }).c_str());
                });

                if (ImGui::IsItemHovered()) {
                    tasks.emplace(std::in_place_type<task::HoverNodeFromGui>, nodeIndex);
                }

                if (selected) {
                    selectedNodeIndices = { nodeIndex };
                    break;
                }
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();

    nodeInspectorCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::background(
    bool canSelectSkyboxBackground,
    full_optional<glm::vec3> &solidBackground
) {
    if (ImGui::Begin("Background")) {
        const bool useSolidBackground = solidBackground.has_value();
        // If canSelectSkyboxBackground is false, the user cannot select the skybox background.
        ImGui::WithDisabled([&]() {
            if (ImGui::RadioButton("Use cubemap image from equirectangular map", !useSolidBackground)) {
                solidBackground.set_active(false);
            }
        }, !canSelectSkyboxBackground);

        if (ImGui::RadioButton("Use solid color", useSolidBackground)) {
            solidBackground.set_active(true);
        }
        ImGui::WithDisabled([&]() {
            ImGui::ColorPicker3("Color", value_ptr(*solidBackground));
        }, !useSolidBackground);
    }
    ImGui::End();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::imageBasedLighting(
    const AppState::ImageBasedLighting &info,
    ImTextureID eqmapTextureImGuiDescriptorSet
) {
    if (ImGui::Begin("IBL")) {
        if (ImGui::CollapsingHeader("Equirectangular map")) {
            const float eqmapAspectRatio = static_cast<float>(info.eqmap.dimension.y) / info.eqmap.dimension.x;
            const ImVec2 eqmapTextureSize = ImVec2 { 1.f, eqmapAspectRatio } * ImGui::GetContentRegionAvail().x;
            hoverableImage(eqmapTextureImGuiDescriptorSet, eqmapTextureSize);

            ImGui::WithLabel("File"sv, [&]() {
                ImGui::TextLinkOpenURL(PATH_C_STR(info.eqmap.path.filename()), PATH_C_STR(info.eqmap.path));
            });
            ImGui::LabelText("Dimension", "%ux%u", info.eqmap.dimension.x, info.eqmap.dimension.y);
        }

        if (ImGui::CollapsingHeader("Cubemap")) {
            ImGui::LabelText("Size", "%u", info.cubemap.size);
        }

        if (ImGui::CollapsingHeader("Diffuse Irradiance")) {
            ImGui::TextUnformatted("Spherical harmonic coefficients (up to 3rd band)"sv);
            constexpr std::array bandLabels { "L0"sv, "L1_1"sv, "L10"sv, "L11"sv, "L2_2"sv, "L2_1"sv, "L20"sv, "L21"sv, "L22"sv };
            ImGui::Table<false>(
                "spherical-harmonic-coeffs",
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                std::views::zip(bandLabels, info.diffuseIrradiance.sphericalHarmonicCoefficients),
                ImGui::ColumnInfo { "Band", decomposer([](std::string_view label, const auto&) { ImGui::TextUnformatted(label); }) },
                ImGui::ColumnInfo { "x", decomposer([](auto, const glm::vec3 &coeff) { ImGui::TextUnformatted(tempStringBuffer.write(coeff.x)); }) },
                ImGui::ColumnInfo { "y", decomposer([](auto, const glm::vec3 &coeff) { ImGui::TextUnformatted(tempStringBuffer.write(coeff.y)); }) },
                ImGui::ColumnInfo { "z", decomposer([](auto, const glm::vec3 &coeff) { ImGui::TextUnformatted(tempStringBuffer.write(coeff.z)); }) });
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

void vk_gltf_viewer::control::ImGuiTaskCollector::inputControl(
    Camera &camera,
    bool &automaticNearFarPlaneAdjustment,
    bool &useFrustumCulling,
    full_optional<AppState::Outline> &hoveringNodeOutline,
    full_optional<AppState::Outline> &selectedNodeOutline
) {
    if (ImGui::Begin("Input control")){
        if (ImGui::CollapsingHeader("Camera")) {
            bool cameraViewChanged = false;
            cameraViewChanged |= ImGui::DragFloat3("Position", value_ptr(camera.position), 0.1f);
            if (ImGui::DragFloat3("Direction", value_ptr(camera.direction), 0.1f, -1.f, 1.f)) {
                camera.direction = normalize(camera.direction);
                cameraViewChanged = true;
            }
            if (ImGui::DragFloat3("Up", value_ptr(camera.up), 0.1f, -1.f, 1.f)) {
                camera.up = normalize(camera.up);
                cameraViewChanged = true;
            }

            if (cameraViewChanged) {
                tasks.emplace(std::in_place_type<task::CameraViewChanged>);
            }

            if (float fovInDegree = glm::degrees(camera.fov); ImGui::DragFloat("FOV", &fovInDegree, 0.1f, 15.f, 120.f, "%.2f deg")) {
                camera.fov = glm::radians(fovInDegree);
            }

            if (ImGui::Checkbox("Automatic Near/Far Adjustment", &automaticNearFarPlaneAdjustment) && automaticNearFarPlaneAdjustment) {
                tasks.emplace(std::in_place_type<task::TightenNearFarPlane>);
            }
            ImGui::SameLine();
            ImGui::HelperMarker("(?)", "Near/Far plane will be automatically tightened to fit the scene bounding box.");

            ImGui::WithDisabled([&]() {
                ImGui::DragFloatRange2("Near/Far", &camera.zMin, &camera.zMax, 1.f, 1e-6f, 1e-6f, "%.2e", nullptr, ImGuiSliderFlags_Logarithmic);
            }, automaticNearFarPlaneAdjustment);

            ImGui::Checkbox("Use Frustum Culling", &useFrustumCulling);
            ImGui::SameLine();
            ImGui::HelperMarker("(?)", "The primitives outside the camera frustum will be culled.");
        }

        if (ImGui::CollapsingHeader("Node selection")) {
            bool showHoveringNodeOutline = hoveringNodeOutline.has_value();
            if (ImGui::Checkbox("Hovering node outline", &showHoveringNodeOutline)) {
                hoveringNodeOutline.set_active(showHoveringNodeOutline);
            }
            ImGui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##hoveringNodeOutline", &hoveringNodeOutline->thickness, 1.f, 1.f, 1.f);
                ImGui::ColorEdit4("Color##hoveringNodeOutline", value_ptr(hoveringNodeOutline->color));
            }, !showHoveringNodeOutline);

            bool showSelectedNodeOutline = selectedNodeOutline.has_value();
            if (ImGui::Checkbox("Selected node outline", &showSelectedNodeOutline)) {
                selectedNodeOutline.set_active(showSelectedNodeOutline);
            }
            ImGui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##selectedNodeOutline", &selectedNodeOutline->thickness, 1.f, 1.f, 1.f);
                ImGui::ColorEdit4("Color##selectedNodeOutline", value_ptr(selectedNodeOutline->color));
            }, !showSelectedNodeOutline);
        }
    }
    ImGui::End();
}

void vk_gltf_viewer::control::ImGuiTaskCollector::imguizmo(Camera &camera) {
    // Set ImGuizmo rect.
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(centerNodeRect.Min.x, centerNodeRect.Min.y, centerNodeRect.GetWidth(), centerNodeRect.GetHeight());

    constexpr ImVec2 size { 64.f, 64.f };
    constexpr ImU32 background = 0x00000000; // Transparent.
    const glm::mat4 oldView = camera.getViewMatrix();
    glm::mat4 newView = oldView;
    ImGuizmo::ViewManipulate(value_ptr(newView), camera.targetDistance, centerNodeRect.Max - size, size, background);
    if (newView != oldView) {
        const glm::mat4 inverseView = inverse(newView);
        camera.position = inverseView[3];
        camera.direction = -inverseView[2];

        tasks.emplace(std::in_place_type<task::CameraViewChanged>);
    }
}

void vk_gltf_viewer::control::ImGuiTaskCollector::imguizmo(
    Camera &camera,
    fastgltf::Asset &asset,
    std::size_t selectedNodeIndex,
    const fastgltf::math::fmat4x4 &selectedNodeWorldTransform,
    ImGuizmo::OPERATION operation
) {
    // Set ImGuizmo rect.
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(centerNodeRect.Min.x, centerNodeRect.Min.y, centerNodeRect.GetWidth(), centerNodeRect.GetHeight());

    fastgltf::math::fmat4x4 newWorldTransform = selectedNodeWorldTransform;
    if (Manipulate(value_ptr(camera.getViewMatrix()), value_ptr(camera.getProjectionMatrixForwardZ()), operation, ImGuizmo::MODE::LOCAL, newWorldTransform.data())) {
        constexpr auto affineInverse = [](const fastgltf::math::fmat4x4 &m) noexcept {
            const fastgltf::math::fmat3x3 inv = inverse(fastgltf::math::fmat3x3 { m });
            const fastgltf::math::fvec3 l = -inv * fastgltf::math::fvec3 { m.col(3) };
            return fastgltf::math::fmat4x4 {
                fastgltf::math::fvec4 { inv.col(0).x(), inv.col(0).y(), inv.col(0).z(), 0.f },
                fastgltf::math::fvec4 { inv.col(1).x(), inv.col(1).y(), inv.col(1).z(), 0.f },
                fastgltf::math::fvec4 { inv.col(2).x(), inv.col(2).y(), inv.col(2).z(), 0.f },
                fastgltf::math::fvec4 { l.x(), l.y(), l.z(), 1.f },
            };
        };
        const fastgltf::math::fmat4x4 deltaMatrix = affineInverse(selectedNodeWorldTransform) * newWorldTransform;

        visit(fastgltf::visitor {
            [&](fastgltf::math::fmat4x4 &transformMatrix) {
                transformMatrix = transformMatrix * deltaMatrix;
            },
            [&](fastgltf::TRS &trs) {
                fastgltf::math::fmat4x4 transformMatrix = toMatrix(trs);
                transformMatrix = transformMatrix * deltaMatrix;
                decomposeTransformMatrix(transformMatrix, trs.scale, trs.rotation, trs.translation);
            },
        }, asset.nodes[selectedNodeIndex].transform);

        tasks.emplace(std::in_place_type<task::NodeLocalTransformChanged>, selectedNodeIndex);
    }

    constexpr ImVec2 size { 64.f, 64.f };
    constexpr ImU32 background = 0x00000000; // Transparent.
    const glm::mat4 oldView = camera.getViewMatrix();
    glm::mat4 newView = oldView;
    ImGuizmo::ViewManipulate(value_ptr(newView), camera.targetDistance, centerNodeRect.Max - size, size, background);
    if (newView != oldView) {
        const glm::mat4 inverseView = inverse(newView);
        camera.position = inverseView[3];
        camera.direction = -inverseView[2];

        tasks.emplace(std::in_place_type<task::CameraViewChanged>);
    }
}