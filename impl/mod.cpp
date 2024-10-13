module;

#ifdef _MSC_VER
#include <istream>
#endif

module vk_gltf_viewer;

import :MainApp;

auto vk_gltf_viewer::run() -> void {
    MainApp{}.run();
}