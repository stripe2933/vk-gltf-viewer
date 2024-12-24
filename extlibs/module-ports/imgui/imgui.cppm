module;

#include <imgui.h>

export module imgui;

export using ::ImDrawData;
export using ::ImDrawList;
export using ::ImFontGlyphRangesBuilder;
export using ::ImGuiCol_Header;
export using ::ImGuiCol_HeaderActive;
export using ::ImGuiContext;
export using ::ImGuiInputTextCallback;
export using ::ImGuiInputTextCallbackData;
export using ::ImGuiInputTextFlags;
export using ::ImGuiID;
export using ::ImGuiIO;
export using ::ImGuiStyle;
export using ::ImGuiTableColumnFlags;
export using ::ImGuiTableColumnFlags_DefaultHide;
export using ::ImGuiTableColumnFlags_WidthFixed;
export using ::ImGuiTableColumnFlags_WidthStretch;
export using ::ImGuiSliderFlags;
export using ::ImGuiSliderFlags_Logarithmic;
export using ::ImGuiTableFlags;
export using ::ImGuiTableFlags_Borders;
export using ::ImGuiTableFlags_Reorderable;
export using ::ImGuiTableFlags_RowBg;
export using ::ImGuiTableFlags_Hideable;
export using ::ImGuiTableFlags_ScrollY;
export using ::ImGuiTreeNodeFlags;
export using ::ImGuiTreeNodeFlags_AllowOverlap;
export using ::ImGuiTreeNodeFlags_Bullet;
export using ::ImGuiTreeNodeFlags_DefaultOpen;
export using ::ImGuiTreeNodeFlags_Framed;
export using ::ImGuiTreeNodeFlags_Leaf;
export using ::ImGuiTreeNodeFlags_NoTreePushOnOpen;
export using ::ImGuiTreeNodeFlags_OpenOnArrow;
export using ::ImGuiTreeNodeFlags_SpanAllColumns;
export using ::ImGuiTreeNodeFlags_Selected;
export using ::ImTextureID;
export using ::ImU32;
export using ::ImVec2;
export using ::ImVec4;
export using ::ImVector;
export using ::ImWchar;

#ifdef IMGUI_HAS_DOCK
export using ::ImGuiConfigFlags_DockingEnable;
export using ::ImGuiDockNodeFlags_NoDockingInCentralNode;
#endif

namespace ImGui {
    export void CheckVersion() {
        IMGUI_CHECKVERSION();
    }

    export using ImGui::AlignTextToFramePadding;
    export using ImGui::Begin;
    export using ImGui::BeginCombo;
    export using ImGui::BeginDisabled;
    export using ImGui::BeginItemTooltip;
    export using ImGui::BeginGroup;
    export using ImGui::BeginMainMenuBar;
    export using ImGui::BeginMenu;
    export using ImGui::BeginTabBar;
    export using ImGui::BeginTabItem;
    export using ImGui::BeginTable;
    export using ImGui::CalcItemWidth;
    export using ImGui::Checkbox;
    export using ImGui::CollapsingHeader;
    export using ImGui::ColorEdit4;
    export using ImGui::ColorPicker3;
    export using ImGui::Combo;
    export using ImGui::CreateContext;
    export using ImGui::DestroyContext;
    export using ImGui::DragFloat;
    export using ImGui::DragFloat2;
    export using ImGui::DragFloat3;
    export using ImGui::DragFloat4;
    export using ImGui::DragFloatRange2;
    export using ImGui::End;
    export using ImGui::EndCombo;
    export using ImGui::EndDisabled;
    export using ImGui::EndGroup;
    export using ImGui::EndMainMenuBar;
    export using ImGui::EndMenu;
    export using ImGui::EndTabBar;
    export using ImGui::EndTabItem;
    export using ImGui::EndTable;
    export using ImGui::EndTooltip;
    export using ImGui::GetContentRegionAvail;
    export using ImGui::GetCursorPosX;
    export using ImGui::GetCursorScreenPos;
    export using ImGui::GetDrawData;
    export using ImGui::GetIO;
    export using ImGui::GetStyle;
    export using ImGui::GetStyleColorVec4;
    export using ImGui::Image;
    export using ImGui::InputInt;
    export using ImGui::InputTextWithHint;
    export using ImGui::IsItemClicked;
    export using ImGui::IsItemHovered;
    export using ImGui::IsItemToggledOpen;
    export using ImGui::LabelText;
    export using ImGui::MenuItem;
    export using ImGui::NewFrame;
    export using ImGui::PopID;
    export using ImGui::PopItemFlag;
    export using ImGui::PopItemWidth;
    export using ImGui::PopStyleColor;
    export using ImGui::PushID;
    export using ImGui::PushItemFlag;
    export using ImGui::PushItemWidth;
    export using ImGui::PushStyleColor;
    export using ImGui::RadioButton;
    export using ImGui::Render;
    export using ImGui::SameLine;
    export using ImGui::Selectable;
    export using ImGui::SeparatorText;
    export using ImGui::SetCursorPosX;
    export using ImGui::SetItemDefaultFocus;
    export using ImGui::SetNextItemWidth;
    export using ImGui::TableHeadersRow;
    export using ImGui::TableNextRow;
    export using ImGui::TableSetColumnIndex;
    export using ImGui::TableSetupColumn;
    export using ImGui::TableSetupScrollFreeze;
    export using ImGui::Text;
    export using ImGui::TextDisabled;
    export using ImGui::TextLink;
    export using ImGui::TextLinkOpenURL;
    export using ImGui::TextUnformatted;
    export using ImGui::TreeNodeEx;
    export using ImGui::TreePop;

#ifdef IMGUI_HAS_DOCK
    export using ImGui::DockSpaceOverViewport;
#endif
}