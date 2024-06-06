module;

#include <compare>

export module vku:wsi;

#ifdef VKU_USE_GLFW
export import :wsi.GlfwWindow;
#endif