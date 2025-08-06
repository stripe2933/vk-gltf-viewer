module;

#include <imgui_impl_vulkan.h>

export module imgui_impl_vulkan;

export import imgui;

export {
    // ----- Types -----

    using ::VkCommandBuffer;
    using ::VkPipeline;
    using ::VkImageView;
    using ::ImTextureData;
    using ::ImGui_ImplVulkanH_Window;
    using ::VkAllocationCallbacks;
    using ::VkSampler;
    using ::VkFormat;
    using ::VkSurfaceKHR;
    using ::PFN_vkVoidFunction;
    using ::VkPhysicalDevice;
    using ::VkPresentModeKHR;
    using ::VkDescriptorSet;
    using ::VkImageLayout;
    using ::VkInstance;
    using ::VkDevice;
    using ::ImGui_ImplVulkan_InitInfo;
    using ::ImDrawData;
    using ::VkColorSpaceKHR;

    // ----- Functions -----

    using ::ImGui_ImplVulkan_AddTexture;
    using ::ImGui_ImplVulkan_LoadFunctions;
    using ::ImGui_ImplVulkanH_SelectPhysicalDevice;
    using ::ImGui_ImplVulkanH_DestroyWindow;
    using ::ImGui_ImplVulkan_NewFrame;
    using ::ImGui_ImplVulkanH_CreateOrResizeWindow;
    using ::ImGui_ImplVulkan_RemoveTexture;
    using ::ImGui_ImplVulkan_Init;
    using ::ImGui_ImplVulkanH_SelectSurfaceFormat;
    using ::ImGui_ImplVulkanH_SelectQueueFamilyIndex;
    using ::ImGui_ImplVulkanH_SelectPresentMode;
    using ::ImGui_ImplVulkan_SetMinImageCount;
    using ::ImGui_ImplVulkan_Shutdown;
    using ::ImGui_ImplVulkan_RenderDrawData;
    using ::ImGui_ImplVulkan_UpdateTexture;
    using ::ImGui_ImplVulkanH_GetMinImageCountFromPresentMode;
}
