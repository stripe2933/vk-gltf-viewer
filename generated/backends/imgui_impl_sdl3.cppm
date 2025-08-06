module;

#include <imgui_impl_sdl3.h>

export module imgui_impl_sdl3;

export import imgui;

export {
    // ----- Types -----

    using ::SDL_Window;
    using ::SDL_Event;
    using ::SDL_Renderer;
    using ::ImGui_ImplSDL3_GamepadMode;
    using ::SDL_Gamepad;

    // ----- Functions -----

    using ::ImGui_ImplSDL3_InitForSDLRenderer;
    using ::ImGui_ImplSDL3_SetGamepadMode;
    using ::ImGui_ImplSDL3_InitForSDLGPU;
    using ::ImGui_ImplSDL3_NewFrame;
    using ::ImGui_ImplSDL3_InitForD3D;
    using ::ImGui_ImplSDL3_Shutdown;
    using ::ImGui_ImplSDL3_InitForOther;
    using ::ImGui_ImplSDL3_InitForOpenGL;
    using ::ImGui_ImplSDL3_InitForMetal;
    using ::ImGui_ImplSDL3_InitForVulkan;
    using ::ImGui_ImplSDL3_ProcessEvent;
}
