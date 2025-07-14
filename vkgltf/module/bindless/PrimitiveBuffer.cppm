export module vkgltf.bindless.PrimitiveBuffer;

import std;
export import fastgltf;
export import vkgltf;
export import vku;

export import vkgltf.bindless.shader_type.Primitive;

namespace vkgltf {
    /**
     * @brief Vulkan buffer of every addressable primitive data in the glTF asset.
     *
     * The following diagram explains the data layout of the buffer.
     *
     *   0 +-------------------+----------------------------+---------------------+
     *     |                   |                            |      &(buffer)      | Device address of beginning element in the buffer.
     *   8 |                   |                            |---------------------|
     *     |                   |     POSITION: Accessor     | Component type: u32 | Identifiable number of component type and normalization.
     *  12 |                   |                            |---------------------|
     *     |                   |                            |   Byte stride: u32  | Byte stride of each element, must be multiple of 4.
     *  16 +                   +----------------------------+---------------------+
     *     |                   |                            |
     *     |                   |      NORMAL: Accessor      |   Component type is calculated as:
     *     |                   |                            |
     *  32 +                   +----------------------------+     fastgltf::Accessor accessor = ...
     *     |                   |                            |     std::uint32_t componentType = getGLComponentType(accessor.componentType) - getGLComponentType(fastgltf::ComponentType::UnsignedByte);
     *     |                   |      TANGENT: Accessor     |     if (accessor.normalized) componentType |= 8;
     *     |                   |                            |
     *  48 +                   +----------------------------+   Result:
     *     |                   |                            |     - BYTE: 0
     *     |                   |     TEXCOORD_0: Accessor   |     - UNSIGNED BYTE: 1
     *     |                   |                            |     - SHORT: 2
     *  64 +                   +----------------------------+     - UNSIGNED SHORT: 3
     *     |                   |                            |     - FLOAT: 6
     *     |                   |     TEXCOORD_1: Accessor   |     - BYTE normalized: 8
     *     |                   |                            |     - UNSIGNED BYTE normalized: 9
     *  80 +                   +----------------------------+     - SHORT normalized: 10
     *     |                   |                            |     - UNSIGNED SHORT normalized: 11
     *     |                   |     TEXCOORD_2: Accessor   |
     *     |     Primitive     |                            |
     *  96 +                   +----------------------------+
     *     |                   |                            |
     *     |                   |     TEXCOORD_3: Accessor   |
     *     |                   |                            |
     * 112 +                   +----------------------------+
     *     |                   |                            |
     *     |                   |       COLOR_0: Accessor    |
     *     |                   |                            |
     * 128 +                   +----------------------------+
     *     |                   |  &(POSITION morph mapping) | ---+
     * 136 +                   +----------------------------+    |
     *     |                   |   &(NORMAL morph mapping)  | ---|---+
     * 144 +                   +----------------------------+    |   |
     *     |                   |  &(TANGENT morph mapping)  | ---|---|------> nullptr (if no morph target presented)
     * 152 +                   +----------------------------+    |   |
     *     |                   |    &(JOINTS_<i> mapping)   |    |   |
     * 160 +                   +----------------------------+    |   |
     *     |                   |   &(WEIGHTS_<i> mapping)   |    |   |
     * 168 +                   +----------------------------+    |   |
     *     |                   |     Material index: u32    |    |   |
     * 172 +                   +----------------------------+    |   |
     *     |                   |          [PADDING]         |    |   | (for make sizeof(shader_type::Primitive) % 16 == 0)
     * 176 +-------------------+----------------------------+    |   |
     *     |     Primitive     |                                 |   |
     *     +-------------------+                                 |   |
     *     |        ...        |                                 |   |
     *     +-------------------+                                 |   |
     *     |     Primitive     |                                 |   |
     *     +-------------------+ <-------------------------------+   |
     *     |     Accessor      | (0-th POSITION morph target)        |
     *     +-------------------+                                     |
     *     |     Accessor      | (1-th POSITION morph target)        |
     *     +-------------------+                                     |
     *     |         ...       |                                     |
     *     +-------------------+                                     |
     *     |     Accessor      | (N-th POSITION morph target)        |
     *     +-------------------+ <-----------------------------------+
     *     |     Accessor      | (0-th NORMAL morph target)
     *     +-------------------+
     *     |     Accessor      | (1-th NORMAL morph target)
     *     +-------------------+
     *     |         ...       |
     *     +-------------------+
     *     |     Accessor      | (N-th NORMAL morph target)
     *     +-------------------+
     *     |         ...       |
     *     +-------------------+
     */
    export class PrimitiveBuffer : public vku::AllocatedBuffer {
    public:
        struct Config {
            struct DefaultMaterialIndexFn {
            #if __cpp_static_call_operator >= 202207L
                [[nodiscard]] static std::int32_t operator()(const fastgltf::Primitive &primitive) noexcept {
            #else
                [[nodiscard]] std::int32_t operator()(const fastgltf::Primitive &primitive) const noexcept {
            #endif
                    return primitive.materialIndex.value_or(std::int32_t { -1 });
                }
            };

            /**
             * @brief A function that returns the material index for the given primitive.
             *
             * By default, it returns the material index of the primitive or -1 if the primitive has no material. You can
             * customize the behavior by providing your own function.
             *
             * One example is adding the material index by 1 and use 0 for the primitives without material: in this case,
             * you can make the material buffer as a storage buffer, whose first element is a fallback material. Then
             * the shader can access the material by the index without checking if the primitive has a material or not.
             */
            std::function<std::int32_t(const fastgltf::Primitive&)> materialIndexFn = DefaultMaterialIndexFn{};

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
            vk::ArrayProxy<std::uint32_t> queueFamilies = {};

            /**
             * @brief VMA allocation creation flags for the buffer allocation.
             *
             * @note <tt>flags</tt> MUST contain either <tt>vma::AllocationCreateFlagBits::eHostAccessSequentialWrite</tt> or
             * <tt>vma::AllocationCreateFlagBits::eHostAccessRandom</tt> to allow the host to write to the buffer.
             */
            vma::AllocationCreateInfo allocationCreateInfo = {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                vma::MemoryUsage::eAutoPreferHost,
            };

            const StagingInfo *stagingInfo = nullptr;
        };

        vk::DescriptorBufferInfo descriptorInfo;
        std::span<shader_type::Primitive> mappedData;

        PrimitiveBuffer(
            const fastgltf::Asset &asset,
            const std::unordered_map<const fastgltf::Primitive*, PrimitiveAttributeBuffers> &primitiveAttributes,
            const vk::raii::Device &device,
            vma::Allocator allocator,
            const Config &config = {
                .materialIndexFn = Config::DefaultMaterialIndexFn{},
                .usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress,
                .queueFamilies = {},
                .allocationCreateInfo = {
                    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite | vma::AllocationCreateFlagBits::eMapped,
                    vma::MemoryUsage::eAutoPreferHost,
                },
                .stagingInfo = nullptr,
            }
        );

        /**
         * @brief Get index of the primitive stored in the buffer.
         * @param primitive Primitive to get the index of.
         * @return Index of the primitive in the buffer.
         */
        [[nodiscard]] std::size_t getPrimitiveIndex(const fastgltf::Primitive &primitive) const;

        /**
         * @brief Get primitive from index.
         * @param index Index of the primitive to get.
         * @return Reference to the primitive at the given index.
         */
        [[nodiscard]] const fastgltf::Primitive &getPrimitive(std::size_t index) const noexcept;

    private:
        struct IData {
            std::vector<const fastgltf::Primitive*> orderedPrimitives;
            vk::DeviceSize bufferSize;
            std::size_t mappingCount;
            vk::DeviceSize mappingDataByteOffset;

            IData(const fastgltf::Asset &asset, const std::unordered_map<const fastgltf::Primitive*, PrimitiveAttributeBuffers> &primitiveAttributes);
        };

        std::vector<const fastgltf::Primitive*> orderedPrimitives;
        std::unordered_map<const fastgltf::Primitive*, std::size_t> primitiveIndices;

        PrimitiveBuffer(
            const std::unordered_map<const fastgltf::Primitive*, PrimitiveAttributeBuffers> &primitiveAttributes,
            const vk::raii::Device &device,
            vma::Allocator allocator,
            const Config &config,
            IData &&intermediateData
        );
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vkgltf::PrimitiveBuffer::PrimitiveBuffer(
    const fastgltf::Asset &asset,
    const std::unordered_map<const fastgltf::Primitive*, PrimitiveAttributeBuffers> &primitiveAttributes,
    const vk::raii::Device &device,
    vma::Allocator allocator,
    const Config &config
) : PrimitiveBuffer { primitiveAttributes, device, allocator, config, IData { asset, primitiveAttributes } } { }

std::size_t vkgltf::PrimitiveBuffer::getPrimitiveIndex(const fastgltf::Primitive &primitive) const {
    return primitiveIndices.at(&primitive);
}

const fastgltf::Primitive & vkgltf::PrimitiveBuffer::getPrimitive(std::size_t index) const noexcept {
    return *orderedPrimitives[index];
}

vkgltf::PrimitiveBuffer::IData::IData(
    const fastgltf::Asset &asset,
    const std::unordered_map<const fastgltf::Primitive*, PrimitiveAttributeBuffers> &primitiveAttributes
) {
    for (const fastgltf::Mesh &mesh : asset.meshes) {
        for (const fastgltf::Primitive &primitive : mesh.primitives) {
            if (primitive.findAttribute("POSITION") == primitive.attributes.end()) {
                // glTF 2.0 specification:
                //   When positions are not specified, client implementations SHOULD skip primitive’s rendering unless its
                //   positions are provided by other means (e.g., by an extension). This applies to both indexed and
                //   non-indexed geometry.
                //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
                continue;
            }

            orderedPrimitives.push_back(&primitive);
        }
    }

    bufferSize = sizeof(shader_type::Primitive) * orderedPrimitives.size();

    mappingCount = 0;
    for (const PrimitiveAttributeBuffers &attributes : primitiveAttributes | std::views::values) {
        mappingCount += attributes.position.morphTargets.size() + attributes.joints.size() + attributes.weights.size();

        if (attributes.normal) {
            mappingCount += attributes.normal->morphTargets.size();

            if (attributes.tangent) {
                mappingCount += attributes.tangent->morphTargets.size();
            }
        }
    }

    mappingDataByteOffset = bufferSize;
    bufferSize = mappingDataByteOffset + sizeof(shader_type::Accessor) * mappingCount;
}

vkgltf::PrimitiveBuffer::PrimitiveBuffer(
    const std::unordered_map<const fastgltf::Primitive*, PrimitiveAttributeBuffers> &primitiveAttributes,
    const vk::raii::Device &device,
    vma::Allocator allocator,
    const Config &config,
    IData &&intermediateData
) : AllocatedBuffer {
        allocator,
        vk::BufferCreateInfo {
            {},
            intermediateData.bufferSize,
            config.usageFlags
                | (config.stagingInfo ? vk::Flags { vk::BufferUsageFlagBits::eTransferSrc } : vk::BufferUsageFlags{}),
            config.queueFamilies.size() < 2 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
            config.queueFamilies,
        },
        config.allocationCreateInfo,
    },
    descriptorInfo { *this, 0, sizeof(shader_type::Primitive) * intermediateData.orderedPrimitives.size() },
    orderedPrimitives { std::move(intermediateData.orderedPrimitives) } {
    vma::Allocation mappedAllocation = allocation;
    std::byte* mapped = static_cast<std::byte*>(allocator.getAllocationInfo(mappedAllocation).pMappedData);
    mappedData = { reinterpret_cast<shader_type::Primitive*>(mapped), orderedPrimitives.size() };

    if (config.stagingInfo && config.stagingInfo->stage(*this, config.usageFlags, config.queueFamilies)) {
        descriptorInfo.buffer = *this;
    }

    const auto toGpuAccessor = [&](const PrimitiveAttributeBuffers::AttributeInfo &info) -> shader_type::Accessor {
        constexpr std::uint32_t byteComponentType = getGLComponentType(fastgltf::ComponentType::Byte);
        return shader_type::Accessor {
            .bufferAddress = device.getBufferAddress({ static_cast<vk::Buffer>(*info.buffer) }) + info.offset,
            .componentType = (info.normalized ? 8U : 0U) | (getGLComponentType(info.componentType) - byteComponentType),
            .byteStride = static_cast<std::uint32_t>(info.stride),
        };
    };

    // Write the given span of AttributeInfo to the mapping data region, and return the start buffer device address of
    // the written accessor data.
    auto emplaceAccessors
        = [&,
            bufferAddress = device.getBufferAddress({ static_cast<vk::Buffer>(*this) }) + intermediateData.mappingDataByteOffset,
            it = std::span { reinterpret_cast<shader_type::Accessor*>(mapped + intermediateData.mappingDataByteOffset), intermediateData.mappingCount }.begin()
        ](
            std::span<const PrimitiveAttributeBuffers::AttributeInfo> attributeInfos
        ) mutable {
            vk::DeviceAddress result = bufferAddress;
            bufferAddress += sizeof(shader_type::Accessor) * attributeInfos.size();
            for (const PrimitiveAttributeBuffers::AttributeInfo &info : attributeInfos) {
                *it++ = toGpuAccessor(info);
            }
            return result;
        };

    std::ranges::transform(
        orderedPrimitives, mappedData.begin(),
        [&](const fastgltf::Primitive *primitive) {
            const PrimitiveAttributeBuffers &attributes = primitiveAttributes.at(primitive);

            shader_type::Primitive result {
                .positionAccessor = toGpuAccessor(attributes.position.attributeInfo),
                .positionMorphTargetAccessorBufferDeviceAddress = emplaceAccessors(attributes.position.morphTargets),
                .jointAccessorBufferDeviceAddress = emplaceAccessors(attributes.joints),
                .weightAccessorBufferDeviceAddress = emplaceAccessors(attributes.weights),
                .materialIndex = std::invoke(config.materialIndexFn, *primitive),
            };

            if (attributes.normal) {
                result.normalAccessor = toGpuAccessor(attributes.normal->attributeInfo);
                result.normalMorphTargetAccessorBufferDeviceAddress = emplaceAccessors(attributes.normal->morphTargets);

                if (attributes.tangent) {
                    result.tangentAccessor = toGpuAccessor(attributes.tangent->attributeInfo);
                    result.tangentMorphTargetAccessorBufferDeviceAddress = emplaceAccessors(attributes.tangent->morphTargets);
                }
            }

            for (auto &&[accessor, info] : std::views::zip(result.texcoordAccessors, attributes.texcoords)) {
                accessor = toGpuAccessor(info.attributeInfo);
            }

            if (!attributes.colors.empty()) {
                result.color0Accessor = toGpuAccessor(attributes.colors[0].attributeInfo);
            }

            return result;
        });

    if (!vku::contains(allocator.getAllocationMemoryProperties(mappedAllocation), vk::MemoryPropertyFlagBits::eHostCoherent)) {
        // Created buffer is non-coherent. Flush the mapped memory range.
        allocator.flushAllocation(mappedAllocation, 0, size);
    }

    for (std::size_t i = 0; const fastgltf::Primitive *primitive : orderedPrimitives) {
        primitiveIndices[primitive] = i++;
    }
}