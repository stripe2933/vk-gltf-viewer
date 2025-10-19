module;

#include <cassert>

export module vkgltf.bindless.SkinBuffer;

import std;
export import fastgltf;
export import vkgltf;
export import vku;

[[nodiscard]] std::size_t getJointCountTotal(const fastgltf::Asset &asset) noexcept;

namespace vkgltf {
    export class SkinBuffer {
    public:
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        class Config {
        public:
            const BufferDataAdapter &adapter;

            /**
             * @brief Vulkan buffer usage flags for the buffer creation.
             *
             * If \p stagingInfo is given, <tt>vk::BufferUsageFlagBits::eTransferSrc</tt> is added at the staging buffer
             * creation and the device local buffer will be created with <tt>usageFlags | vk::BufferUsageFlagBits::eTransferDst</tt>
             * usage.
             */
            vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;

            /**
             * @brief Queue family indices that the buffer can be concurrently accessed.
             *
             * If its size is less than 2, <tt>sharingMode</tt> of the buffer will be set to <tt>vk::SharingMode::eExclusive</tt>.
             */
            vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};

            /**
             * @brief VMA allocation creation flags for the buffer allocation.
             *
             * @note <tt>flags</tt> MUST contain either <tt>vma::AllocationCreateFlagBits::eHostAccessSequentialWrite</tt> or
             * <tt>vma::AllocationCreateFlagBits::eHostAccessRandom</tt> to allow the host to write to the buffer.
             */
            vma::AllocationCreateInfo allocationCreateInfo = vma::AllocationCreateInfo {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                vma::MemoryUsage::eAutoPreferHost,
            };

            const StagingInfo *stagingInfo = nullptr;
        };

        /**
         * @brief Vulkan buffer of glTF asset's skin joint indices combined into a single buffer, with tight-packed <tt>float</tt>s.
         *
         * You can obtain the region of the bytes data of <tt>i</tt>-th skin's joint indices via
         * <tt>getJointIndicesOffsetAndSize(i) = (byte offset, byte size)</tt>.
         */
        vku::raii::AllocatedBuffer jointIndices;

        /**
         * @brief Vulkan buffer of glTF asset's inverse bind matrices combined into a single buffer, with tight-packed <tt>fmat4x4</tt>s.
         *
         * If <tt>fastgltf::Skin::inverseBindMatrices</tt> is defined, the corresponding accessor data is copied to the
         * buffer. Otherwise, 4x4 identity matrices of the skin joint indices count is copied to the buffer.
         *
         * You can obtain the region of the bytes data of <tt>i</tt>-th skin's inverse bind matrices via
         * <tt>getInverseBindMatricesOffsetAndSize(i) = (byte offset, byte size)</tt>.
         */
        vku::raii::AllocatedBuffer inverseBindMatrices;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        SkinBuffer(const fastgltf::Asset &asset, vma::Allocator allocator, const Config<BufferDataAdapter> &config = {})
            : SkinBuffer { asset, allocator, config, getJointCountTotal(asset) } { }

        /**
         * @brief Get region of the bytes data of the <tt>i</tt>-th skin's joint indices.
         *
         * You may use this function's result to obtain the device address of joint index data, like:
         * @code{.cpp}
         * device.getBufferAddress({ skinBuffer.jointIndices }) + getJointIndicesOffsetAndSize(skinIndex).first
         * @endcode
         *
         * @param skinIndex Skin index to retrieve the byte region.
         * @return Pair of (byte offset, byte size).
         * @note The result byte offset and size will be always multiple of 4, as the indices are tight packed.
         */
        [[nodiscard]] std::pair<vk::DeviceSize, vk::DeviceSize> getJointIndicesOffsetAndSize(std::size_t skinIndex) const noexcept;

        /**
         * @brief Get region of the bytes data of the <tt>i</tt>-th skin's inverse bind matrices.
         *
         * You may use this function's result to obtain the device address of joint index data, like:
         * @code{.cpp}
         * device.getBufferAddress({ skinBuffer.inverseBindMatrices }) + getInverseBindMatricesOffsetAndSize(skinIndex).first
         * @endcode
         *
         * @param skinIndex Skin index to retrieve the byte region.
         * @return Pair of (byte offset, byte size).
         * @note The result byte offset and size will be always multiple of 64, as the 4x4 matrices are tight packed.
         */
        [[nodiscard]] std::pair<vk::DeviceSize, vk::DeviceSize> getInverseBindMatricesOffsetAndSize(std::size_t skinIndex) const noexcept;

        /**
         * @brief Construct <tt>SkinBuffer</tt> from \p asset, only if the asset has skin data.
         *
         * If there's no skin data to process, <tt>vk::InitializationFailedError</tt> will be thrown as Vulkan does not
         * allow zero-sized buffer creation. This function calculates the buffer size before the buffer creation and
         * returns <tt>std::nullopt</tt> if the size is zero, otherwise returns a <tt>SkinBuffer</tt> instance.
         *
         * @tparam BufferDataAdapter A functor type that return the bytes span from a glTF buffer view.
         * @param asset glTF asset.
         * @param allocator VMA allocator to allocate the buffer.
         * @param config Configuration for the skin buffer creation.
         * @return <tt>SkinBuffer</tt> instance if the asset has skin data, otherwise <tt>std::nullopt</tt>.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        [[nodiscard]] static std::optional<SkinBuffer> from(
            const fastgltf::Asset &asset,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config = {}
        ) {
            const std::size_t jointCountTotal = getJointCountTotal(asset);
            if (jointCountTotal == 0) {
                return std::nullopt;
            }

            // TODO: why this not compiled in Clang 20?
            // return std::optional<SkinBuffer> { std::in_place, asset, allocator, config, jointCountTotal };
            return SkinBuffer { asset, allocator, config, jointCountTotal };
        }

    private:
        std::vector<std::size_t> jointCountScanWithCount;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        SkinBuffer(const fastgltf::Asset &asset, vma::Allocator allocator, const Config<BufferDataAdapter> &config, std::size_t jointCountTotal)
            : jointIndices {
                allocator,
                vk::BufferCreateInfo {
                    {},
                    sizeof(std::uint32_t) * jointCountTotal,
                    config.usageFlags
                        | (config.stagingInfo ? vk::Flags { vk::BufferUsageFlagBits::eTransferSrc } : vk::BufferUsageFlags{}),
                    vku::getSharingMode(config.queueFamilies),
                    config.queueFamilies,
                },
                config.allocationCreateInfo,
            }
            , inverseBindMatrices {
                allocator,
                vk::BufferCreateInfo {
                    {},
                    sizeof(fastgltf::math::fmat4x4) * jointCountTotal,
                    config.usageFlags
                        | (config.stagingInfo ? vk::Flags { vk::BufferUsageFlagBits::eTransferSrc } : vk::BufferUsageFlags{}),
                    vku::getSharingMode(config.queueFamilies),
                    config.queueFamilies,
                },
                config.allocationCreateInfo,
            } {
            std::span jointIndicesData {
                static_cast<std::uint32_t*>(allocator.getAllocationInfo(jointIndices.allocation).pMappedData),
                jointCountTotal,
            };
            std::span inverseBindMatricesData {
                static_cast<fastgltf::math::fmat4x4*>(allocator.getAllocationInfo(inverseBindMatrices.allocation).pMappedData),
                jointCountTotal,
            };

            // glTF 2.0 specification:
            //   The number of elements of the accessor referenced by inverseBindMatrices MUST greater than or equal to the
            //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#skins-overviewnumber of joints elements.
            // Therefore, fastgltf::copyFromAccessor cannot be operated directly on the buffer, if the accessor element count is
            // greater than the skin joint count. For this, an additional buffer which has the capacity of storing the largest
            // accessor data can be used as a temporary storage, and then the clamped data can be copied to the buffer instead.
            std::size_t maxAccessorElementCount = 0;
            for (const fastgltf::Skin &skin : asset.skins) {
                if (skin.inverseBindMatrices) {
                    const fastgltf::Accessor &accessor = asset.accessors[*skin.inverseBindMatrices];
                    // Accumulate only the accessor count that exceeds the joint count.
                    if (accessor.count > skin.joints.size()) {
                        maxAccessorElementCount = std::max(maxAccessorElementCount, accessor.count);
                    }
                }
            }
            auto accessorDataBuffer = std::make_unique_for_overwrite<fastgltf::math::fmat4x4[]>(maxAccessorElementCount);

            auto jointIndexBufferIt = jointIndicesData.begin();
            auto inverseBindMatrixBufferIt = inverseBindMatricesData.begin();
            for (const fastgltf::Skin &skin : asset.skins) {
                jointIndexBufferIt = std::ranges::transform(skin.joints, jointIndexBufferIt, [](std::size_t n) noexcept {
                    return static_cast<std::uint32_t>(n);
                }).out;

                if (skin.inverseBindMatrices) {
                    const fastgltf::Accessor &accessor = asset.accessors[*skin.inverseBindMatrices];
                    if (accessor.count > skin.joints.size()) {
                        fastgltf::copyFromAccessor<fastgltf::math::fmat4x4>(asset, accessor, &accessorDataBuffer[0], config.adapter);
                        std::copy_n(&accessorDataBuffer[0], skin.joints.size(), inverseBindMatrixBufferIt);
                    }
                    else {
                        // If the accessor count is equal to the joint count, copy to the buffer directly.
                        fastgltf::copyFromAccessor<fastgltf::math::fmat4x4>(asset, accessor, std::to_address(inverseBindMatrixBufferIt), config.adapter);
                    }
                }
                else {
                    // Use identity matrices.
                    std::generate_n(inverseBindMatrixBufferIt, skin.joints.size(), []() noexcept {
                        return fastgltf::math::fmat4x4{};
                    });
                }

                std::advance(inverseBindMatrixBufferIt, skin.joints.size());
                jointCountScanWithCount.push_back(skin.joints.size());
            }

            assert(jointIndexBufferIt == jointIndicesData.end() && "jointIndices buffer size estimation failed");
            assert(inverseBindMatrixBufferIt == inverseBindMatricesData.end() && "inverseBindMatrices buffer size estimation failed");

            // Flush mapped memory ranges if the memory is not host coherent.
            if (!vku::contains(allocator.getAllocationMemoryProperties(jointIndices.allocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
                allocator.flushAllocation(jointIndices.allocation, 0, vk::WholeSize);
            }
            if (!vku::contains(allocator.getAllocationMemoryProperties(inverseBindMatrices.allocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
                allocator.flushAllocation(inverseBindMatrices.allocation, 0, vk::WholeSize);
            }

            jointCountScanWithCount.push_back(0);
            std::exclusive_scan(jointCountScanWithCount.begin(), jointCountScanWithCount.end(), jointCountScanWithCount.begin(), std::size_t{});

            if (config.stagingInfo) {
                config.stagingInfo->stage(jointIndices, config.usageFlags, config.queueFamilies);
                config.stagingInfo->stage(inverseBindMatrices, config.usageFlags, config.queueFamilies);
            }
        }
    };

    export template <>
    class SkinBuffer::Config<fastgltf::DefaultBufferDataAdapter> {
        static constexpr fastgltf::DefaultBufferDataAdapter adapter;

        // Make adapter accessible by SkinBuffer.
        friend class SkinBuffer;

    public:
        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
        vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};
        vma::AllocationCreateInfo allocationCreateInfo = vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
            vma::MemoryUsage::eAutoPreferHost,
        };
        const StagingInfo *stagingInfo = nullptr;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::size_t getJointCountTotal(const fastgltf::Asset &asset) noexcept {
    std::size_t result = 0;
    for (const fastgltf::Skin &skin : asset.skins) {
        result += skin.joints.size();
    }
    return result;
}

std::pair<vk::DeviceSize, vk::DeviceSize> vkgltf::SkinBuffer::getJointIndicesOffsetAndSize(
    std::size_t skinIndex
) const noexcept {
    return {
        sizeof(std::uint32_t) * jointCountScanWithCount[skinIndex],
        sizeof(std::uint32_t) * (jointCountScanWithCount[skinIndex + 1] - jointCountScanWithCount[skinIndex]),
    };
}

std::pair<vk::DeviceSize, vk::DeviceSize> vkgltf::SkinBuffer::getInverseBindMatricesOffsetAndSize(
    std::size_t skinIndex
) const noexcept {
    return {
        sizeof(fastgltf::math::fmat4x4) * jointCountScanWithCount[skinIndex],
        sizeof(fastgltf::math::fmat4x4) * (jointCountScanWithCount[skinIndex + 1] - jointCountScanWithCount[skinIndex]),
    };
}
