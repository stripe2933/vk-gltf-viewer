module vk_gltf_viewer;
import :AppState;

import std;
import glm;
import vku;

vk_gltf_viewer::AppState::AppState() noexcept
    : camera { glm::vec3 { 5.f }, normalize(glm::vec3 { -1.f }), glm::vec3 { 0.f, 1.f, 0.f },
        glm::radians(45.f), 1.f /* will be determined by passthru rect dimension */, 1e-2f, 1e6f } // Use reverse Z
{ }