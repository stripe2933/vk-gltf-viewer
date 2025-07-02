module;

#include <mikktspace.h>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.buffer.PrimitiveAttributes;

import std;
export import BS.thread_pool;

import vk_gltf_viewer.gltf.algorithm.MikktSpaceInterface;
export import vk_gltf_viewer.gltf.AssetExternalBuffers;
export import vk_gltf_viewer.gltf.AssetProcessError;
import vk_gltf_viewer.helpers.fastgltf;
import vk_gltf_viewer.helpers.functional;
import vk_gltf_viewer.helpers.ranges;
import vk_gltf_viewer.vulkan.buffer;
export import vk_gltf_viewer.vulkan.buffer.StagingBufferStorage;
export import vk_gltf_viewer.vulkan.Gpu;
export import vk_gltf_viewer.vulkan.shader_type.Accessor;
import vk_gltf_viewer.vulkan.trait.PostTransferObject;

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Storage of every primitive's attribute buffers in an asset, which can be addressed via buffer device addresses.
     */
    export class PrimitiveAttributes : trait::PostTransferObject {
    public:
        struct PrimitiveAccessors {
            shader_type::Accessor positionAccessor;
            std::vector<shader_type::Accessor> positionMorphTargetAccessors;
            std::optional<shader_type::Accessor> normalAccessor;
            std::vector<shader_type::Accessor> normalMorphTargetAccessors;
            std::optional<shader_type::Accessor> tangentAccessor;
            std::vector<shader_type::Accessor> tangentMorphTargetAccessors;
            std::vector<shader_type::Accessor> texcoordAccessors;
            std::optional<shader_type::Accessor> colorAccessor;
            std::vector<shader_type::Accessor> jointsAccessors;
            std::vector<shader_type::Accessor> weightsAccessors;

            vk::DeviceAddress positionMorphTargetAccessorBufferAddress;
            vk::DeviceAddress normalMorphTargetAccessorBufferAddress;
            vk::DeviceAddress tangentMorphTargetAccessorBufferAddress;
            vk::DeviceAddress texcoordAccessorBufferAddress;
            vk::DeviceAddress jointsAccessorBufferAddress;
            vk::DeviceAddress weightsAccessorBufferAddress;
        };

        PrimitiveAttributes(
            const fastgltf::Asset &asset,
            const Gpu &gpu,
            StagingBufferStorage &stagingBufferStorage,
            BS::thread_pool<> &threadPool,
            const gltf::AssetExternalBuffers &adapter
        );

        [[nodiscard]] const PrimitiveAccessors &getAccessors(const fastgltf::Primitive &primitive) const;

    private:
        std::vector<vku::AllocatedBuffer> internalBuffers;
        std::unordered_map<const fastgltf::Primitive*, PrimitiveAccessors> mappings;

        /**
         * @brief Device address of the buffer of 16 byte zeros, which is used for accessor without buffer view.
         *
         * From glTF spec:
         *   When accessor buffer view is undefined, the accessor MUST be initialized with zeros.
         *
         * However, this doesn't imply that buffer of consecutive zeros has to be generated. Instead, it could be zero
         * filled with the largest accessor data type possible, and setting the stride in the GPU accessor as zero to
         * emulate the behavior. The largest component type of accessor in glTF spec is VEC4, which is 16 bytes, so the
         * buffer will be filled with 16 bytes of zeros (and stored in <tt>internalBuffers</tt>). Every fetch address
         * of that accessor would point to this buffer device address.
         *
         * @param gpu Vulkan GPU.
         * @return Device address of the zero buffer.
         * @note This function should be called only once in a GPU device lifetime.
         */
        [[nodiscard]] vk::DeviceAddress getZeroBufferAddress(const Gpu &gpu);

        std::unordered_map<const fastgltf::Primitive*, PrimitiveAccessors> createMappings(
            const fastgltf::Asset &asset,
            const Gpu &gpu,
            const gltf::AssetExternalBuffers &adapter
        );

        void generateCombinedAccessorMappingInfo(const Gpu &gpu, auto &&accessorGetter, auto &&bufferAddressGetter) {
            auto indexedAttributeAccessors
                = mappings
                | std::views::values
                | std::views::filter([&](const PrimitiveAccessors &accessors) {
                    return !std::invoke(accessorGetter, accessors).empty();
                });
            if (indexedAttributeAccessors.empty()) {
                return;
            }

            auto [buffer, copyOffsets] = createCombinedBuffer<true>(
                gpu.allocator,
                indexedAttributeAccessors | std::views::transform(accessorGetter),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
                vma::MemoryUsage::eAutoPreferHost);
            if (StagingBufferStorage::needStaging(buffer)) {
                stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
            }

            const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ internalBuffers.emplace_back(std::move(buffer)).buffer });
            for (auto &&[accessors, copyOffset] : std::views::zip(indexedAttributeAccessors, copyOffsets)) {
                std::invoke(bufferAddressGetter, accessors) = bufferAddress + copyOffset;
            }
        }

        void generateMissingTangentBuffers(const fastgltf::Asset &asset, const Gpu &gpu, BS::thread_pool<> &threadPool, const gltf::AssetExternalBuffers &adapter);
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

/**
 * @brief Parse a number from given \p str.
 * @tparam T Type of the number.
 * @param str String to parse.
 * @return Expected of parsed number, or error code if failed.
 */
template <std::integral T>
[[nodiscard]] std::expected<T, std::errc> parse(std::string_view str) {
    T value;
    if (auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value); ec == std::errc{}) {
        return value;
    }
    else {
        return std::unexpected { ec };
    }
}

vk_gltf_viewer::vulkan::buffer::PrimitiveAttributes::PrimitiveAttributes(
    const fastgltf::Asset &asset,
    const Gpu &gpu,
    StagingBufferStorage &stagingBufferStorage,
    BS::thread_pool<> &threadPool,
    const gltf::AssetExternalBuffers &adapter
) : PostTransferObject { stagingBufferStorage },
    mappings { createMappings(asset, gpu, adapter) } {
    // Generate mappings of indexed attribute accessors: {TEXCOORD,JOINTS,WEIGHTS}_<i>.
    generateCombinedAccessorMappingInfo(gpu, &PrimitiveAccessors::texcoordAccessors, &PrimitiveAccessors::texcoordAccessorBufferAddress);
    generateCombinedAccessorMappingInfo(gpu, &PrimitiveAccessors::jointsAccessors, &PrimitiveAccessors::jointsAccessorBufferAddress);
    generateCombinedAccessorMappingInfo(gpu, &PrimitiveAccessors::weightsAccessors, &PrimitiveAccessors::weightsAccessorBufferAddress);

    // Generate morph target accessors of POSITION, NORMAL and TANGENT attributes.
    generateCombinedAccessorMappingInfo(gpu, &PrimitiveAccessors::positionMorphTargetAccessors, &PrimitiveAccessors::positionMorphTargetAccessorBufferAddress);
    generateCombinedAccessorMappingInfo(gpu, &PrimitiveAccessors::normalMorphTargetAccessors, &PrimitiveAccessors::normalMorphTargetAccessorBufferAddress);
    generateCombinedAccessorMappingInfo(gpu, &PrimitiveAccessors::tangentMorphTargetAccessors, &PrimitiveAccessors::tangentMorphTargetAccessorBufferAddress);

    generateMissingTangentBuffers(asset, gpu, threadPool, adapter);
}

const vk_gltf_viewer::vulkan::buffer::PrimitiveAttributes::PrimitiveAccessors &vk_gltf_viewer::vulkan::buffer::PrimitiveAttributes::getAccessors(const fastgltf::Primitive &primitive) const {
    return mappings.at(&primitive);
}

vk::DeviceAddress vk_gltf_viewer::vulkan::buffer::PrimitiveAttributes::getZeroBufferAddress(const Gpu &gpu) {
    constexpr float data[4] = { 0.f, 0.f, 0.f, 0.f };
    vku::AllocatedBuffer &buffer = internalBuffers.emplace_back(
        gpu.allocator,
        vk::BufferCreateInfo {
            {},
            sizeof(data),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        },
        vma::AllocationCreateInfo {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
            vma::MemoryUsage::eAutoPreferHost,
        });
    gpu.allocator.copyMemoryToAllocation(data, buffer.allocation, 0, sizeof(data));

    if (StagingBufferStorage::needStaging(buffer)) {
        stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
    }

    return gpu.device.getBufferAddress({ buffer.buffer });
}

std::unordered_map<const fastgltf::Primitive*, vk_gltf_viewer::vulkan::buffer::PrimitiveAttributes::PrimitiveAccessors> vk_gltf_viewer::vulkan::buffer::PrimitiveAttributes::createMappings(
    const fastgltf::Asset &asset,
    const Gpu &gpu,
    const gltf::AssetExternalBuffers &adapter
) {
    using namespace std::string_view_literals;

    const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;

    // Get buffer view indices that are used in primitive attributes, or generate accessor data if it is not
    // compatible to the GPU accessor.
    std::vector<std::size_t> attributeBufferViewIndices;
    std::unordered_map<std::size_t, std::vector<std::byte>> generatedAccessorByteData;
    vk::DeviceAddress zeroBufferAddress = 0; // Generate this on-demand.
    for (const fastgltf::Primitive &primitive : primitives) {
        const auto isAccessorBufferViewCompatibleWithGpuAccessor = [&](const fastgltf::Accessor &accessor) {
            if (accessor.sparse) return false;

            if (accessor.bufferViewIndex) {
                const auto byteStride = asset.bufferViews[*accessor.bufferViewIndex].byteStride;
                if (byteStride && !std::in_range<std::uint8_t>(*byteStride)) return false;
            }

            return true;
        };

        // Primitive attributes.
        for (const fastgltf::Attribute &attribute : primitive.attributes) {
            // Process only used attributes.
            const bool isAttributeUsed
                = ranges::one_of<std::string_view>(attribute.name, { "POSITION", "NORMAL", "TANGENT", "COLOR_0" })
                || attribute.name.starts_with("TEXCOORD_"sv)
                || attribute.name.starts_with("JOINTS_"sv)
                || attribute.name.starts_with("WEIGHTS_"sv);
            if (!isAttributeUsed) continue;

            const fastgltf::Accessor &accessor = asset.accessors[attribute.accessorIndex];
            if (!isAccessorBufferViewCompatibleWithGpuAccessor(accessor)) {
                generatedAccessorByteData.emplace(attribute.accessorIndex, getVertexAttributeAccessorByteData(accessor, asset, adapter));
            }
            else if (accessor.bufferViewIndex) {
                attributeBufferViewIndices.push_back(*accessor.bufferViewIndex);
            }
            else if (zeroBufferAddress == 0) {
                // Accessor without buffer view will be handled by zero buffer address.
                zeroBufferAddress = getZeroBufferAddress(gpu);
            }
        }

        // Morph target attributes.
        for (std::span<const fastgltf::Attribute> attributes : primitive.targets) {
            for (const auto &[attributeName, accessorIndex] : attributes) {
                // Process only used attributes.
                // TODO: TEXCOORD_<i> and COLOR_0.
                const bool isAttributeUsed = ranges::one_of<std::string_view>(attributeName, { "POSITION", "NORMAL", "TANGENT" });
                if (!isAttributeUsed) continue;

                const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                if (!isAccessorBufferViewCompatibleWithGpuAccessor(accessor)) {
                    generatedAccessorByteData.emplace(accessorIndex, getVertexAttributeAccessorByteData(accessor, asset, adapter));
                }
                else if (accessor.bufferViewIndex) {
                    attributeBufferViewIndices.push_back(*accessor.bufferViewIndex);
                }
                else if (zeroBufferAddress == 0) {
                    // Accessor without buffer view will be handled by zero buffer address.
                    zeroBufferAddress = getZeroBufferAddress(gpu);
                }
            }
        }
    }

    // Sort the used buffer view indices and remove duplicates.
    std::ranges::sort(attributeBufferViewIndices);
    const auto ret = std::ranges::unique(attributeBufferViewIndices);
    attributeBufferViewIndices.erase(ret.begin(), ret.end());

    // Hashmap that can get buffer device address by corresponding buffer view index.
    std::unordered_map<std::size_t, vk::DeviceAddress> bufferDeviceAddressMappings;
    if (!attributeBufferViewIndices.empty()) {
        // Create Vulkan buffer consisted of the buffer view data.
        auto [buffer, copyOffsets] = createCombinedBuffer<true>(
            gpu.allocator,
            attributeBufferViewIndices | std::views::transform([&](std::size_t bufferViewIndex) { return adapter(asset, bufferViewIndex); }),
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
            vma::MemoryUsage::eAutoPreferHost);
        if (StagingBufferStorage::needStaging(buffer)) {
            stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        }

        const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ internalBuffers.emplace_back(std::move(buffer)).buffer });
        for (auto [bufferViewIndex, copyOffset] : std::views::zip(attributeBufferViewIndices, copyOffsets)) {
            bufferDeviceAddressMappings.emplace(bufferViewIndex, bufferAddress + copyOffset);
        }
    }

    // Hashmap that can get buffer device address by corresponding accessor index.
    std::unordered_map<std::size_t, vk::DeviceAddress> generatedBufferDeviceAddressMappings;
    if (!generatedAccessorByteData.empty()) {
        auto [buffer, copyOffsets] = createCombinedBuffer<true>(
            gpu.allocator,
            generatedAccessorByteData | std::views::values,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
            vma::MemoryUsage::eAutoPreferHost);
        if (StagingBufferStorage::needStaging(buffer)) {
            stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
        }

        const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ internalBuffers.emplace_back(std::move(buffer)).buffer });
        for (auto [bufferViewIndex, copyOffset] : std::views::zip(generatedAccessorByteData | std::views::keys, copyOffsets)) {
            generatedBufferDeviceAddressMappings.emplace(bufferViewIndex, bufferAddress + copyOffset);
        }
    }

    return primitives
        | std::views::transform([&](const fastgltf::Primitive &primitive) {
            std::pair result { &primitive, PrimitiveAccessors{} };
            auto &[_, accessors] = result;

            const auto resizeIndexedAccessors = [&](std::string_view prefix, std::vector<shader_type::Accessor> &accessors) {
                // Get number of indexed attributes.
                const auto attributeNames
                    = primitive.attributes
                    | std::views::transform([](const auto &attribute) -> std::string_view { return attribute.name; });
                const std::size_t attributeCount = std::ranges::count_if(
                    attributeNames,
                    [&](std::string_view name) { return name.starts_with(prefix); });
                accessors.resize(attributeCount);
            };
            resizeIndexedAccessors("TEXCOORD_"sv, accessors.texcoordAccessors);
            resizeIndexedAccessors("JOINTS_"sv, accessors.jointsAccessors);
            resizeIndexedAccessors("WEIGHTS_"sv, accessors.weightsAccessors);

            const auto getGpuAccessor = [&](std::size_t accessorIndex) {
                const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                shader_type::Accessor result {
                    .componentType = static_cast<std::uint8_t>((accessor.normalized ? 8U : 0U)
                        | (getGLComponentType(accessor.componentType) - getGLComponentType(fastgltf::ComponentType::Byte))),
                    .componentCount = static_cast<std::uint8_t>(getNumComponents(accessor.type)),
                };

                if (auto it = generatedBufferDeviceAddressMappings.find(accessorIndex); it != generatedBufferDeviceAddressMappings.end()) {
                    result.bufferAddress = it->second;
                    result.byteStride = getElementByteSize(accessor.type, accessor.componentType);
                    result.byteStride = (result.byteStride / 4 + (result.byteStride % 4 != 0)) * 4; // Align to 4 bytes.
                }
                else if (accessor.bufferViewIndex) {
                    const std::uint8_t byteStride
                        = asset.bufferViews[*accessor.bufferViewIndex].byteStride
                        .value_or(getElementByteSize(accessor.type, accessor.componentType));
                    result.bufferAddress = bufferDeviceAddressMappings.at(*accessor.bufferViewIndex) + accessor.byteOffset;
                    result.byteStride = byteStride;
                }
                else {
                    result.bufferAddress = zeroBufferAddress;
                    // Use zero stride to make every accessor element point to the zero buffer address.
                    result.byteStride = 0;
                }

                return result;
            };

            using namespace std::string_view_literals;

            // Process the attributes by identifying their names.
            for (const auto &[attributeName, accessorIndex] : primitive.attributes) {
                if (attributeName == "POSITION"sv) {
                    accessors.positionAccessor = getGpuAccessor(accessorIndex);
                }
                else if (attributeName == "NORMAL"sv) {
                    accessors.normalAccessor.emplace(getGpuAccessor(accessorIndex));
                }
                else if (attributeName == "TANGENT"sv) {
                    accessors.tangentAccessor.emplace(getGpuAccessor(accessorIndex));
                }
                else if (attributeName == "COLOR_0"sv) {
                    accessors.colorAccessor.emplace(getGpuAccessor(accessorIndex));
                }
                else {
                    const auto assignAccessor = [&](std::string_view prefix, std::vector<shader_type::Accessor> &accessors) {
                        std::size_t index;
                        if (auto result = parse<std::size_t>(std::string_view { attributeName }.substr(prefix.size()))) {
                            index = *result;
                        }
                        else {
                            // Attribute name starting with "TEXCOORD_", but the following string is not a number.
                            // TODO: would it be filtered by the glTF validation?
                            throw fastgltf::Error::InvalidOrMissingAssetField;
                        }

                        accessors[index] = getGpuAccessor(accessorIndex);
                    };

                    if (constexpr auto prefix = "TEXCOORD_"sv; attributeName.starts_with(prefix)) {
                        assignAccessor(prefix, accessors.texcoordAccessors);
                    }
                    else if (constexpr auto prefix = "JOINTS_"sv; attributeName.starts_with(prefix)) {
                        assignAccessor(prefix, accessors.jointsAccessors);
                    }
                    else if (constexpr auto prefix = "WEIGHTS_"sv; attributeName.starts_with(prefix)) {
                        assignAccessor(prefix, accessors.weightsAccessors);
                    }
                }
            }

            // Morph targets processing.
            for (std::span<const fastgltf::Attribute> attributes : primitive.targets) {
                for (const auto &[attributeName, accessorIndex] : attributes) {
                    if (attributeName == "POSITION"sv) {
                        accessors.positionMorphTargetAccessors.push_back(getGpuAccessor(accessorIndex));
                    }
                    else if (attributeName == "NORMAL"sv) {
                        accessors.normalMorphTargetAccessors.push_back(getGpuAccessor(accessorIndex));
                    }
                    else if (attributeName == "TANGENT"sv) {
                        accessors.tangentMorphTargetAccessors.push_back(getGpuAccessor(accessorIndex));
                    }
                }
            }

            return result;
        })
        | std::ranges::to<std::unordered_map>();
}

void vk_gltf_viewer::vulkan::buffer::PrimitiveAttributes::generateMissingTangentBuffers(
    const fastgltf::Asset &asset,
    const Gpu &gpu,
    BS::thread_pool<> &threadPool,
    const gltf::AssetExternalBuffers &adapter
) {
    // Collect primitives that are missing tangent attributes (and require it).
    std::vector missingTangentPrimitives
        = mappings
        | std::views::filter(decomposer([&](const fastgltf::Primitive *pPrimitive, const PrimitiveAccessors &accessors) {
            // Skip if primitive already has a tangent attribute.
            if (accessors.tangentAccessor) return false;

            const auto &materialIndex = pPrimitive->materialIndex;
            // Skip if primitive doesn't have a material.
            if (!materialIndex) return false;
            // Skip if primitive material doesn't have a normal texture.
            if (!asset.materials[*materialIndex].normalTexture) return false;

            // Skip if primitive is non-indexed geometry (screen-space normal and tangent will be generated in the shader).
            if (!pPrimitive->indicesAccessor) return false;

            // Skip if primitive doesn't have normal attribute (screen-space normal and tangent will be generated in the shader).
            if (!accessors.normalAccessor.has_value()) return false;

            return true;
        }))
        | std::views::transform(decomposer([&](const fastgltf::Primitive *pPrimitive, PrimitiveAccessors &accessors) {
            return std::pair<PrimitiveAccessors*, gltf::algorithm::MikktSpaceMesh> {
                std::piecewise_construct,
                std::tuple { &accessors },
                std::tie(asset, *pPrimitive, adapter),
            };
        }))
        | std::ranges::to<std::vector>();
    if (missingTangentPrimitives.empty()) {
        return;
    }

    threadPool.submit_loop(std::size_t{ 0 }, missingTangentPrimitives.size(), [&](std::size_t i) {
        auto& mesh = missingTangentPrimitives[i].second;

        gltf::algorithm::MikktSpaceInterface interface;
        if (const SMikkTSpaceContext context{ &interface, &mesh }; !genTangSpaceDefault(&context)) {
            throw std::runtime_error{ "Failed to generate the tangent attributes" };
        }
    }).get();

    auto [buffer, copyOffsets] = createCombinedBuffer<true>(
        gpu.allocator,
        missingTangentPrimitives | std::views::transform([](const auto &pair) {
            return as_bytes(std::span { pair.second.tangents });
        }),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        vma::MemoryUsage::eAutoPreferHost);
    if (StagingBufferStorage::needStaging(buffer)) {
        stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
    }

    const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ internalBuffers.emplace_back(std::move(buffer)).buffer });
    for (auto [pPrimitive, copyOffset] : std::views::zip(missingTangentPrimitives | std::views::keys, copyOffsets)) {
        pPrimitive->tangentAccessor.emplace(bufferAddress + copyOffset, 6, 4, 16);
    }
}