module;

#include <imgui_internal.h>

export module imgui.internal;

export import imgui;

export using ::ImGuiInputTextFlags_CallbackResize;
export using ::ImGuiItemFlags_MixedValue;
export using ::ImRect;

namespace ImGui {
#ifdef IMGUI_HAS_DOCK
    export using ImGui::DockBuilderGetCentralNode;
#endif
}