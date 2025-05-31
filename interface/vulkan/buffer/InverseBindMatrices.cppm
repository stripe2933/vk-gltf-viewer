export module vk_gltf_viewer.vulkan.buffer.InverseBindMatrices;

import std;
export import fastgltf;
import glm; // TODO: use fastgltf::math::fmat4x4 when it gets being trivially copyable.

import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.span;
import vk_gltf_viewer.vulkan.buffer;
export import vk_gltf_viewer.vulkan.buffer.StagingBufferStorage;
import vk_gltf_viewer.vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    export class InverseBindMatrices : trait::PostTransferObject {
    public:
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        InverseBindMatrices(
            const fastgltf::Asset &asset,
            vma::Allocator allocator,
            StagingBufferStorage &stagingBufferStorage,
            const BufferDataAdapter &adapter = {}
        ) : PostTransferObject { stagingBufferStorage },
            buffer { [&]() {
                // When a skin's inverseBindMatrices is not defined, identity matrices must be used. It first calculates
                // the maximum number of skin joints count (=largestContinuousIdentityMatrixCount), and allocate
                // contiguous identity matrices of size largestContinuousIdentityMatrixCount. Then, each skin's
                // inverseBindMatrices can use the subspan of the contiguous identity matrices.
                std::size_t largestContinuousIdentityMatrixCount = 0;
                for (const fastgltf::Skin &skin : asset.skins) {
                    if (!skin.inverseBindMatrices && skin.joints.size() > largestContinuousIdentityMatrixCount) {
                        largestContinuousIdentityMatrixCount = skin.joints.size();
                    }
                }
                const std::vector contiguousIdentityMatrices(largestContinuousIdentityMatrixCount, glm::identity<glm::mat4>());

                std::vector<std::vector<fastgltf::math::fmat4x4>> sparseAccessorData;
                std::vector<std::span<const glm::mat4>> matrices;
                for (const fastgltf::Skin &skin : asset.skins) {
                    if (skin.inverseBindMatrices) {
                        const fastgltf::Accessor &accessor = asset.accessors[*skin.inverseBindMatrices];
                        if (accessor.sparse) {
                            std::vector<fastgltf::math::fmat4x4> &v = sparseAccessorData.emplace_back(accessor.count);
                            fastgltf::copyFromAccessor<fastgltf::math::fmat4x4>(asset, accessor, v.data(), adapter);
                            matrices.push_back(reinterpret_span<const glm::mat4>(std::span { v }));
                        }
                        else {
                            matrices.push_back(reinterpret_span<const glm::mat4>(fastgltf::getByteRegion(asset, accessor, adapter)));
                        }
                    }
                    else {
                        matrices.push_back(contiguousIdentityMatrices);
                    }

                    // Only first N (=# of joints in a skin) matrices are used.
                    matrices.back() = matrices.back().subspan(0, skin.joints.size());
                }

                vku::AllocatedBuffer result = createCombinedBuffer<true>(
                    allocator, matrices, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc).first;

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