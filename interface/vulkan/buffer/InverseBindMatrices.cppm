module;

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.buffer.InverseBindMatrices;

import std;
export import fastgltf;

export import vk_gltf_viewer.gltf.AssetExternalBuffers;
export import vk_gltf_viewer.gltf.data_structure.SkinJointCountExclusiveScanWithCount;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.span;
export import vk_gltf_viewer.vulkan.buffer.StagingBufferStorage;
import vk_gltf_viewer.vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class InverseBindMatrices final : public vku::AllocatedBuffer, trait::PostTransferObject {
    public:
        std::reference_wrapper<const gltf::ds::SkinJointCountExclusiveScanWithCount> skinJointCountExclusiveScanWithCount;
        
        InverseBindMatrices(
            const fastgltf::Asset &asset,
            const gltf::ds::SkinJointCountExclusiveScanWithCount& skinJointCountExclusiveScanWithCount LIFETIMEBOUND,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage,
            const gltf::AssetExternalBuffers &adapter
        );
    };

}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vk_gltf_viewer::vulkan::buffer::InverseBindMatrices::InverseBindMatrices(
    const fastgltf::Asset &asset,
    const gltf::ds::SkinJointCountExclusiveScanWithCount& skinJointCountExclusiveScanWithCount,
    vma::Allocator allocator,
    StagingBufferStorage &stagingBufferStorage,
    const gltf::AssetExternalBuffers &adapter
) : AllocatedBuffer { 
        allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(fastgltf::math::fmat4x4) * skinJointCountExclusiveScanWithCount.back(),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferHost,
        },
    },
    PostTransferObject { stagingBufferStorage },
    skinJointCountExclusiveScanWithCount { skinJointCountExclusiveScanWithCount } {
    std::size_t maxSparseAccessorCount = 0;
    std::size_t maxContinuousIdentityMatrixCount = 0;
    for (const fastgltf::Skin &skin : asset.skins) {
        if (skin.inverseBindMatrices) {
            const fastgltf::Accessor &accessor = asset.accessors[*skin.inverseBindMatrices];
            if (accessor.sparse) {
                maxSparseAccessorCount = std::max(maxSparseAccessorCount, accessor.count);
            }
        }
        else {
            maxContinuousIdentityMatrixCount = std::max(maxContinuousIdentityMatrixCount, skin.joints.size());
        }
    }

    auto sparseAccessorDataBuffer = std::make_unique_for_overwrite<fastgltf::math::fmat4x4[]>(maxSparseAccessorCount);
    const auto contiguousIdentityMatrices = std::make_unique<fastgltf::math::fmat4x4[]>(maxContinuousIdentityMatrixCount);

    vk::DeviceSize byteOffset = 0;
    for (const fastgltf::Skin &skin : asset.skins) {
        std::span<const fastgltf::math::fmat4x4> data;
        if (skin.inverseBindMatrices) {
            const fastgltf::Accessor &accessor = asset.accessors[*skin.inverseBindMatrices];
            if (accessor.sparse) {
                fastgltf::copyFromAccessor<fastgltf::math::fmat4x4>(asset, accessor, &sparseAccessorDataBuffer[0], adapter);
                data = { &sparseAccessorDataBuffer[0], skin.joints.size() };
            }
            else {
                data = reinterpret_span<const fastgltf::math::fmat4x4>(fastgltf::getByteRegion(asset, accessor, adapter)).subspan(0, skin.joints.size());
            }
        }
        else {
            data = { &contiguousIdentityMatrices[0], skin.joints.size() };
        }

        const vk::DeviceSize byteSize = data.size_bytes();
        allocator.copyMemoryToAllocation(data.data(), allocation, byteOffset, byteSize);
        byteOffset += byteSize;
    }

    if (StagingBufferStorage::needStaging(*this)) {
        stagingBufferStorage.stage(*this, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
    }
}