module;

#include <cassert>
#include <cinttypes>
#include <version>

#include <boost/container/static_vector.hpp>
#include <fastgltf/types.hpp>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <nfd.hpp>

module vk_gltf_viewer;
import :imgui.TaskCollector;

import std;
import glm;
import vku;
import :helpers.fastgltf;
#if __cpp_lib_format_ranges >= 202207L
import :helpers.formatters.joiner;
#endif
import :helpers.functional;
import :helpers.imgui;
import :helpers.optional;
import :helpers.ranges;

#define INDEX_SEQ(Is, N, ...) [&]<std::size_t... Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})
#ifdef _MSC_VER
#define PATH_C_STR(...) (__VA_ARGS__).string().c_str()
#else
#define PATH_C_STR(...) (__VA_ARGS__).c_str()
#endif

using namespace std::string_view_literals;

/**
 * Return \p str if it is not empty, otherwise return \p fallback.
 * @param str Null-terminated non-owning string view to check.
 * @param fallback Fallback to be used if \p str is empty.
 * @return \p str if it is not empty, otherwise \p fallback.
 */
[[nodiscard]] auto nonempty_or(cstring_view str, cstring_view fallback) -> cstring_view {
    return str.empty() ? fallback : str;
}

/**
 * Return \p str if it is not empty, otherwise return the result of \p fallback.
 * @param str Null-terminated non-owning string view to check.
 * @param fallback Fallback function to call if \p str is empty.
 * @return A variant that contains either the <tt>cstring_view</tt> of the original \p str, or string of the result of \p fallback.
 */
[[nodiscard]] auto nonempty_or(cstring_view str, std::invocable auto &&fallback) -> std::variant<cstring_view, std::string> {
    if (str.empty()) return fallback();
    else return str;
}

template <std::integral T>
[[nodiscard]] auto to_string(T value) -> cstring_view {
    static constexpr T MAX_NUM = 4096;
    static const std::vector numStrings
        = std::views::iota(T { 0 }, T { MAX_NUM + 1 })
        | std::views::transform([](T i) { return std::format("{}", i); })
        | std::ranges::to<std::vector>();

    assert(value <= MAX_NUM && "Value is too large");
    return numStrings[value];
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
    ImGui::Image(texture, size, { 0.f, 0.f }, { 1.f, 1.f }, tint);

    if (ImGui::BeginItemTooltip()) {
        const ImGuiIO &io = ImGui::GetIO();

        const ImVec2 zoomedPortionSize = size / 4.f;
        ImVec2 region = io.MousePos - texturePosition - zoomedPortionSize * 0.5f;
        region.x = std::clamp(region.x, 0.f, size.x - zoomedPortionSize.x);
        region.y = std::clamp(region.y, 0.f, size.y - zoomedPortionSize.y);

        constexpr float zoomScale = 4.0f;
        ImGui::Image(texture, zoomedPortionSize * zoomScale, region / size, (region + zoomedPortionSize) / size, tint);
        ImGui::Text("Showing: [%.0f, %.0f]x[%.0f, %.0f]", region.x, region.y, region.x + zoomedPortionSize.y, region.y + zoomedPortionSize.y);

        ImGui::EndTooltip();
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
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "<empty>", &buffer.name);

            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "Length", [](const fastgltf::Buffer &buffer) {
            ImGui::Text("%zu", buffer.byteLength);
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
                    ImGui::Text("BufferView (%zu)", bufferView.bufferViewIndex);
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
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "<empty>", &bufferView.name);
            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "Buffer", [&](const fastgltf::BufferView &bufferView) {
            if (ImGui::TextLink("\u2197" /*↗*/)) {
                // TODO
            }
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted(visit_as<cstring_view>(nonempty_or(
                    buffers[bufferView.bufferIndex].name,
                    [&]() { return std::format("<Unnamed buffer {}>", bufferView.bufferIndex); })));
                ImGui::EndTooltip();
            }
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Offset", [&](const fastgltf::BufferView &bufferView) {
            ImGui::Text("%zu", bufferView.byteOffset);
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Length", [&](const fastgltf::BufferView &bufferView) {
            ImGui::Text("%zu", bufferView.byteLength);
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Stride", [&](const fastgltf::BufferView &bufferView) {
            if (const auto &byteStride = bufferView.byteStride) {
                ImGui::Text("%zu", *byteStride);
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
                ImGui::SetNextItemWidth(-FLT_MIN);
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
                    ImGui::Text("BufferView (%zu)", bufferView.bufferViewIndex);
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

auto assetMaterials(
    std::span<fastgltf::Material> materials,
    std::optional<std::size_t> &selectedMaterialIndex,
    std::span<const vk::DescriptorSet> assetTextureImGuiDescriptorSets
) -> void {
    const bool isComboBoxOpened = [&]() {
        if (materials.empty()) {
            return ImGui::BeginCombo("Material", "<empty>");
        }
        else if (selectedMaterialIndex) {
            return ImGui::BeginCombo("Material", visit_as<cstring_view>(nonempty_or(
                materials[*selectedMaterialIndex].name,
                [&]() { return std::format("<Unnamed material {}>", *selectedMaterialIndex); })).c_str());
        }
        else {
            return ImGui::BeginCombo("Material", "<select...>");
        }
    }();
    if (isComboBoxOpened) {
        for (const auto &[i, material] : materials | ranges::views::enumerate) {
            const bool isSelected = i == selectedMaterialIndex;
            if (ImGui::Selectable(visit_as<cstring_view>(nonempty_or(material.name, [&]() { return std::format("<Unnamed material {}>", i); })).c_str(), isSelected)) {
                selectedMaterialIndex.emplace(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }

    if (selectedMaterialIndex) {
        fastgltf::Material &material = materials[*selectedMaterialIndex];

        ImGui::InputTextWithHint("Name", "<empty>", &material.name);

        if (ImGui::Checkbox("Double sided", &material.doubleSided)) {
            // TODO
        }

        constexpr std::array alphaModes { "OPAQUE", "MASK", "BLEND" };
        if (int alphaMode = static_cast<int>(material.alphaMode); ImGui::Combo("Alpha mode", &alphaMode, alphaModes.data(), alphaModes.size())) {
            material.alphaMode = static_cast<fastgltf::AlphaMode>(alphaMode);
            // TODO
        }

        if (ImGui::CollapsingHeader("Physically Based Rendering")) {
            ImGui::SeparatorText("Base Color");
            const auto &baseColorTextureInfo = material.pbrData.baseColorTexture;
            if (baseColorTextureInfo) {
                hoverableImage(assetTextureImGuiDescriptorSets[baseColorTextureInfo->textureIndex], { 128.f, 128.f });
                ImGui::SameLine();
            }
            ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPos().x + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                ImGui::WithGroup([&]() {
                    if (ImGui::DragFloat4("Factor", material.pbrData.baseColorFactor.data(), 0.01f, 0.f, 1.f)) {
                        // TODO
                    }
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
            ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPos().x + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                ImGui::WithGroup([&]() {
                    if (ImGui::DragFloat("Metallic Factor", &material.pbrData.metallicFactor, 0.01f, 0.f, 1.f)) {
                        // TODO
                    }
                    if (ImGui::DragFloat("Roughness Factor", &material.pbrData.roughnessFactor, 0.01f, 0.f, 1.f)) {
                        // TODO
                    }
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
            ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPos().x + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                ImGui::WithGroup([&]() {
                    if (ImGui::DragFloat("Scale", &textureInfo->scale, 0.01f, 0.f, FLT_MAX)) {
                        // TODO
                    }
                    ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);
                    ImGui::LabelText("Texture Coordinate", "%zu", textureInfo->texCoordIndex);
                });
            });
        }

        if (auto &textureInfo = material.occlusionTexture; textureInfo && ImGui::CollapsingHeader("Occlusion Mapping")) {
            hoverableImage(assetTextureImGuiDescriptorSets[textureInfo->textureIndex], { 128.f, 128.f }, { 1.f, 0.f, 0.f, 1.f });
            ImGui::SameLine();
            ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPos().x + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                ImGui::WithGroup([&]() {
                    if (ImGui::DragFloat("Strength", &textureInfo->strength, 0.01f, 0.f, FLT_MAX)) {
                        // TODO
                    }
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
            ImGui::WithItemWidth(ImGui::CalcItemWidth() - ImGui::GetCursorPos().x + 2.f * ImGui::GetStyle().ItemInnerSpacing.x, [&]() {
                ImGui::WithGroup([&]() {
                    if (ImGui::DragFloat3("Factor", material.emissiveFactor.data(), 0.01f, 0.f, 1.f)) {
                        // TODO
                    }
                    if (textureInfo) {
                        ImGui::LabelText("Texture Index", "%zu", textureInfo->textureIndex);
                        ImGui::LabelText("Texture Coordinate", "%zu", textureInfo->texCoordIndex);
                    }
                }, textureInfo.has_value());
            });
        }
    }
}

auto assetSamplers(std::span<fastgltf::Sampler> samplers) -> void {
    ImGui::Table(
        "gltf-samplers-table",
        ImGuiTableFlags_Borders | ImGuiTableFlags_Reorderable | ImGuiTableFlags_RowBg | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY,
        samplers,
        ImGui::ColumnInfo { "Name", [](std::size_t rowIndex, fastgltf::Sampler &sampler) {
            ImGui::WithID(rowIndex, [&]() {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::InputTextWithHint("##name", "<empty>", &sampler.name);
            });
        }, ImGuiTableColumnFlags_WidthStretch },
        ImGui::ColumnInfo { "Filter (Mag/Min)", [](const fastgltf::Sampler &sampler) {
            ImGui::Text("%s / %s",
                to_optional(sampler.magFilter).transform([](fastgltf::Filter filter) { return to_string(filter).c_str(); }).value_or("-"),
                to_optional(sampler.minFilter).transform([](fastgltf::Filter filter) { return to_string(filter).c_str(); }).value_or("-"));
        }, ImGuiTableColumnFlags_WidthFixed },
        ImGui::ColumnInfo { "Wrap (S/T)", [](const fastgltf::Sampler &sampler) {
            ImGui::Text("%s / %s", to_string(sampler.wrapS).c_str(), to_string(sampler.wrapT).c_str());
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
    ImGui::Render();
}

auto vk_gltf_viewer::control::ImGuiTaskCollector::menuBar(
    const std::list<std::filesystem::path> &recentGltfs,
    const std::list<std::filesystem::path> &recentSkyboxes
) && -> ImGuiTaskCollector {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open glTF File", "Ctrl+O")) {
                constexpr std::array filterItems {
                    nfdfilteritem_t { "glTF File", "gltf" },
                    nfdfilteritem_t { "glTf Binary File", "glb" },
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
                constexpr std::array filterItems { nfdfilteritem_t { "HDR image", "hdr" } };
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

    return *this;
}

auto vk_gltf_viewer::control::ImGuiTaskCollector::assetInspector(
const std::optional<std::tuple<fastgltf::Asset&, const std::filesystem::path&, std::optional<std::size_t>&, std::span<const vk::DescriptorSet>>> &assetAndAssetDirAndAssetInspectorMaterialIndexAssetTextureImGuiDescriptorSets
) && -> ImGuiTaskCollector {
    if (assetAndAssetDirAndAssetInspectorMaterialIndexAssetTextureImGuiDescriptorSets) {
        auto &[asset, assetDir, assetInspectorMaterialIndex, assetTexturesImGuiDescriptorSets] = *assetAndAssetDirAndAssetInspectorMaterialIndexAssetTextureImGuiDescriptorSets;

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

        if (ImGui::Begin("Materials")) {
            assetMaterials(asset.materials, assetInspectorMaterialIndex, assetTexturesImGuiDescriptorSets);
        }
        ImGui::End();

        if (ImGui::Begin("Samplers")) {
            assetSamplers(asset.samplers);
        }
        ImGui::End();
    }
    else {
        for (auto name : { "Asset Info", "Buffers", "Buffer Views", "Images", "Materials", "Samplers" }) {
            if (ImGui::Begin(name)) {
                ImGui::TextUnformatted("Asset not loaded."sv);
            }
            ImGui::End();
        }
    }

    return *this;
}

auto vk_gltf_viewer::control::ImGuiTaskCollector::sceneHierarchy(
    const std::optional<std::tuple<const fastgltf::Asset&, std::size_t, const std::variant<std::vector<std::optional<bool>>, std::vector<bool>>&, const std::optional<std::size_t>&, const std::unordered_set<std::size_t>&>> &assetAndSceneIndexAndNodeVisibilitiesAndHoveringNodeIndexAndSelectedNodeIndices
) && -> ImGuiTaskCollector {
    if (ImGui::Begin("Scene Hierarchy")) {
        if (assetAndSceneIndexAndNodeVisibilitiesAndHoveringNodeIndexAndSelectedNodeIndices) {
            const auto &[asset, sceneIndex, visibilities, hoveringNodeIndex, selectedNodeIndices] = *assetAndSceneIndexAndNodeVisibilitiesAndHoveringNodeIndexAndSelectedNodeIndices;
            if (ImGui::BeginCombo("Scene", visit_as<cstring_view>(nonempty_or(asset.scenes[sceneIndex].name, [&]() { return std::format("<Unnamed scene {}>", sceneIndex); })).c_str())) {
                for (const auto &[i, scene] : asset.scenes | ranges::views::enumerate) {
                    const bool isSelected = i == sceneIndex;
                    if (ImGui::Selectable(visit_as<cstring_view>(nonempty_or(scene.name, [&]() { return std::format("<Unnamed scene {}>", i); })).c_str(), isSelected)) {
                        tasks.emplace_back(std::in_place_type<task::ChangeScene>, i);
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            static bool mergeSingleChildNodes = true;
            ImGui::Checkbox("Merge single child nodes", &mergeSingleChildNodes);

            if (bool tristateVisibility = holds_alternative<std::vector<std::optional<bool>>>(visibilities); ImGui::Checkbox("Use tristate visibility", &tristateVisibility)) {
                tasks.emplace_back(std::in_place_type<task::ChangeNodeVisibilityType>);
            }

            // FIXME: due to the Clang 18's explicit object parameter bug, const fastgltf::Asset& is passed (but it is unnecessary). Remove the parameter when fixed.
            const auto addChildNode = [&](this const auto &self, const fastgltf::Asset &asset, std::size_t nodeIndex) -> void {
                std::size_t descendentNodeIndex = nodeIndex;

                std::vector<std::string> directDescendentNodeNames;
                while (true) {
                    const fastgltf::Node &descendentNode = asset.nodes[descendentNodeIndex];
                    if (const auto &descendentNodeName = descendentNode.name; descendentNodeName.empty()) {
                        directDescendentNodeNames.emplace_back(std::format("<Unnamed node {}>", descendentNodeIndex));
                    }
                    else {
                        directDescendentNodeNames.emplace_back(descendentNodeName);
                    }

                    if (!mergeSingleChildNodes || descendentNode.children.size() != 1) {
                        break;
                    }

                    descendentNodeIndex = descendentNode.children[0];
                }

                const fastgltf::Node &descendentNode = asset.nodes[descendentNodeIndex];

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                const ImGuiTreeNodeFlags flags
                    = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowOverlap
                    | (descendentNodeIndex == hoveringNodeIndex ? ImGuiTreeNodeFlags_Framed : 0)
                    | (selectedNodeIndices.contains(descendentNodeIndex) ? ImGuiTreeNodeFlags_Selected : 0)
                    | (descendentNode.children.empty() ? (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet) : 0);
                ImGui::WithID(descendentNodeIndex, [&]() {
                    const bool isTreeNodeOpen = ImGui::WithStyleColor(ImGuiCol_Header, ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive), [flags]() {
                        return ImGui::TreeNodeEx("", flags);
                    }, descendentNodeIndex == hoveringNodeIndex);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !(flags & ImGuiTreeNodeFlags_Selected)) {
                        tasks.emplace_back(std::in_place_type<task::SelectNodeFromSceneHierarchy>, descendentNodeIndex, ImGui::GetIO().KeyCtrl);
                    }
                    if (ImGui::IsItemHovered() && !(flags & ImGuiTreeNodeFlags_Framed)) {
                        tasks.emplace_back(std::in_place_type<task::HoverNodeFromSceneHierarchy>, descendentNodeIndex);
                    }

                    ImGui::SameLine();

#if __cpp_lib_format_ranges >= 202207L
                    const std::string concat = std::format("{::s}", make_joiner<" / ">(directDescendentNodeNames));
#else
                    std::string concat = directDescendentNodeNames[0];
                    for (std::string_view name : directDescendentNodeNames | std::views::drop(1)) {
                        using namespace std::string_literals;
                        concat += std::format(" / {}", name);
                    }
#endif

                    const bool visibilityChanged = visit(multilambda {
                        [&](std::span<const std::optional<bool>> visibilities) {
                            std::optional visibility = visibilities[nodeIndex];
                            return ImGui::CheckboxTristate(concat, visibility);
                        },
                        [&](const std::vector<bool> &visibilities) {
                            bool visibility = visibilities[nodeIndex];
                            return ImGui::Checkbox(concat.c_str(), &visibility);
                        },
                    }, visibilities);
                    if (visibilityChanged)  {
                        tasks.emplace_back(std::in_place_type<task::ChangeNodeVisibility>, nodeIndex);
                    }

                    ImGui::TableSetColumnIndex(1);
                    visit(fastgltf::visitor {
                        [](const fastgltf::TRS &trs) {
                            boost::container::static_vector<std::string, 3> transformComponents;
                            if (trs.translation != std::array { 0.f, 0.f, 0.f }) {
#if __cpp_lib_format_ranges >= 202207L
                                transformComponents.emplace_back(std::format("T{::.2f}", trs.translation));
#else
                                transformComponents.emplace_back(std::format("T[{:.2f}, {:.2f}, {:.2f}]", trs.translation[0], trs.translation[1], trs.translation[2]));
#endif
                            }
                            if (trs.rotation != std::array { 0.f, 0.f, 0.f, 1.f }) {
#if __cpp_lib_format_ranges >= 202207L
                                transformComponents.emplace_back(std::format("R{::.2f}", trs.rotation));
#else
                                transformComponents.emplace_back(std::format("R[{:.2f}, {:.2f}, {:.2f}, {:.2f}]", trs.rotation[0], trs.rotation[1], trs.rotation[2], trs.rotation[3]));
#endif
                            }
                            if (trs.scale != std::array { 1.f, 1.f, 1.f }) {
#if __cpp_lib_format_ranges >= 202207L
                                transformComponents.emplace_back(std::format("S{::.2f}", trs.scale));
#else
                                transformComponents.emplace_back(std::format("S[{:.2f}, {:.2f}, {:.2f}]", trs.scale[0], trs.scale[1], trs.scale[2]));
#endif
                            }

                            if (!transformComponents.empty()) {
#if __cpp_lib_format_ranges >= 202207L
                                ImGui::TextUnformatted(std::format("{::s}", make_joiner<" * ">(transformComponents)));
#else
                                switch (transformComponents.size()) {
                                case 1:
                                    ImGui::Text("%s", transformComponents[0].c_str());
                                    break;
                                case 2:
                                    ImGui::Text("%s * %s", transformComponents[0].c_str(), transformComponents[1].c_str());
                                    break;
                                case 3:
                                    ImGui::Text("%s * %s * %s", transformComponents[0].c_str(), transformComponents[1].c_str(), transformComponents[2].c_str());
                                    break;
                                }
#endif
                            }
                        },
                        [](const fastgltf::Node::TransformMatrix &transformMatrix) {
                            constexpr fastgltf::Node::TransformMatrix identity { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f };
                            if (transformMatrix != identity) {
#if __cpp_lib_ranges_chunk >= 202202L && __cpp_lib_format_ranges >= 202207L
                                ImGui::TextUnformatted(std::format("{:::.2f}", transformMatrix | std::views::chunk(4)));
#elif __cpp_lib_format_ranges >= 202207L
                                const std::span components { transformMatrix };
                                INDEX_SEQ(Is, 4, {
                                    ImGui::TextUnformatted(std::format("[{::.2f}, {::.2f}, {::.2f}, {::.2f}]", components.subspan(4 * Is, 4)...));
                                });
#else
                                std::apply([](auto ...components) {
                                    ImGui::Text("[[%.2f, %.2f, %.2f, %.2f], [%.2f, %.2f, %.2f, %.2f], [%.2f, %.2f, %.2f, %.2f], [%.2f, %.2f, %.2f, %.2f]]", components...);
                                }, transformMatrix);
#endif
                            }
                        },
                    }, descendentNode.transform);

                    if (const auto &meshIndex = descendentNode.meshIndex) {
                        ImGui::TableSetColumnIndex(2);
                        if (ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.meshes[*meshIndex].name, [&]() { return std::format("<Unnamed mesh {}>", *meshIndex); })).c_str())) {
                            // TODO
                        }
                    }

                    if (const auto &lightIndex = descendentNode.lightIndex) {
                        ImGui::TableSetColumnIndex(3);
                        if (ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.lights[*lightIndex].name, [&]() { return std::format("<Unnamed light {}>", *lightIndex); })).c_str())) {
                            // TODO
                        }
                    }

                    if (const auto &cameraIndex = descendentNode.cameraIndex) {
                        ImGui::TableSetColumnIndex(4);
                        if (ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.cameras[*cameraIndex].name, [&]() { return std::format("<Unnamed camera {}>", *cameraIndex); })).c_str())) {
                            // TODO
                        }
                    }

                    if (isTreeNodeOpen) {
                        for (std::size_t childNodeIndex : descendentNode.children) {
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
        else {
            ImGui::TextUnformatted("Asset not loaded."sv);
        }
    }
    ImGui::End();

    return *this;
}

auto vk_gltf_viewer::control::ImGuiTaskCollector::nodeInspector(
    std::optional<std::pair<fastgltf::Asset &, const std::unordered_set<std::size_t>&>> assetAndSelectedNodeIndices
) && -> ImGuiTaskCollector {
    if (ImGui::Begin("Node inspector")) {
        if (!assetAndSelectedNodeIndices) {
            ImGui::TextUnformatted("Asset not loaded."sv);
        }
        else if (const auto &[asset, selectedNodeIndices] = *assetAndSelectedNodeIndices; selectedNodeIndices.empty()) {
            ImGui::TextUnformatted("No nodes are selected."sv);
        }
        else if (selectedNodeIndices.size() == 1) {
            const std::size_t selectedNodeIndex = *selectedNodeIndices.begin();
            fastgltf::Node &node = asset.nodes[selectedNodeIndex];
            ImGui::InputTextWithHint("Name", "<empty>", &node.name);

            ImGui::SeparatorText("Transform");

            if (bool isTrs = holds_alternative<fastgltf::TRS>(node.transform); ImGui::BeginCombo("Local transform", isTrs ? "TRS" : "Transform Matrix")) {
                if (ImGui::Selectable("TRS", isTrs) && !isTrs) {
                    fastgltf::TRS trs;
                    fastgltf::decomposeTransformMatrix(get<fastgltf::Node::TransformMatrix>(node.transform), trs.scale, trs.rotation, trs.translation);
                    node.transform = trs;
                }
                if (ImGui::Selectable("Transform Matrix", !isTrs) && isTrs) {
                    const auto &trs = get<fastgltf::TRS>(node.transform);
                    const glm::mat4 matrix = glm::translate(glm::mat4 { 1.f }, glm::make_vec3(trs.translation.data())) * glm::mat4_cast(glm::make_quat(trs.rotation.data())) * glm::scale(glm::mat4 { 1.f }, glm::make_vec3(trs.scale.data()));

                    auto &transform = node.transform.emplace<fastgltf::Node::TransformMatrix>();
                    std::copy_n(value_ptr(matrix), 16, transform.data());
                }
                ImGui::EndCombo();
            }
            std::visit(fastgltf::visitor {
                [&](fastgltf::TRS &trs) {
                    // | operator cannot be chained, because of the short circuit evaluation.
                    bool transformChanged = ImGui::DragFloat3("Translation", trs.translation.data());
                    transformChanged |= ImGui::DragFloat4("Rotation", trs.rotation.data());
                    transformChanged |= ImGui::DragFloat3("Scale", trs.scale.data());

                    if (transformChanged) {
                        tasks.emplace_back(std::in_place_type<task::ChangeNodeLocalTransform>, selectedNodeIndex);
                    }
                },
                [&](fastgltf::Node::TransformMatrix &matrix) {
                    // | operator cannot be chained, because of the short circuit evaluation.
                    bool transformChanged = ImGui::DragFloat4("Column 0", &matrix[0]);
                    transformChanged |= ImGui::DragFloat4("Column 1", &matrix[4]);
                    transformChanged |= ImGui::DragFloat4("Column 2", &matrix[8]);
                    transformChanged |= ImGui::DragFloat4("Column 3", &matrix[12]);

                    if (transformChanged) {
                        tasks.emplace_back(std::in_place_type<task::ChangeNodeLocalTransform>, selectedNodeIndex);
                    }
                },
            }, node.transform);

            if (ImGui::BeginTabBar("node-tab-bar")) {
                if (node.meshIndex && ImGui::BeginTabItem("Mesh")) {
                    fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                    ImGui::InputTextWithHint("Name", "<empty>", &mesh.name);

                    for (auto &&[primitiveIndex, primitive]: mesh.primitives | ranges::views::enumerate) {
                        if (ImGui::CollapsingHeader(std::format("Primitive {}", primitiveIndex).c_str())) {
                            ImGui::LabelText("Type", "%s", to_string(primitive.type).c_str());
                            if (primitive.materialIndex) {
                                ImGui::WithID(*primitive.materialIndex, [&]() {
                                    if (ImGui::WithLabel("Material"sv, [&]() { return ImGui::TextLink(visit_as<cstring_view>(nonempty_or(asset.materials[*primitive.materialIndex].name, [&]() { return std::format("<Unnamed material {}>", *primitive.materialIndex); })).c_str()); })) {
                                        // TODO.
                                    }
                                });
                            }
                            else {
                                ImGui::WithDisabled([]() {
                                    ImGui::LabelText("Material", "-");
                                });
                            }

                            static int floatingPointPrecision = 2;
                            ImGui::TableNoRowNumber(
                                "attributes-table",
                                ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable | ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable | ImGuiTableFlags_SizingFixedFit,
                                ranges::views::concat(
                                    to_range(to_optional(primitive.indicesAccessor).transform([&](std::size_t accessorIndex) {
                                        return std::pair<std::string_view, const fastgltf::Accessor&> { "Index"sv, asset.accessors[accessorIndex] };
                                    })),
                                    primitive.attributes | ranges::views::decompose_transform([&](std::string_view attributeName, std::size_t accessorIndex) {
                                        return std::pair<std::string_view, const fastgltf::Accessor&> { attributeName, asset.accessors[accessorIndex] };
                                    })),
                                ImGui::ColumnInfo { "Attribute", decomposer([](std::string_view attributeName, const auto&) {
                                    ImGui::TextUnformatted(attributeName);
                                }) },
                                ImGui::ColumnInfo { "Type", decomposer([](auto, const fastgltf::Accessor &accessor) {
                                    ImGui::Text("%s (%s)", to_string(accessor.type).c_str(), to_string(accessor.componentType).c_str());
                                }) },
                                ImGui::ColumnInfo { "Count", decomposer([](auto, const fastgltf::Accessor &accessor) {
                                    ImGui::Text("%zu", accessor.count);
                                }) },
                                ImGui::ColumnInfo { "Bound", decomposer([](auto, const fastgltf::Accessor &accessor) {
                                    std::visit(fastgltf::visitor {
                                        [](const std::pmr::vector<std::int64_t> &min, const std::pmr::vector<std::int64_t> &max) {
                                            assert(min.size() == max.size() && "Different min/max dimension");
                                            if (min.size() == 1) ImGui::Text("[%" PRId64 ", %" PRId64 "]", min[0], max[0]);
#if __cpp_lib_format_ranges >= 202207L
                                            else ImGui::TextUnformatted(std::format("{}x{}", min, max));
#else
                                            else if (min.size() == 2) ImGui::Text("[%" PRId64 ", %" PRId64 "]x[%" PRId64 ", %" PRId64 "]", min[0], min[1], max[0], max[1]);
                                            else if (min.size() == 3) ImGui::Text("[%" PRId64 ", %" PRId64 ", %" PRId64 "]x[%" PRId64 ", %" PRId64 ", %" PRId64 "]", min[0], min[1], min[2], max[0], max[1], max[2]);
                                            else if (min.size() == 4) ImGui::Text("[%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "]x[%" PRId64 ", %" PRId64 ", %" PRId64 ", %" PRId64 "]", min[0], min[1], min[2], min[3], max[0], max[1], max[2], max[3]);
                                            else assert(false && "Unsupported min/max dimension");
#endif
                                        },
                                        [](const std::pmr::vector<double> &min, const std::pmr::vector<double> &max) {
                                            assert(min.size() == max.size() && "Different min/max dimension");
                                            if (min.size() == 1) ImGui::Text("[%.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, max[0]);
#if __cpp_lib_format_ranges >= 202207L
                                            else ImGui::TextUnformatted(std::format("{0::.{2}f}x{1::.{2}f}", min, max, floatingPointPrecision));
#else
                                            else if (min.size() == 2) ImGui::Text("[%.*lf, %.*lf]x[%.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, max[0], floatingPointPrecision, max[1]);
                                            else if (min.size() == 3) ImGui::Text("[%.*lf, %.*lf, %.*lf]x[%.*lf, %.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, min[2], floatingPointPrecision, max[0], floatingPointPrecision, max[1], floatingPointPrecision, max[2]);
                                            else if (min.size() == 4) ImGui::Text("[%.*lf, %.*lf, %.*lf, %.*lf]x[%.*lf, %.*lf, %.*lf, %.*lf]", floatingPointPrecision, min[0], floatingPointPrecision, min[1], floatingPointPrecision, min[2], min[1], floatingPointPrecision, min[3], floatingPointPrecision, max[0], floatingPointPrecision, max[1], floatingPointPrecision, max[2], floatingPointPrecision, max[3]);
                                            else assert(false && "Unsupported min/max dimension");
#endif
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
                                        if (ImGui::TextLink(to_string(*accessor.bufferViewIndex).c_str())) {
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

    return *this;
}

auto vk_gltf_viewer::control::ImGuiTaskCollector::background(
    bool canSelectSkyboxBackground,
    full_optional<glm::vec3> &solidBackground
) && -> ImGuiTaskCollector {
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

    return *this;
}

auto vk_gltf_viewer::control::ImGuiTaskCollector::imageBasedLighting(
    const std::optional<std::pair<const AppState::ImageBasedLighting&, vk::DescriptorSet>> &imageBasedLightingInfoAndEqmapTextureImGuiDescriptorSet
) && -> ImGuiTaskCollector {
    if (ImGui::Begin("IBL")) {
        if (imageBasedLightingInfoAndEqmapTextureImGuiDescriptorSet) {
            const auto &[info, eqmapTextureImGuiDescriptorSet] = *imageBasedLightingInfoAndEqmapTextureImGuiDescriptorSet;

            if (ImGui::CollapsingHeader("Equirectangular map")) {
                const float eqmapAspectRatio = static_cast<float>(info.eqmap.dimension.y) / info.eqmap.dimension.x;
                const ImVec2 eqmapTextureSize = ImVec2 { 1.f, eqmapAspectRatio } * ImGui::GetContentRegionAvail().x;
                hoverableImage(eqmapTextureImGuiDescriptorSet, eqmapTextureSize);

                ImGui::WithLabel("File"sv, [&]() {
                    ImGui::TextLinkOpenURL(PATH_C_STR(info.eqmap.path.stem()), PATH_C_STR(info.eqmap.path));
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
                    ImGui::ColumnInfo { "x", decomposer([](auto, const glm::vec3 &coeff) { ImGui::Text("%.3f", coeff.x); }) },
                    ImGui::ColumnInfo { "y", decomposer([](auto, const glm::vec3 &coeff) { ImGui::Text("%.3f", coeff.y); }) },
                    ImGui::ColumnInfo { "z", decomposer([](auto, const glm::vec3 &coeff) { ImGui::Text("%.3f", coeff.z); }) });
            }

            if (ImGui::CollapsingHeader("Prefiltered map")) {
                ImGui::LabelText("Size", "%u", info.prefilteredmap.size);
                ImGui::LabelText("Roughness levels", "%u", info.prefilteredmap.roughnessLevels);
                ImGui::LabelText("Samples", "%u", info.prefilteredmap.sampleCount);
            }
        }
        else {
            ImGui::TextUnformatted("Input equirectangular map not loaded."sv);
        }
    }
    ImGui::End();

    return *this;
}

auto vk_gltf_viewer::control::ImGuiTaskCollector::inputControl(
    Camera &camera,
    bool &automaticNearFarPlaneAdjustment,
    full_optional<AppState::Outline> &hoveringNodeOutline,
    full_optional<AppState::Outline> &selectedNodeOutline
) && -> ImGuiTaskCollector {
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
        }

        if (ImGui::CollapsingHeader("Node selection")) {
            bool showHoveringNodeOutline = hoveringNodeOutline.has_value();
            if (ImGui::Checkbox("Hovering node outline", &showHoveringNodeOutline)) {
                hoveringNodeOutline.set_active(showHoveringNodeOutline);
            }
            ImGui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##hoveringNodeOutline", &hoveringNodeOutline->thickness, 1.f, 1.f, FLT_MAX);
                ImGui::ColorEdit4("Color##hoveringNodeOutline", value_ptr(hoveringNodeOutline->color));
            }, !showHoveringNodeOutline);

            bool showSelectedNodeOutline = selectedNodeOutline.has_value();
            if (ImGui::Checkbox("Selected node outline", &showSelectedNodeOutline)) {
                selectedNodeOutline.set_active(showSelectedNodeOutline);
            }
            ImGui::WithDisabled([&]() {
                ImGui::DragFloat("Thickness##selectedNodeOutline", &selectedNodeOutline->thickness, 1.f, 1.f, FLT_MAX);
                ImGui::ColorEdit4("Color##selectedNodeOutline", value_ptr(selectedNodeOutline->color));
            }, !showSelectedNodeOutline);
        }
    }
    ImGui::End();

    return *this;
}


auto vk_gltf_viewer::control::ImGuiTaskCollector::imguizmo(
    Camera &camera,
    const std::optional<std::tuple<fastgltf::Asset&, std::span<const glm::mat4>, std::size_t, ImGuizmo::OPERATION>> &assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation
) && -> ImGuiTaskCollector {
    // Set ImGuizmo rect.
    ImGuizmo::BeginFrame();
    ImGuizmo::SetRect(centerNodeRect.Min.x, centerNodeRect.Min.y, centerNodeRect.GetWidth(), centerNodeRect.GetHeight());

    if (assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation) {
        auto &[asset, nodeWorldTransforms, selectedNodeIndex, operation] = *assetAndNodeWorldTransformsAndSelectedNodeIndexAndImGuizmoOperation;
        if (glm::mat4 worldTransform = nodeWorldTransforms[selectedNodeIndex];
            Manipulate(value_ptr(camera.getViewMatrix()), value_ptr(camera.getProjectionMatrixForwardZ()), operation, ImGuizmo::MODE::WORLD, value_ptr(worldTransform))) {

            const glm::mat4 deltaMatrix = worldTransform * inverse(nodeWorldTransforms[selectedNodeIndex]);
            visit(fastgltf::visitor {
                [&](fastgltf::Node::TransformMatrix &transformMatrix) {
                    const glm::mat4 newTransform = deltaMatrix * fastgltf::toMatrix(transformMatrix);
                    std::copy_n(value_ptr(newTransform), 16, transformMatrix.data());
                },
                [&](fastgltf::TRS &trs) {
                    const glm::mat4 newTransform = deltaMatrix * toMatrix(trs);
                    fastgltf::Node::TransformMatrix newTransformMatrix;
                    std::copy_n(value_ptr(newTransform), 16, newTransformMatrix.data());
                    fastgltf::decomposeTransformMatrix(newTransformMatrix, trs.scale, trs.rotation, trs.translation);
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

    return *this;
}