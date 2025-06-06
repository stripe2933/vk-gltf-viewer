export module vk_gltf_viewer:vulkan.buffer.SkinJointIndices;

import std;
export import fastgltf;
export import vk_mem_alloc_hpp;
export import :gltf.data_structure.SkinJointCountExclusiveScanWithCount;
#ifdef _MSC_VER
import :helpers.ranges;
#endif
export import :vulkan.buffer.StagingBufferStorage;
import :vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class SkinJointIndices : trait::PostTransferObject {
    public:
        SkinJointIndices(
            const fastgltf::Asset& asset,
            const gltf::ds::SkinJointCountExclusiveScanWithCount& skinJointCountExclusiveScanWithCount,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage
        ) : PostTransferObject { stagingBufferStorage },
            buffer { [&]() {
                std::vector<std::uint32_t> combinedJointIndices(skinJointCountExclusiveScanWithCount.back());
#ifdef _MSC_VER
                for (const auto& [skinIndex, skin] : asset.skins | ranges::views::enumerate) {
                    std::ranges::copy(
                        skin.joints | std::views::transform([](auto x) -> std::uint32_t { return x; }), 
                        combinedJointIndices.begin() + skinJointCountExclusiveScanWithCount[skinIndex]);
                }
#else
                // TODO: this code triggers ICE at MainApp.cpp when compiling with MSVC. Need investigation.
                for (const auto& [startIndex, skin] : std::views::zip(skinJointCountExclusiveScanWithCount, asset.skins)) {
                    std::ranges::copy(
                        skin.joints | std::views::transform([](auto x) -> std::uint32_t { return x; }),
                        combinedJointIndices.begin() + startIndex);
                }
#endif

                vku::AllocatedBuffer result = vku::MappedBuffer {
                    allocator,
                    std::from_range, combinedJointIndices,
                    vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc
                }.unmap();
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