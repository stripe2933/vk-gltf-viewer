module;

#include <imgui_internal.h>

export module imgui.internal;

export import imgui;

export using ::ImGuiInputTextFlags_CallbackResize;
export using ::ImGuiItemFlags_MixedValue;
export using ::ImGuiTreeNodeFlags_DrawTreeLines;
export using ::ImGuiWindow;
export using ::ImRect;
#ifdef IMGUI_HAS_DOCK
export using ::ImGuiDir_Left;
export using ::ImGuiDir_Right;
export using ::ImGuiDir_Up;
export using ::ImGuiDir_Down;
#endif

namespace ImGui {
    export using ImGui::FindWindowByName;
    export using ImGui::FocusWindow;
    export using ImGui::PushMultiItemsWidths;

#ifdef IMGUI_HAS_DOCK
    export using ImGui::DockBuilderDockWindow;
    export using ImGui::DockBuilderFinish;
    export using ImGui::DockBuilderGetCentralNode;
    export using ImGui::DockBuilderSplitNode;
#endif
}