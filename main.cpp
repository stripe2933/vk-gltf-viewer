#include <GLFW/glfw3.h>
#include <vulkan/vulkan_hpp_macros.hpp>

import vk_gltf_viewer;

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

int main() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	VULKAN_HPP_DEFAULT_DISPATCHER.init();

	vk_gltf_viewer::MainApp{}.run();

	glfwTerminate();
}