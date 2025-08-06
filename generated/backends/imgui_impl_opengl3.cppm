module;

#include <imgui_impl_opengl3.h>

export module imgui_impl_opengl3;

export import imgui;

export {
    // ----- Types -----

    using ::ImDrawData;
    using ::ImTextureData;

    // ----- Functions -----

    using ::ImGui_ImplOpenGL3_NewFrame;
    using ::ImGui_ImplOpenGL3_Init;
    using ::ImGui_ImplOpenGL3_CreateDeviceObjects;
    using ::ImGui_ImplOpenGL3_UpdateTexture;
    using ::ImGui_ImplOpenGL3_DestroyDeviceObjects;
    using ::ImGui_ImplOpenGL3_RenderDrawData;
    using ::ImGui_ImplOpenGL3_Shutdown;
}
