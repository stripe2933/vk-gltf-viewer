module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.buffer.SkinJointIndices;

import std;
export import fastgltf;
export import vku;

export import vk_gltf_viewer.gltf.data_structure.SkinJointCountExclusiveScanWithCount;

namespace vk_gltf_viewer::vulkan::buffer {
    export struct SkinJointIndices final : vku::AllocatedBuffer {
        std::reference_wrapper<const gltf::ds::SkinJointCountExclusiveScanWithCount> skinJointCountExclusiveScanWithCount;

        SkinJointIndices(
            const fastgltf::Asset& asset,
            const gltf::ds::SkinJointCountExclusiveScanWithCount& skinJointCountExclusiveScanWithCount LIFETIMEBOUND,
            vma::Allocator allocator
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::buffer::SkinJointIndices::SkinJointIndices(
    const fastgltf::Asset& asset,
    const gltf::ds::SkinJointCountExclusiveScanWithCount& skinJointCountExclusiveScanWithCount,
    vma::Allocator allocator
) : AllocatedBuffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(std::uint32_t) * skinJointCountExclusiveScanWithCount.back(),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferHost,
        },
    },
    skinJointCountExclusiveScanWithCount { skinJointCountExclusiveScanWithCount } {
    // Get maximum joint count of all skins.
    std::size_t maxJointCount = 0;
    for (const fastgltf::Skin &skin : asset.skins) {
        maxJointCount = std::max(maxJointCount, skin.joints.size());
    }
    auto convertedJointIndices = std::make_unique_for_overwrite<std::uint32_t[]>(maxJointCount);

    vk::DeviceSize byteOffset = 0;
    for (const fastgltf::Skin &skin : asset.skins) {
        std::ranges::copy(skin.joints, &convertedJointIndices[0]);

        const vk::DeviceSize byteSize = sizeof(std::uint32_t) * skin.joints.size();
        allocator.copyMemoryToAllocation(&convertedJointIndices[0], allocation, byteOffset, byteSize);
        byteOffset += byteSize;
    }
}