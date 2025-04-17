export module vk_gltf_viewer:vulkan.buffer.SkinJointIndices;

import std;
export import fastgltf;
export import vk_mem_alloc_hpp;
import :vulkan.buffer;
export import :vulkan.buffer.StagingBufferStorage;
import :vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class SkinJointIndices : trait::PostTransferObject {
    public:
        SkinJointIndices(
            const fastgltf::Asset &asset,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage
        ) : PostTransferObject { stagingBufferStorage },
            buffer { [&]() {
                std::vector<std::vector<std::uint32_t>> jointIndices;
                jointIndices.reserve(asset.skins.size());
                for (const fastgltf::Skin &skin : asset.skins) {
                    jointIndices.emplace_back(std::from_range, skin.joints | std::views::transform([](std::size_t skinIndex) {
                        return static_cast<std::uint32_t>(skinIndex);
                    }));
                }

                vku::AllocatedBuffer result = createCombinedBuffer<true>(
                    allocator, jointIndices, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc).first;

                if (StagingBufferStorage::needStaging(result)) {
                    stagingBufferStorage.stage(result, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer);
                }

                return result;
            }() },
            descriptorInfo { buffer, 0, vk::WholeSize } { }

        [[nodiscard]] const vk::DescriptorBufferInfo &getDescriptorInfo() const noexcept {
            return descriptorInfo;
        }

    private:
        vku::AllocatedBuffer buffer;
        vk::DescriptorBufferInfo descriptorInfo;
    };
}