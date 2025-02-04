module;

#include <cassert>

#include <mikktspace.h>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.PrimitiveAttributes;

import std;
export import BS.thread_pool;
import :gltf.algorithm.MikktSpaceInterface;
export import :gltf.AssetProcessError;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
import :vulkan.buffer;
export import :vulkan.buffer.StagingBufferStorage;
export import :vulkan.Gpu;
export import :vulkan.shader_type.Accessor;
import :vulkan.trait.PostTransferObject;

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

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Storage of every primitive's attribute buffers in an asset, which can be addressed via buffer device addresses.
     */
    export class PrimitiveAttributes : trait::PostTransferObject {
    public:
        struct PrimitiveAccessors {
            shader_type::Accessor positionAccessor;
            std::optional<shader_type::Accessor> normalAccessor;
            std::optional<shader_type::Accessor> tangentAccessor;
            std::vector<shader_type::Accessor> texcoordAccessors;
            std::optional<shader_type::Accessor> colorAccessor;

            vk::DeviceAddress texcoordAccessorBufferAddress;
        };

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        PrimitiveAttributes(
            const fastgltf::Asset &asset,
            const Gpu &gpu,
            StagingBufferStorage &stagingBufferStorage,
            BS::thread_pool<> &threadPool,
            const BufferDataAdapter &adapter = {}
        ) : PostTransferObject { stagingBufferStorage },
            zeroBufferAddress { getZeroBufferAddress(gpu) },
            mappings { createMappings(asset, gpu, adapter) } {
            generateIndexedAttributeMappingInfos(gpu);
            generateMissingTangentBuffers(asset, gpu, threadPool, adapter);
        }

        [[nodiscard]] const PrimitiveAccessors &getAccessors(const fastgltf::Primitive &primitive) const {
            return mappings.at(&primitive);
        }

    private:
        std::vector<vku::AllocatedBuffer> internalBuffers;

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
         */
        vk::DeviceAddress zeroBufferAddress;

        std::unordered_map<const fastgltf::Primitive*, PrimitiveAccessors> mappings;

        [[nodiscard]] vk::DeviceAddress getZeroBufferAddress(const Gpu &gpu) {
            vku::AllocatedBuffer &buffer = internalBuffers.emplace_back(vku::MappedBuffer {
                gpu.allocator,
                std::array { 0.f, 0.f, 0.f, 0.f },
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
            }.unmap());
            if (StagingBufferStorage::needStaging(buffer)) {
                stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
            }

            return gpu.device.getBufferAddress({ buffer });
        }

        template <typename DataBufferAdapter>
        std::unordered_map<const fastgltf::Primitive*, PrimitiveAccessors> createMappings(
            const fastgltf::Asset &asset,
            const Gpu &gpu,
            const DataBufferAdapter &adapter
        ) {
            const auto primitives = asset.meshes | std::views::transform(&fastgltf::Mesh::primitives) | std::views::join;

            // Get buffer view indices that are used in primitive attributes.
            std::vector<std::size_t> attributeBufferViewIndices;
            for (const fastgltf::Primitive &primitive : primitives) {
                for (const fastgltf::Attribute &attribute : primitive.attributes) {
                    // Process only used attributes.
                    using namespace std::string_view_literals;
                    const bool isAttributeUsed
                        = ranges::one_of(attribute.name, "POSITION"sv, "NORMAL"sv, "TANGENT"sv, "COLOR_0"sv)
                        || attribute.name.starts_with("TEXCOORD_"sv);
                    if (!isAttributeUsed) continue;

                    const fastgltf::Accessor &accessor = asset.accessors[attribute.accessorIndex];

                    // Check accessor validity.
                    if (accessor.sparse) throw gltf::AssetProcessError::SparseAttributeBufferAccessor;

                    if (accessor.bufferViewIndex) {
                        attributeBufferViewIndices.push_back(*accessor.bufferViewIndex);
                    }
                    // Accessor without buffer view will be handled by zero buffer address.
                }
            }

            // Sort the used buffer view indices and remove duplicates.
            std::ranges::sort(attributeBufferViewIndices);
            const auto ret = std::ranges::unique(attributeBufferViewIndices);
            attributeBufferViewIndices.erase(ret.begin(), ret.end());

            // Create Vulkan buffer consisted of the buffer view data.
            auto [buffer, copyOffsets] = createCombinedBuffer<true>(
                gpu.allocator,
                attributeBufferViewIndices | std::views::transform([&](std::size_t bufferViewIndex) { return adapter(asset, bufferViewIndex); }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc);
            if (StagingBufferStorage::needStaging(buffer)) {
                stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
            }

            // Hashmap that can get buffer device address by corresponding buffer view index.
            const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ internalBuffers.emplace_back(std::move(buffer)) });
            const std::unordered_map<std::size_t, vk::DeviceAddress> bufferDeviceAddressMappings { std::from_range, std::views::zip(
                attributeBufferViewIndices,
                copyOffsets | std::views::transform([&](vk::DeviceSize offset) {
                    return bufferAddress + offset;
                }))
            };

            return primitives
                | std::views::transform([&](const fastgltf::Primitive &primitive) {
                    std::pair result { &primitive, PrimitiveAccessors{} };
                    auto &[_, accessors] = result;

                    // Get number of TEXCOORD_<i> attributes.
                    const auto attributeNames
                        = primitive.attributes
                        | std::views::transform([](const auto &attribute) -> std::string_view { return attribute.name; });
                    const std::size_t texcoordCount = std::ranges::count_if(
                        attributeNames,
                        [](std::string_view name) { return name.starts_with("TEXCOORD_"); });
                    accessors.texcoordAccessors.resize(texcoordCount);

                    // Process the attributes by identifying their names.
                    for (const auto &[attributeName, accessorIndex] : primitive.attributes) {
                        const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                        const auto getGpuAccessor = [&]() {
                            shader_type::Accessor result {
                                .componentType = static_cast<std::uint8_t>(getGLComponentType(accessor.componentType) - getGLComponentType(fastgltf::ComponentType::Byte)),
                                .componentCount = static_cast<std::uint8_t>(getNumComponents(accessor.type)),
                            };
                            if (accessor.bufferViewIndex) {
                                const std::size_t byteStride
                                    = asset.bufferViews[*accessor.bufferViewIndex].byteStride
                                    .value_or(getElementByteSize(accessor.type, accessor.componentType));
                                if (!std::in_range<std::uint8_t>(byteStride)) throw gltf::AssetProcessError::TooLargeAccessorByteStride;

                                result.bufferAddress = bufferDeviceAddressMappings.at(*accessor.bufferViewIndex) + accessor.byteOffset;
                                result.byteStride = static_cast<std::uint8_t>(byteStride);
                            }
                            else {
                                result.bufferAddress = zeroBufferAddress;
                                // Use zero stride to make every accessor element point to the zero buffer address.
                                result.byteStride = 0;
                            }

                            return result;
                        };

                        using namespace std::string_view_literals;

                        if (attributeName == "POSITION"sv) {
                            accessors.positionAccessor = getGpuAccessor();
                        }
                        else if (attributeName == "NORMAL"sv) {
                            accessors.normalAccessor.emplace(getGpuAccessor());
                        }
                        else if (attributeName == "TANGENT"sv) {
                            accessors.tangentAccessor.emplace(getGpuAccessor());
                        }
                        else if (constexpr auto prefix = "TEXCOORD_"sv; attributeName.starts_with(prefix)) {
                            std::size_t index;
                            if (auto result = parse<std::size_t>(std::string_view { attributeName }.substr(prefix.size()))) {
                                index = *result;
                            }
                            else {
                                // Attribute name starting with "TEXCOORD_", but the following string is not a number.
                                // TODO: would it be filtered by the glTF validation?
                                throw fastgltf::Error::InvalidOrMissingAssetField;
                            }

                            accessors.texcoordAccessors[index] = getGpuAccessor();
                        }
                        else if (attributeName == "COLOR_0"sv) {
                            accessors.colorAccessor.emplace(getGpuAccessor());
                        }
                    }

                    return result;
                })
                | std::ranges::to<std::unordered_map>();
        }

        void generateIndexedAttributeMappingInfos(const Gpu &gpu) {
            // Collect primitive infos to be filled.
            auto primitiveAccessorsWithTextures
                = mappings
                | std::views::values
                | std::views::filter([](const PrimitiveAccessors &accessors) {
                    return !accessors.texcoordAccessors.empty();
                });
            if (primitiveAccessorsWithTextures.empty()) {
                return;
            }

            auto [buffer, copyOffsets] = createCombinedBuffer<true>(
                gpu.allocator,
                primitiveAccessorsWithTextures | std::views::transform(&PrimitiveAccessors::texcoordAccessors),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc);
            if (StagingBufferStorage::needStaging(buffer)) {
                stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
            }

            const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ internalBuffers.emplace_back(std::move(buffer)) });
            for (auto &&[accessors, copyOffset] : std::views::zip(primitiveAccessorsWithTextures, copyOffsets)) {
                accessors.texcoordAccessorBufferAddress = bufferAddress + copyOffset;
            }
        }

        template <typename BufferDataAdapter>
        void generateMissingTangentBuffers(const fastgltf::Asset &asset, const Gpu &gpu, BS::thread_pool<> &threadPool, const BufferDataAdapter &adapter) {
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
                    const std::size_t texcoordIndex = getTexcoordIndex(*asset.materials[*pPrimitive->materialIndex].normalTexture);
                    return std::pair<PrimitiveAccessors*, gltf::algorithm::MikktSpaceMesh<BufferDataAdapter>> {
                        std::piecewise_construct,
                        std::tuple { &accessors },
                        std::tie(
                            asset,
                            asset.accessors[*pPrimitive->indicesAccessor],
                            asset.accessors[pPrimitive->findAttribute("POSITION")->accessorIndex],
                            asset.accessors[pPrimitive->findAttribute("NORMAL")->accessorIndex],
                            asset.accessors[pPrimitive->findAttribute(std::format("TEXCOORD_{}", texcoordIndex))->accessorIndex],
                            adapter),
                    };
                }))
                | std::ranges::to<std::vector>();
            if (missingTangentPrimitives.empty()) {
                return;
            }

            threadPool.submit_loop(std::size_t{ 0 }, missingTangentPrimitives.size(), [&](std::size_t i) {
                auto& mesh = missingTangentPrimitives[i].second;

                SMikkTSpaceInterface* const pInterface
                    = [indexType = mesh.indicesAccessor.componentType]() -> SMikkTSpaceInterface* {
                        switch (indexType) {
                            case fastgltf::ComponentType::UnsignedByte:
                                return &gltf::algorithm::mikktSpaceInterface<std::uint8_t, BufferDataAdapter>;
                            case fastgltf::ComponentType::UnsignedShort:
                                return &gltf::algorithm::mikktSpaceInterface<std::uint16_t, BufferDataAdapter>;
                            case fastgltf::ComponentType::UnsignedInt:
                                return &gltf::algorithm::mikktSpaceInterface<std::uint32_t, BufferDataAdapter>;
                            default:
                                // glTF Specification:
                                // The indices accessor MUST have SCALAR type and an unsigned integer component type.
                                std::unreachable();
                        }
                    }();
                if (const SMikkTSpaceContext context{ pInterface, &mesh }; !genTangSpaceDefault(&context)) {
                    throw std::runtime_error{ "Failed to generate the tangent attributes" };
                }
            }).get();

            auto [buffer, copyOffsets] = createCombinedBuffer<true>(
                gpu.allocator,
                missingTangentPrimitives | std::views::transform([](const auto &pair) {
                    return as_bytes(std::span { pair.second.tangents });
                }),
                vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc);
            if (StagingBufferStorage::needStaging(buffer)) {
                stagingBufferStorage.get().stage(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
            }

            const vk::DeviceAddress bufferAddress = gpu.device.getBufferAddress({ internalBuffers.emplace_back(std::move(buffer)) });
            for (auto [pPrimitive, copyOffset] : std::views::zip(missingTangentPrimitives | std::views::keys, copyOffsets)) {
                pPrimitive->tangentAccessor.emplace(bufferAddress + copyOffset, 16);
            }
        }
    };
}