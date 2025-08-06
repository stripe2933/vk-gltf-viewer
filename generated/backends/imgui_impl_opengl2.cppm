module;

#include <imgui_impl_opengl2.h>

export module imgui_impl_opengl2;

export import imgui;

export {
    // ----- Types -----

    using ::ImDrawData;
    using ::ImTextureData;

    // ----- Functions -----

    using ::ImGui_ImplOpenGL2_RenderDrawData;
    using ::ImGui_ImplOpenGL2_UpdateTexture;
    using ::ImGui_ImplOpenGL2_CreateDeviceObjects;
    using ::ImGui_ImplOpenGL2_DestroyDeviceObjects;
    using ::ImGui_ImplOpenGL2_Shutdown;
    using ::ImGui_ImplOpenGL2_Init;
    using ::ImGui_ImplOpenGL2_NewFrame;
}
