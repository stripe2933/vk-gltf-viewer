module;

#include <imgui_impl_vulkan.h>

export module imgui.vulkan;

export import imgui;

export using ::ImGui_ImplVulkan_AddTexture;
export using ::ImGui_ImplVulkan_Init;
export using ::ImGui_ImplVulkan_InitInfo;
export using ::ImGui_ImplVulkan_NewFrame;
export using ::ImGui_ImplVulkan_RemoveTexture;
export using ::ImGui_ImplVulkan_RenderDrawData;
export using ::ImGui_ImplVulkan_Shutdown;

// --------------------
// Not part of ImGui, but these definitions are necessary for interoping with Vulkan-Hpp module.
// --------------------

export using ::VkImageLayout;