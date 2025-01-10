export module vk_gltf_viewer:helpers.imgui.compat;

export import imgui;

export namespace ImGui {
    void EndItemTooltip() { EndTooltip(); }
    void EndPopupModal() { EndPopup(); }
    void PopMultiItemsWidths() { PopItemWidth(); }
} // namespace ImGui