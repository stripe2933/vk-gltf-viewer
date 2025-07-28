module;

#include <imgui.h>
#include <ImGuizmo.h>

export module ImGuizmo;

export import imgui;

namespace ImGuizmo {
    export using ImGuizmo::BeginFrame;
    export using ImGuizmo::IsUsing;
    export using ImGuizmo::Enable;
    export using ImGuizmo::OPERATION;
    export using ImGuizmo::Manipulate;
    export using ImGuizmo::MODE;
    export using ImGuizmo::PushID;
    export using ImGuizmo::PopID;
    export using ImGuizmo::SetRect;
    export using ImGuizmo::ViewManipulate;
}