export module vk_gltf_viewer:GlobalState;

import :control.Camera;

namespace vk_gltf_viewer {
    export class GlobalState {
    public:
        control::Camera camera;

        [[nodiscard]] static auto getInstance() noexcept -> GlobalState&;

    private:
        GlobalState() noexcept;
    };
}