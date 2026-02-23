module;

#include <imgui_impl_vulkan.h>

export module imgui.vulkan;

export import imgui;

export {
    // ----- Types -----

    using ::ImDrawData;
    using ::ImGui_ImplVulkanH_Window;
    using ::ImGui_ImplVulkan_InitInfo;
    using ::ImGui_ImplVulkan_PipelineInfo;
    using ::ImTextureData;
    using ::PFN_vkVoidFunction;
    using ::VkAllocationCallbacks;
    using ::VkColorSpaceKHR;
    using ::VkCommandBuffer;
    using ::VkDescriptorSet;
    using ::VkDevice;
    using ::VkFormat;
    using ::VkImageLayout;
    using ::VkImageUsageFlags;
    using ::VkImageView;
    using ::VkInstance;
    using ::VkPhysicalDevice;
    using ::VkPipeline;
    using ::VkPresentModeKHR;
    using ::VkSampler;
    using ::VkSurfaceKHR;
#ifdef IMGUI_HAS_DOCK
    using ::ImGuiViewport;
#endif

    // ----- Functions -----

    using ::ImGui_ImplVulkanH_CreateOrResizeWindow;
    using ::ImGui_ImplVulkanH_DestroyWindow;
    using ::ImGui_ImplVulkanH_GetMinImageCountFromPresentMode;
    using ::ImGui_ImplVulkanH_SelectPhysicalDevice;
    using ::ImGui_ImplVulkanH_SelectPresentMode;
    using ::ImGui_ImplVulkanH_SelectQueueFamilyIndex;
    using ::ImGui_ImplVulkanH_SelectSurfaceFormat;
    using ::ImGui_ImplVulkan_AddTexture;
    using ::ImGui_ImplVulkan_CreateMainPipeline;
    using ::ImGui_ImplVulkan_Init;
    using ::ImGui_ImplVulkan_LoadFunctions;
    using ::ImGui_ImplVulkan_NewFrame;
    using ::ImGui_ImplVulkan_RemoveTexture;
    using ::ImGui_ImplVulkan_RenderDrawData;
    using ::ImGui_ImplVulkan_SetMinImageCount;
    using ::ImGui_ImplVulkan_Shutdown;
    using ::ImGui_ImplVulkan_UpdateTexture;
#ifdef IMGUI_HAS_DOCK
    using ::ImGui_ImplVulkanH_GetWindowDataFromViewport;
#endif
}