module;

#include <imgui_impl_glfw.h>

export module imgui.glfw;

export import imgui;

export {
    // ----- Types -----

    using ::GLFWmonitor;
    using ::GLFWwindow;

    // ----- Functions -----

    using ::ImGui_ImplGlfw_CharCallback;
    using ::ImGui_ImplGlfw_CursorEnterCallback;
    using ::ImGui_ImplGlfw_CursorPosCallback;
    using ::ImGui_ImplGlfw_GetContentScaleForMonitor;
    using ::ImGui_ImplGlfw_GetContentScaleForWindow;
    using ::ImGui_ImplGlfw_InitForOpenGL;
    using ::ImGui_ImplGlfw_InitForOther;
    using ::ImGui_ImplGlfw_InitForVulkan;
    using ::ImGui_ImplGlfw_InstallCallbacks;
    using ::ImGui_ImplGlfw_KeyCallback;
    using ::ImGui_ImplGlfw_MonitorCallback;
    using ::ImGui_ImplGlfw_MouseButtonCallback;
    using ::ImGui_ImplGlfw_NewFrame;
    using ::ImGui_ImplGlfw_RestoreCallbacks;
    using ::ImGui_ImplGlfw_ScrollCallback;
    using ::ImGui_ImplGlfw_SetCallbacksChainForAllWindows;
    using ::ImGui_ImplGlfw_Shutdown;
    using ::ImGui_ImplGlfw_Sleep;
    using ::ImGui_ImplGlfw_WindowFocusCallback;
}
