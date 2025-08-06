module;

#include <imgui_freetype.h>

export module imgui_freetype;

export import imgui;

export {
    // ----- Enums -----

    using ::ImGuiFreeTypeLoaderFlags_;
    using ::ImGuiFreeTypeLoaderFlags_NoHinting;
    using ::ImGuiFreeTypeLoaderFlags_NoAutoHint;
    using ::ImGuiFreeTypeLoaderFlags_ForceAutoHint;
    using ::ImGuiFreeTypeLoaderFlags_LightHinting;
    using ::ImGuiFreeTypeLoaderFlags_MonoHinting;
    using ::ImGuiFreeTypeLoaderFlags_Bold;
    using ::ImGuiFreeTypeLoaderFlags_Oblique;
    using ::ImGuiFreeTypeLoaderFlags_Monochrome;
    using ::ImGuiFreeTypeLoaderFlags_LoadColor;
    using ::ImGuiFreeTypeLoaderFlags_Bitmap;

    // ----- Type aliases -----

    using ::ImGuiFreeTypeLoaderFlags;

    // ----- Functions -----

namespace ImGui {
    using ImGui::SetAllocatorFunctions;
    using ImGui::DebugEditFontLoaderFlags;
    using ImGui::GetFontLoader;
}
}
