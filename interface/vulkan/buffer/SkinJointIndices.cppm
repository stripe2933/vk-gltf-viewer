export module vk_gltf_viewer:vulkan.buffer.SkinJointIndices;

import std;
export import fastgltf;
export import vk_mem_alloc_hpp;
import :vulkan.buffer;

namespace vk_gltf_viewer::vulkan::buffer {
    export class SkinJointIndices {
    public:
        SkinJointIndices(
            const fastgltf::Asset &asset,
            vma::Allocator allocator
        ) : buffer { [&]() {
                std::vector<std::vector<std::uint32_t>> jointIndices;
                jointIndices.reserve(std::max<std::size_t>(asset.skins.size(), 1));
                for (const fastgltf::Skin &skin : asset.skins) {
                    jointIndices.emplace_back(std::from_range, skin.joints | std::views::transform([](std::size_t skinIndex) {
                        return static_cast<std::uint32_t>(skinIndex);
                    }));
                }

                // Avoid zero-sized buffer
                if (jointIndices.empty()) {
                    jointIndices.push_back(std::vector { std::numeric_limits<std::uint32_t>::max() });
                }

                return createCombinedBuffer(allocator, jointIndices, vk::BufferUsageFlagBits::eStorageBuffer).first;
            }() },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

    private:
        vku::MappedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
    };
}