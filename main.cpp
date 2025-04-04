#include <GLFW/glfw3.h>
#include <vulkan/vulkan_hpp_macros.hpp>
#ifdef _WIN32
#include <windows.h>
#endif

import vk_gltf_viewer;
import vulkan_hpp;

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
#else
int main() {
#endif
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    VULKAN_HPP_DEFAULT_DISPATCHER.init();

    vk_gltf_viewer::run();

    glfwTerminate();
}