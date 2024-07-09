export module vku:rendering.Attachment;

import std;
export import vulkan_hpp;
export import :images.Image;

namespace vku {
    export struct Attachment {
        Image image;
        vk::raii::ImageView view;
    };
}