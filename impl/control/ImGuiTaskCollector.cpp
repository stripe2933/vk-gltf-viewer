module;

#include <cassert>
#include <version>

#include <nfd.hpp>

module vk_gltf_viewer;
import :imgui.TaskCollector;

import std;
import glm;
import imgui.glfw;
import imgui.internal;
import imgui.math;
import imgui.vulkan;
import ImGuizmo;
import vku;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.imgui;
import :helpers.optional;
import :helpers.ranges;
import :helpers.TempStringBuffer;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }
#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#ifdef _MSC_VER
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

using namespace std::string_view_literals;

/**
 * Return \p str if it is not empty, otherwise return the result of \p fallback.
 * @param str Null-terminated non-owning string view to check.
 * @param fallback Fallback function to call if \p str is empty.
 * @return <tt>cstring_view</tt> that contains either the original \p str, or the result of \p fallback.
 */
template <concepts::signature_of<cstring_view> F>
[[nodiscard]] auto nonempty_or(cstring_view str, F &&fallback) -> cstring_view {
    if (str.empty()) return FWD(fallback)();
    else return str;
}

[[nodiscard]] auto processFileDialog(std::span<const nfdfilteritem_t> filterItems) -> std::optional<std::filesystem::path> {
    NFD::UniquePath outPath;
    if (nfdresult_t nfdResult = OpenDialog(outPath, filterItems.data(), filterItems.size()); nfdResult == NFD_OKAY) {
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

auto hoverableImage(vk::DescriptorSet texture, const ImVec2 &size, const ImVec4 &tint = { 1.f, 1.f, 1.f, 1.f}) -> void {
    const ImVec2 texturePosition = ImGui::GetCursorScreenPos();
    ImGui::Image(vku::toUint64(texture), size, { 0.f, 0.f }, { 1.f, 1.f }, tint);

    if (ImGui::BeginItemTooltip()) {
        const ImGuiIO &io = ImGui::GetIO();

        const ImVec2 zoomedPortionSize = size / 4.f;
        ImVec2 region = io.MousePos - texturePosition - zoomedPortionSize * 0.5f;
        region.x = std::clamp(region.x, 0.f, size.x - zoomedPortionSize.x);
        region.y = std::clamp(region.y, 0.f, size.y - zoomedPortionSize.y);

        constexpr float zoomScale = 4.0f;
        ImGui::Image(vku::toUint64(texture), zoomedPortionSize * zoomScale, region / size, (region + zoomedPortionSize) / size, tint);
        ImGui::TextUnformatted(tempStringBuffer.write("Showing: [{:.0f}, {:.0f}]x[{:.0f}, {:.0f}]", region.x, region.y, region.x + zoomedPortionSize.y, region.y + zoomedPortionSize.y));

        ImGui::EndTooltip();
    }
}

void attributeTable(std::ranges::viewable_range auto const &attributes) {
    static int floatingPointPrecision = 2;
    ImGui::TableNoRowNumber(
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
                    if (min.size() == 1) ImGui::TextUnformatted(tempStringBuffer.write("[{1:.{0}f}, {2:.{0}f}]", floatingPointPrecision, min[0], max[0]));
                    else ImGui::TextUnformatted(tempStringBuffer.write("{1::.{0}f}x{2::.{0}f}", floatingPointPrecision, min, max));
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
                    // TODO.
                }
            }
            else {
                ImGui::TextDisabled("-");
            }
        }) });

    if (ImGui::InputInt("Bound fp precision", &floatingPointPrecision)) {
        floatingPointPrecision = std::clamp(floatingPointPrecision, 0, 9);
    }
}

auto assetInfo(fastgltf::AssetInfo &assetInfo) -> void {
    ImGui::InputTextWithHint("glTF Version", "<empty>", &assetInfo.gltfVersion);
    ImGui::InputTextWithHint("Generator", "<empty>", &assetInfo.generator);
    ImGui::InputTextWithHint("Copyright", "<empty>", &assetInfo.copyright);
}

auto assetBuffers(std::span<fastgltf::Buffer> buffers, const std::filesystem::path &assetDir) -> void {
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
        ImGui::ColumnInfo { "Length", [](const fastgltf::Buffer &buffer) {
            ImGui::TextUnformatted(tempStringBuffer.write(buffer.byteLength));
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "MIME", [](const fastgltf::Buffer &buffer) {
            visit(fastgltf::visitor {
                [](const auto &source) requires requires { source.mimeType -> fastgltf::MimeType; } {
                    ImGui::TextUnformatted(to_string(source.mimeType));
                },
                [](const auto&) {
                    ImGui::TextDisabled("-");
                },
            }, buffer.data);
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Location", [&](const fastgltf::Buffer &buffer) {
            visit(fastgltf::visitor {
                [](const fastgltf::sources::Array&) {
                    ImGui::TextUnformatted("Embedded (Array)"sv);
                },
                [](const fastgltf::sources::BufferView &bufferView) {
                    ImGui::TextUnformatted(tempStringBuffer.write("BufferView ({})", bufferView.bufferViewIndex));
                },
                [&](const fastgltf::sources::URI &uri) {
                    ImGui::TextLinkOpenURL("\u2197" /*↗*/, PATH_C_STR(assetDir / uri.uri.fspath()));
                },
                [](const auto&) {
                    ImGui::TextDisabled("-");
                }
            }, buffer.data);
        }, ImGuiTableColumnFlags_WidthFixed });
}

auto assetBufferViews(std::span<fastgltf::BufferView> bufferViews, std::span<fastgltf::Buffer> buffers) -> void {
    ImGui::Table(
        "gltf-buffer-views-table",
        ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
        bufferViews,
        ImGui::ColumnInfo { "Name", [&](std::size_t rowIndex, fastgltf::BufferView &bufferView) {
            ImGui::WithID(rowIndex, [&]() {
                ImGui::SetNextItemWidth(-std::numeric_limits<float>::min());
                ImGui::InputTextWithHint("##name", "<empty>", &bufferView.name);
            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "Buffer", [&](const fastgltf::BufferView &bufferView) {
            if (ImGui::TextLink("\u2197" /*↗*/)) {
                // TODO
            }
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(nonempty_or(
                    buffers[bufferView.bufferIndex].name,
                    [&]() { return tempStringBuffer.write("<Unnamed buffer {}>", bufferView.bufferIndex).view(); }));
                ImGui::EndTooltip();
            }
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Offset", [&](const fastgltf::BufferView &bufferView) {
            ImGui::TextUnformatted(tempStringBuffer.write(bufferView.byteOffset));
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Length", [&](const fastgltf::BufferView &bufferView) {
            ImGui::TextUnformatted(tempStringBuffer.write(bufferView.byteLength));
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

auto assetImages(std::span<fastgltf::Image> images, const std::filesystem::path &assetDir) -> void {
    ImGui::Table(
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
            visit(fastgltf::visitor {
                [](const auto &source) requires requires { source.mimeType -> fastgltf::MimeType; } {
                    ImGui::TextUnformatted(to_string(source.mimeType));
                },
                [](const auto&) {
                    ImGui::TextDisabled("-");
                },
            }, image.data);
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Location", [&](const fastgltf::Image &image) {
            visit(fastgltf::visitor {
                [](const fastgltf::sources::Array&) {
                    ImGui::TextUnformatted("Embedded (Array)"sv);
                },
                [](const fastgltf::sources::BufferView &bufferView) {
                    ImGui::TextUnformatted(tempStringBuffer.write("BufferView ({})", bufferView.bufferViewIndex));
                },
                [&](const fastgltf::sources::URI &uri) {
                    ImGui::TextLinkOpenURL("\u2197" /*↗*/, PATH_C_STR(assetDir / uri.uri.fspath()));
                },
                [](const auto&) {
                    ImGui::TextDisabled("-");
                }
            }, image.data);
        }, ImGuiTableColumnFlags_WidthFixed });
}

auto assetSamplers(std::span<fastgltf::Sampler> samplers) -> void {
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

vk_gltf_viewer::control::ImGuiTaskCollector::ImGuiTaskCollector(
    std::vector<Task> &tasks,
    const ImVec2 &framebufferSize,
    const vk::Rect2D &oldPassthruRect
) : tasks { tasks } {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Enable global docking.
    const ImGuiID dockSpaceId = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);

    // Get central node region.
    centerNodeRect = ImGui::DockBuilderGetCentralNode(dockSpaceId)->Rect();

    // Calculate framebuffer coordinate based passthru rect.
    const ImVec2 scaleFactor = framebufferSize / ImGui::GetIO().DisplaySize;
    const vk::Rect2D passthruRect {
        { static_cast<std::int32_t>(centerNodeRect.Min.x * scaleFactor.x), static_cast<std::int32_t>(centerNodeRect.Min.y * scaleFactor.y) },
        { static_cast<std::uint32_t>(centerNodeRect.GetWidth() * scaleFactor.x), static_cast<std::uint32_t>(centerNodeRect.GetHeight() * scaleFactor.y) },
    };
    if (passthruRect != oldPassthruRect) {
        tasks.emplace_back(std::in_place_type<task::ChangePassthruRect>, passthruRect);
    }
}

vk_gltf_viewer::control::ImGuiTaskCollector::~ImGuiTaskCollector() {
    if (!assetInspectorCalled) {
        for (auto name : { "Asset Info", "Buffers", "Buffer Views", "Images", "Samplers" }) {
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
    const std::list<std::filesystem::path> &recentSkyboxes
) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open glTF File", "Ctrl+O")) {
                constexpr std::array filterItems {
                    nfdfilteritem_t { "All Supported Files", "gltf,glb" },
                    nfdfilteritem_t { "glTF File", "gltf,glb" },
                };
                if (auto filename = processFileDialog(filterItems)) {
                    tasks.emplace_back(std::in_place_type<task::LoadGltf>, *std::move(filename));
                }
            }
            if (ImGui::BeginMenu("Recent glTF Files")) {
                if (recentGltfs.empty()) {
                    ImGui::MenuItem("<empty>", nullptr, false, false);
                }
                else {
                    for (const std::filesystem::path &path : recentGltfs) {
                        if (ImGui::MenuItem(PATH_C_STR(path))) {
                            tasks.emplace_back(std::in_place_type<task::LoadGltf>, path);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Close glTF File", "Ctrl+W")) {
                tasks.emplace_back(std::in_place_type<task::CloseGltf>);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Skybox")) {
            if (ImGui::MenuItem("Open Skybox")) {
                constexpr std::array filterItems {
                    nfdfilteritem_t { "All Supported Images", "hdr,exr" },
                    nfdfilteritem_t { "HDR Image", "hdr" },
                    nfdfilteritem_t { "EXR Image", "exr" },
                };
                if (auto filename = processFileDialog(filterItems)) {
                    tasks.emplace_back(std::in_place_type<task::LoadEqmap>, *std::move(filename));
                }
            }
            if (ImGui::BeginMenu("Recent Skyboxes")) {
                if (recentSkyboxes.empty()) {
                    ImGui::MenuItem("<empty>", nullptr, false, false);
                }
                else {
                    for (const std::filesystem::path &path : recentSkyboxes) {
                        if (ImGui::MenuItem(PATH_C_STR(path))) {
                            tasks.emplace_back(std::in_place_type<task::LoadEqmap>, path);
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
    std::optional<std::size_t> &selectedMaterialIndex,
    std::span<const vk::DescriptorSet> assetTextureImGuiDescriptorSets
) {
    if (ImGui::Begin("Material Editor")) {
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
                if (ImGui::Selectable(nonempty_or(material.name, [&]() { return tempStringBuffer.write("<Unnamed material {}>", i).view(); }).c_str(), isSelected)) {
                    selectedMaterialIndex.emplace(i);
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }

            ImGui::EndCombo();
        }

        if (selectedMaterialIndex) {
            fastgltf::Material &material = asset.materials[*selectedMaterialIndex];

            ImGui::InputTextWithHint("Name", "<empty>", &material.name);

            if (ImGui::Checkbox("Double sided", &material.doubleSided)) {
                tasks.emplace_back(std::in_place_type<task::InvalidateDrawCommandSeparation>);
            }

            if (ImGui::Checkbox("KHR_materials_unlit", &material.unlit)) {
                tasks.emplace_back(std::in_place_type<task::InvalidateDrawCommandSeparation>);
            }

            constexpr std::array alphaModes { "OPAQUE", "MASK", "BLEND" };
            if (int alphaMode = static_cast<int>(material.alphaMode); ImGui::Combo("Alpha mode", &alphaMode, alphaModes.data(), alphaModes.size())) {
                material.alphaMode = static_cast<fastgltf::AlphaMode>(alphaMode);
                tasks.emplace_back(std::in_place_type<task::InvalidateDrawCommandSeparation>);
            }

            ImGui::WithDisabled([&]() {
                if (material.alphaMode == fastgltf::AlphaMode::Mask && ImGui::DragFloat("Alpha cutoff", &material.alphaCutoff, 0.01f, 0.f, 1.f)) {
                    // TODO
                }
            });

            if (ImGui::CollapsingHeader("Physically Based Rendering")) {
                ImGui::SeparatorText("Base Color");
                const auto &baseColorTextureInfo = material.pbrData.baseColorTexture;
                if (baseColorTextureInfo) {
                    hoverableImage(assetTextureImGuiDescriptorSets[baseColorTextureInfo->textureIndex], { 128.f, 128.f });
                    ImGui::SameLine();
                }
                ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                    ImGui::WithGroup([&]() {
                        ImGui::WithDisabled([&]() {
                            if (ImGui::DragFloat4("Factor", material.pbrData.baseColorFactor.data(), 0.01f, 0.f, 1.f)) {
                                // TODO
                            }
                        });
                        if (baseColorTextureInfo) {
                            ImGui::LabelText("Texture Index", "%zu", baseColorTextureInfo->textureIndex);
                            ImGui::LabelText("Texture Coordinate", "%zu", baseColorTextureInfo->texCoordIndex);
                        }
                    }, baseColorTextureInfo.has_value());
                });

                ImGui::SeparatorText("Metallic/Roughness");
                const auto &metallicRoughnessTextureInfo = material.pbrData.metallicRoughnessTexture;
                if (metallicRoughnessTextureInfo) {
                    hoverableImage(assetTextureImGuiDescriptorSets[metallicRoughnessTextureInfo->textureIndex], { 128.f, 128.f }, { 0.f, 0.f, 1.f, 1.f });
                    ImGui::SameLine();
                    hoverableImage(assetTextureImGuiDescriptorSets[metallicRoughnessTextureInfo->textureIndex], { 128.f, 128.f }, { 0.f, 1.f, 0.f, 1.f });
                    ImGui::SameLine();
                }
                ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                    ImGui::WithGroup([&]() {
                        ImGui::WithDisabled([&]() {
                            if (ImGui::DragFloat("Metallic Factor", &material.pbrData.metallicFactor, 0.01f, 0.f, 1.f)) {
                                // TODO
                            }
                            if (ImGui::DragFloat("Roughness Factor", &material.pbrData.roughnessFactor, 0.01f, 0.f, 1.f)) {
                                // TODO
                            }
                        });
                        if (metallicRoughnessTextureInfo) {
                            ImGui::LabelText("Texture Index", "%zu", metallicRoughnessTextureInfo->textureIndex);
                            ImGui::LabelText("Texture Coordinate", "%zu", metallicRoughnessTextureInfo->texCoordIndex);
                        }
                    });
                });
            }

            if (auto &textureInfo = material.normalTexture; textureInfo && ImGui::CollapsingHeader("Normal Mapping")) {
                hoverableImage(assetTextureImGuiDescriptorSets[textureInfo->textureIndex], { 128.f, 128.f });
                ImGui::SameLine();
                ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                    ImGui::WithGroup([&]() {
                        ImGui::WithDisabled([&]() {
                            if (ImGui::DragFloat("Scale", &textureInfo->scale, 0.01f, 0.f, std::numeric_limits<float>::max())) {
                                // TODO
                            }
                        });
                        ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);
                        ImGui::LabelText("Texture Coordinate", "%zu", textureInfo->texCoordIndex);
                    });
                });
            }

            if (auto &textureInfo = material.occlusionTexture; textureInfo && ImGui::CollapsingHeader("Occlusion Mapping")) {
                hoverableImage(assetTextureImGuiDescriptorSets[textureInfo->textureIndex], { 128.f, 128.f }, { 1.f, 0.f, 0.f, 1.f });
                ImGui::SameLine();
                ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                    ImGui::WithGroup([&]() {
                        ImGui::WithDisabled([&]() {
                            if (ImGui::DragFloat("Strength", &textureInfo->strength, 0.01f, 0.f, std::numeric_limits<float>::max())) {
                                // TODO
                            }
                        });
                        ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);
                        ImGui::LabelText("Texture Coordinate", "%zu", textureInfo->texCoordIndex);
                    });
                });
            }

            if (ImGui::CollapsingHeader("Emissive")) {
                const auto &textureInfo = material.emissiveTexture;
                if (textureInfo) {
                    hoverableImage(assetTextureImGuiDescriptorSets[textureInfo->textureIndex], { 128.f, 128.f });
                    ImGui::SameLine();
                }
                ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPosX() + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                    ImGui::WithGroup([&]() {
                        ImGui::WithDisabled([&]() {
                            if (ImGui::DragFloat3("Factor", material.emissiveFactor.data(), 0.01f, 0.f, 1.f)) {
                                // TODO
                            }
                        });
                        if (textureInfo) {
                            ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);
                            ImGui::LabelText("Texture Coordinate", "%zu", textureInfo->texCoordIndex);
                        }
                    }, textureInfo.has_value());
                });
            }
        }
    }
    ImGui::End();

    materialEditorCalled = true;
}

void vk_gltf_viewer::control::ImGuiTaskCollector::sceneHierarchy(
    fastgltf::Asset &asset,
    std::size_t sceneIndex,
    const std::variant<std::vector<std::optional<bool>>, std::vector<bool>> &visibilities,
    const std::optional<std::uint16_t> &hoveringNodeIndex,
    const std::unordered_set<std::uint16_t> &selectedNodeIndices
) {
    if (ImGui::Begin("Scene Hierarchy")) {
        if (ImGui::BeginCombo("Scene", nonempty_or(asset.scenes[sceneIndex].name, [&]() { return tempStringBuffer.write("<Unnamed scene {}>", sceneIndex).view(); }).c_str())) {
            for (const auto &[i, scene] : asset.scenes | ranges::views::enumerate) {
                const bool isSelected = i == sceneIndex;
                if (ImGui::Selectable(nonempty_or(scene.name, [&]() { return tempStringBuffer.write("<Unnamed scene {}>", i).view(); }).c_str(), isSelected)) {
                    tasks.emplace_back(std::in_place_type<task::ChangeScene>, i);
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
        ImGui::HelperMarker("If all nested nodes have only one child, they will be shown as a single node (with combined name).");

        if (bool tristateVisibility = holds_alternative<std::vector<std::optional<bool>>>(visibilities);
            ImGui::Checkbox("Use tristate visibility", &tristateVisibility)) {
            tasks.emplace_back(std::in_place_type<task::ChangeNodeVisibilityType>);
        }
        ImGui::SameLine();
        ImGui::HelperMarker(
            "If all children of a node are visible, the node will be checked. "
            "If all children are hidden, the node will be unchecked. "
            "If some children are visible and some are hidden, the node will be in an indeterminate state.");

        // FIXME: due to the Clang 18's explicit object parameter bug, const fastgltf::Asset& is passed (but it is unnecessary). Remove the parameter when fixed.
        const auto addChildNode = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex) -> void {
            std::vector<std::size_t> ancestorNodeIndices;
            if (mergeSingleChildNodes) {
                while (asset.nodes[nodeIndex].children.size() == 1) {
                    ancestorNodeIndices.push_back(nodeIndex);
                    nodeIndex = asset.nodes[nodeIndex].children[0];
                }
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::WithID(nodeIndex, [&]() {
                const fastgltf::Node &node = asset.nodes[nodeIndex];
                const bool isNodeSelected = selectedNodeIndices.contains(nodeIndex);
                const bool isTreeNodeOpen = ImGui::WithStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), [&]() {
                    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowOverlap;
                    if (nodeIndex == hoveringNodeIndex) flags |= ImGuiTreeNodeFlags_Framed;
                    if (isNodeSelected) flags |= ImGuiTreeNodeFlags_Selected;
                    if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;

                    return ImGui::TreeNodeEx("##treenode", flags);
                }, nodeIndex == hoveringNodeIndex);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !isNodeSelected) {
                    tasks.emplace_back(std::in_place_type<task::SelectNodeFromSceneHierarchy>, nodeIndex, ImGui::GetIO().KeyCtrl);
                }
                if (ImGui::IsItemHovered() && nodeIndex != hoveringNodeIndex) {
                    tasks.emplace_back(std::in_place_type<task::HoverNodeFromSceneHierarchy>, nodeIndex);
                }

                // --------------------
                // Node visibility checkbox.
                // --------------------

                ImGui::SameLine();
                const bool visibilityChanged = visit(multilambda {
                    [&](std::span<const std::optional<bool>> visibilities) {
                        std::optional visibility = visibilities[nodeIndex];
                        return ImGui::CheckboxTristate("##visibility", visibility);
                    },
                    [&](const std::vector<bool> &visibilities) {
                        bool visibility = visibilities[nodeIndex];
                        return ImGui::Checkbox("##visibility", &visibility);
                    },
                }, visibilities);
                if (visibilityChanged)  {
                    tasks.emplace_back(std::in_place_type<task::ChangeNodeVisibility>, nodeIndex);
                }

                // --------------------
                // Node name (and its ancestors' if mergeSingleChildNodes is true).
                // --------------------

                for (bool first = true; std::size_t passedNodeIndex : ranges::views::concat(ancestorNodeIndices, std::views::single(nodeIndex))) {
                    if (first) first = false;
                    else {
                        ImGui::SameLine();
                        ImGui::TextUnformatted("/"sv);
                    }

                    ImGui::SameLine();
                    if (ImGui::TextLink(nonempty_or(asset.nodes[passedNodeIndex].name, [&]() {
                        return tempStringBuffer.write("<Unnamed node {}>", passedNodeIndex).view();
                    }).c_str())) {
                        tasks.emplace_back(std::in_place_type<task::SelectNodeFromSceneHierarchy>, passedNodeIndex, ImGui::GetIO().KeyCtrl);
                    }
                }

                // --------------------
                // Node transformation.
                // --------------------

                ImGui::TableSetColumnIndex(1);
                visit(fastgltf::visitor {
                    [](const fastgltf::TRS &trs) {
                        tempStringBuffer.clear();
                        if (trs.translation != fastgltf::math::fvec3{}) {
                            tempStringBuffer.append("T[{:.2f}, {:.2f}, {:.2f}]", trs.translation.x(), trs.translation.y(), trs.translation.z());
                        }
                        if (trs.rotation != fastgltf::math::fquat{}) {
                            if (!tempStringBuffer.empty()) tempStringBuffer.append(" * ");
                            tempStringBuffer.append("R[{:.2f}, {:.2f}, {:.2f}, {:.2f}]", trs.rotation.x(), trs.rotation.y(), trs.rotation.z(), trs.rotation.w());
                        }
                        if (trs.scale != fastgltf::math::fvec3 { 1.f, 1.f, 1.f }) {
                            if (!tempStringBuffer.empty()) tempStringBuffer.append(" * ");
                            tempStringBuffer.append("S[{:.2f}, {:.2f}, {:.2f}]", trs.scale.x(), trs.scale.y(), trs.scale.z());
                        }

                        if (!tempStringBuffer.empty()) {
                            ImGui::TextUnformatted(tempStringBuffer);
                        }
                    },
                    [](const fastgltf::math::fmat4x4 &transformMatrix) {
                        if (transformMatrix != fastgltf::math::fmat4x4 { 1.f }) {
                            const std::span components { transformMatrix.data(), 16 };
#if __cpp_lib_ranges_chunk >= 202202L
                            ImGui::TextUnformatted(tempStringBuffer.write("{:::.2f}", components | std::views::chunk(4)));
#else
                            INDEX_SEQ(Is, 4, {
                                ImGui::TextUnformatted(tempStringBuffer.write("[{::.2f}, {::.2f}, {::.2f}, {::.2f}]", components.subspan(4 * Is, 4)...));
                            });
#endif
                        }
                    },
                }, node.transform);

                // --------------------
                // Node mesh, light and camera.
                // --------------------

                if (const auto &meshIndex = node.meshIndex) {
                    ImGui::TableSetColumnIndex(2);
                    if (ImGui::TextLink(nonempty_or(asset.meshes[*meshIndex].name, [&]() {
                        return tempStringBuffer.write("<Unnamed mesh {}>", *meshIndex).view();
                    }).c_str())) {
                        // TODO
                    }
                }

                if (const auto &lightIndex = node.lightIndex) {
                    ImGui::TableSetColumnIndex(3);
                    if (ImGui::TextLink(nonempty_or(asset.lights[*lightIndex].name, [&]() {
                        return tempStringBuffer.write("<Unnamed light {}>", *lightIndex).view();
                    }).c_str())) {
                        // TODO
                    }
                }

                if (const auto &cameraIndex = node.cameraIndex) {
                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::TextLink(nonempty_or(asset.cameras[*cameraIndex].name, [&]() {
                        return tempStringBuffer.write("<Unnamed camera {}>", *cameraIndex).view();
                    }).c_str())) {
                        // TODO
                    }
                }

                if (isTreeNodeOpen) {
                    for (std::size_t childNodeIndex : node.children) {
                        self(asset, childNodeIndex);
                    }
                    ImGui::TreePop();
                }
            });
        };

        if (ImGui::BeginTable("scene-hierarchy-table", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);
            ImGui::TableSetupColumn("Transform", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Mesh", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Light", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Camera", ImGuiTableColumnFlags_WidthFixed);
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
    const std::unordered_set<std::uint16_t> &selectedNodeIndices
) {
    if (ImGui::Begin("Node Inspector")) {
        if (selectedNodeIndices.empty()) {
            ImGui::TextUnformatted("No nodes are selected."sv);
        }
        else if (selectedNodeIndices.size() == 1) {
            const std::uint16_t selectedNodeIndex = *selectedNodeIndices.begin();
            fastgltf::Node &node = asset.nodes[selectedNodeIndex];
            ImGui::InputTextWithHint("Name", "<empty>", &node.name);

            ImGui::SeparatorText("Transform");

            if (bool isTrs = holds_alternative<fastgltf::TRS>(node.transform); ImGui::BeginCombo("Local transform", isTrs ? "TRS" : "Transform Matrix")) {
                if (ImGui::Selectable("TRS", isTrs) && !isTrs) {
                    fastgltf::TRS trs;
                    decomposeTransformMatrix(get<fastgltf::math::fmat4x4>(node.transform), trs.scale, trs.rotation, trs.translation);
                    node.transform = trs;
                }
                if (ImGui::Selectable("Transform Matrix", !isTrs) && isTrs) {
                    const auto &trs = get<fastgltf::TRS>(node.transform);
                    constexpr fastgltf::math::fmat4x4 identity { 1.f };
                    node.transform.emplace<fastgltf::math::fmat4x4>(translate(identity, trs.translation) * rotate(identity, trs.rotation) * scale(identity, trs.scale));
                }
                ImGui::EndCombo();
            }
            std::visit(fastgltf::visitor {
                [&](fastgltf::TRS &trs) {
                    // | operator cannot be chained, because of the short circuit evaluation.
                    bool transformChanged = ImGui::DragFloat3("Translation", trs.translation.data());
                    transformChanged |= ImGui::DragFloat4("Rotation", trs.rotation.value_ptr());
                    transformChanged |= ImGui::DragFloat3("Scale", trs.scale.data());

                    if (transformChanged) {
                        tasks.emplace_back(std::in_place_type<task::ChangeNodeLocalTransform>, selectedNodeIndex);
                    }
                },
                [&](fastgltf::math::fmat4x4 &matrix) {
                    // | operator cannot be chained, because of the short circuit evaluation.
                    bool transformChanged = ImGui::DragFloat4("Column 0", matrix.col(0).data());
                    transformChanged |= ImGui::DragFloat4("Column 1", matrix.col(4).data());
                    transformChanged |= ImGui::DragFloat4("Column 2", matrix.col(8).data());
                    transformChanged |= ImGui::DragFloat4("Column 3", matrix.col(12).data());

                    if (transformChanged) {
                        tasks.emplace_back(std::in_place_type<task::ChangeNodeLocalTransform>, selectedNodeIndex);
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
                                            // TODO.
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
                    ImGui::EndTabItem();
                }
                if (node.cameraIndex && ImGui::BeginTabItem("Camera")) {
                    fastgltf::Camera &camera = asset.cameras[*node.cameraIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &camera.name);
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
    vk::DescriptorSet eqmapTextureImGuiDescriptorSet
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
            ImGui::TableNoRowNumber(
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
                tasks.emplace_back(std::in_place_type<task::ChangeCameraView>);
            }

            if (float fovInDegree = glm::degrees(camera.fov); ImGui::DragFloat("FOV", &fovInDegree, 0.1f, 15.f, 120.f, "%.2f deg")) {
                camera.fov = glm::radians(fovInDegree);
            }

            if (ImGui::Checkbox("Automatic Near/Far Adjustment", &automaticNearFarPlaneAdjustment) && automaticNearFarPlaneAdjustment) {
                tasks.emplace_back(std::in_place_type<task::TightenNearFarPlane>);
            }
            ImGui::SameLine();
            ImGui::HelperMarker("Near/Far plane will be automatically to fit the scene bounding box.");

            ImGui::WithDisabled([&]() {
                ImGui::DragFloatRange2("Near/Far", &camera.zMin, &camera.zMax, 1e-6f, 1e-4f, 1e6, "%.2e", nullptr, ImGuiSliderFlags_Logarithmic);
            }, automaticNearFarPlaneAdjustment);

            ImGui::Checkbox("Use Frustum Culling", &useFrustumCulling);
            ImGui::SameLine();
            ImGui::HelperMarker("The primitives outside the camera frustum will be culled.");
        }

        if (ImGui::CollapsingHeader("Node selection")) {
            bool showHoveringNodeOutline = hoveringNodeOutline.has_value();
            if (ImGui::Checkbox("Hovering node outline", &showHoveringNodeOutline)) {
                hoveringNodeOutline.set_active(showHoveringNodeOutline);
            }
            ImGui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##hoveringNodeOutline", &hoveringNodeOutline->thickness, 1.f, 1.f, std::numeric_limits<float>::max());
                ImGui::ColorEdit4("Color##hoveringNodeOutline", value_ptr(hoveringNodeOutline->color));
            }, !showHoveringNodeOutline);

            bool showSelectedNodeOutline = selectedNodeOutline.has_value();
            if (ImGui::Checkbox("Selected node outline", &showSelectedNodeOutline)) {
                selectedNodeOutline.set_active(showSelectedNodeOutline);
            }
            ImGui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##selectedNodeOutline", &selectedNodeOutline->thickness, 1.f, 1.f, std::numeric_limits<float>::max());
                ImGui::ColorEdit4("Color##selectedNodeOutline", value_ptr(selectedNodeOutline->color));
            }, !showSelectedNodeOutline);
        }
    }
    ImGui::End();
}


void vk_gltf_viewer::control::ImGuiTaskCollector::imguizmo(
    Camera &camera,
    const std::optional<std::tuple<fastgltf::Asset&, std::span<const glm::mat4>, std::uint16_t, ImGuizmo::OPERATION>> &assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation
) {
    // Set ImGuizmo rect.
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(centerNodeRect.Min.x, centerNodeRect.Min.y, centerNodeRect.GetWidth(), centerNodeRect.GetHeight());

    if (assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation) {
        auto &[asset, nodeWorldTransforms, selectedNodeIndex, operation] = *assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation;
        if (glm::mat4 worldTransform = nodeWorldTransforms[selectedNodeIndex];
            Manipulate(value_ptr(camera.getViewMatrix()), value_ptr(camera.getProjectionMatrixForwardZ()), operation, ImGuizmo::MODE::WORLD, value_ptr(worldTransform))) {

            const glm::mat4 deltaMatrix = worldTransform * inverse(nodeWorldTransforms[selectedNodeIndex]);
            visit(fastgltf::visitor {
                [&](fastgltf::math::fmat4x4 &transformMatrix) {
                    const glm::mat4 newTransform = deltaMatrix * fastgltf::toMatrix(transformMatrix);
                    std::copy_n(value_ptr(newTransform), 16, transformMatrix.data());
                },
                [&](fastgltf::TRS &trs) {
                    const glm::mat4 newTransform = deltaMatrix * toMatrix(trs);
                    fastgltf::math::fmat4x4 newTransformMatrix;
                    std::copy_n(value_ptr(newTransform), 16, newTransformMatrix.data());
                    fastgltf::math::decomposeTransformMatrix(newTransformMatrix, trs.scale, trs.rotation, trs.translation);
                },
            }, asset.nodes[selectedNodeIndex].transform);
            tasks.emplace_back(std::in_place_type<task::ChangeNodeLocalTransform>, selectedNodeIndex);
        }
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

        tasks.emplace_back(std::in_place_type<task::ChangeCameraView>);
    }
}