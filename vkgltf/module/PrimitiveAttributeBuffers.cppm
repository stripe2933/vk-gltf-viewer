module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vkgltf.PrimitiveAttributeBuffers;

import std;
export import fastgltf;
export import vku;

#ifdef USE_MIKKTSPACE
import vkgltf.mikktspace;
import vkgltf.util;
#endif

#define FWD(...) static_cast<decltype(__VA_ARGS__)&&>(__VA_ARGS__)

using namespace std::string_view_literals;

using ComponentCountVariant = std::variant<std::integral_constant<int, 1>, std::integral_constant<int, 2>,
                                           std::integral_constant<int, 3>, std::integral_constant<int, 4>>;
using ComponentTypeVariant = std::variant<std::type_identity<std::int8_t>, std::type_identity<std::uint8_t>,
                                          std::type_identity<std::int16_t>, std::type_identity<std::uint16_t>,
                                          std::type_identity<std::int32_t>, std::type_identity<std::uint32_t>,
                                          std::type_identity<float>>;

[[nodiscard]] ComponentCountVariant getComponentCountVariant(fastgltf::AccessorType type) noexcept;
[[nodiscard]] ComponentTypeVariant getComponentTypeVariant(fastgltf::ComponentType componentType) noexcept;

/**
 * @brief Parse index in <tt>attributeName[prefixSize:]</tt>.
 *
 * @code{.cpp}
 * parseIndex("TEXCOORD_0", 9) == 0
 * parseIndex("JOINTS_2", 7) == 2
 * @endcode
 *
 * @param attributeName glTF 2.0 indexed attribute name.
 * @param prefixSize Size of the prefix in \p attributeName to skip.
 * @return Index in the attribute name.
 * @throw std::invalid_argument If <tt>attributeName[prefixSize:]</tt> is not a valid index.
 */
[[nodiscard]] std::size_t parseIndex(std::string_view attributeName, std::size_t prefixSize);

namespace vkgltf {
    export class PrimitiveAttributeBuffers {
    public:
        struct AttributeInfo {
            /**
             * @brief Vulkan buffer that contains the attribute data.
             * Ownership of the buffer is shared across the instances of <tt>PrimitiveAttributeBuffers</tt>.
             */
            std::shared_ptr<vku::AllocatedBuffer> buffer;

            /**
             * @brief Offset in <tt>buffer</tt> where the attribute data starts.
             */
            vk::DeviceSize offset;

            /**
             * @brief Size of the attribute data in the buffer.
             */
            vk::DeviceSize size;

            /**
             * @brief Stride of the attribute data in the buffer. Always a non-zero multiple of 4.
             */
            vk::DeviceSize stride;

            /**
             * @brief Component type of the attribute data.
             *
             * This may be different from the accessor <tt>componentType</tt> if you passed non-identity
             * <tt>componentTypeFn</tt> to the configuration of <tt>PrimitiveAttributeBuffers</tt> constructor.
             */
            fastgltf::ComponentType componentType;

            /**
            * @brief Boolean indicating whether the attribute data is normalized.
             *
             * This may be different from the accessor <tt>normalized</tt> value if accessor's component type is
             * normalized integral type, and <tt>componentTypeFn</tt> you passed is returning
             * <tt>fastgltf::ComponentType::Float</tt> for them.
             */
            bool normalized;
        };

        struct AttributeInfoWithMorphTargets {
            AttributeInfo attributeInfo;
            std::vector<AttributeInfo> morphTargets;
        };

        class AttributeInfoCache {
        public:
            template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
            class Config {
            public:
                const BufferDataAdapter &adapter;

                /**
                 * @brief Maximum count of <tt>TEXCOORD_<i></tt> attributes to parse. Default by 2.
                 *
                 * For example, if this value is 2, only <tt>TEXCOORD_0</tt> and <tt>TEXCOORD_1</tt> attributes
                 * are parsed.
                 */
                std::size_t maxTexcoordAttributeCount = 2;

                /**
                 * @brief Maximum count of <tt>COLOR_<i></tt> attributes to parse. Default by 1.
                 *
                 * For example, if this value is 2, only <tt>COLOR_0</tt> and <tt>COLOR_1</tt> attributes
                 * are parsed.
                 *
                 * @note You would usually remain this value as 1, as only <tt>COLOR_0</tt> attribute is used for vertex color in glTF 2.0.
                 */
                std::size_t maxColorAttributeCount = 1;

                /**
                 * @brief Maximum count of <tt>JOINTS_<i></tt> attributes to parse. Default by 1.
                 *
                 * For example, if this value is 2, only <tt>JOINTS_0</tt> and <tt>JOINTS_1</tt> attributes
                 * are parsed.
                 */
                std::size_t maxJointsAttributeCount = 1;

                /**
                 * @brief Maximum count of <tt>WEIGHTS_<i></tt> attributes to parse. Default by 1.
                 *
                 * For example, if this value is 2, only <tt>WEIGHTS_0</tt> and <tt>WEIGHTS_1</tt> attributes
                 * are parsed.
                 */
                std::size_t maxWeightsAttributeCount = 1;

                /**
                 * @brief A functor determines that if the accessor buffer view data can be used as-is. The resulting
                 * <tt>vk::BufferUsageFlags</tt> will be used to create the Vulkan buffer. If <tt>std::nullopt</tt> is
                 * returned, the accessor buffer view data will not be cached.
                 *
                 * @note This function is called for accessors that have a buffer view, therefore you don't have to
                 * check the existence.
                 */
                std::function<std::optional<vk::BufferUsageFlags>(const fastgltf::Accessor&)> usageFlagsFn
                    = [](const fastgltf::Accessor &accessor) -> std::optional<vk::BufferUsageFlags> {
                        if (accessor.sparse) return std::nullopt;
                        return vk::BufferUsageFlagBits::eVertexBuffer;
                    };

                /**
                 * @brief Queue family indices that the buffer can be concurrently accessed.
                 *
                 * If its size is less than 2, <tt>sharingMode</tt> of the buffer will be set to <tt>vk::SharingMode::eExclusive</tt>.
                 */
                vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};

                /**
                 * @brief VMA allocation creation flags for the buffer allocation.
                 * @note <tt>flags</tt> MUST contain either <tt>vma::AllocationCreateFlagBits::eHostAccessSequentialWrite</tt> or
                 * <tt>vma::AllocationCreateFlagBits::eHostAccessRandom</tt> to allow the host to write to the buffer.
                 */
                vma::AllocationCreateInfo allocationCreateInfo = {
                    vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                    vma::MemoryUsage::eAutoPreferHost,
                };
            };

            /**
             * @brief Vulkan buffers that have glTF buffer view data.
             */
            std::unordered_map<std::size_t /* buffer view index */, std::shared_ptr<vku::AllocatedBuffer>> bufferViewBuffers;

            /**
             * @brief Cached attribute infos for glTF accessors.
             */
            std::unordered_map<std::size_t /* accessor index */, AttributeInfo> accessorAttributeInfos;

            /**
             * @brief Creates a cache that stores the Vulkan buffers of compatible glTF buffer views and accessors.
             * @tparam BufferDataAdapter A functor type that return the bytes span from a glTF buffer view.
             * @param asset glTF asset.
             * @param allocator Vulkan memory allocator.
             * @param config Cache configuration.
             */
            template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
            AttributeInfoCache(
                const fastgltf::Asset &asset,
                vma::Allocator allocator,
                const Config<BufferDataAdapter> &config = {}
            ) {
                std::unordered_map<std::size_t, vk::BufferUsageFlags> bufferViewUsages;
                std::unordered_set<std::size_t> accessors;

                // Collect accessors whose buffer view data is compatible (which is determined by usageFlagsFn),
                // and their buffer view usages.
                for (const fastgltf::Mesh &mesh : asset.meshes) {
                    for (const fastgltf::Primitive &primitive : mesh.primitives) {
                        // glTF 2.0 specification:
                        //   When positions are not specified, client implementations SHOULD skip primitive’s rendering unless its
                        //   positions are provided by other means (e.g., by an extension). This applies to both indexed and
                        //   non-indexed geometry.
                        //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
                        if (primitive.findAttribute("POSITION") == primitive.attributes.end()) {
                            continue;
                        }

                        for (const auto &[attributeName, accessorIndex] : primitive.attributes) {
                            const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                            if (!accessor.bufferViewIndex) continue;

                            if (attributeName == "POSITION"sv || attributeName == "NORMAL"sv || attributeName == "TANGENT"sv) { }
                            else if (attributeName.starts_with("TEXCOORD")) {
                                if (parseIndex(attributeName, 9) >= config.maxTexcoordAttributeCount) {
                                    continue;
                                }
                            }
                            else if (attributeName.starts_with("COLOR")) {
                                if (parseIndex(attributeName, 6) >= config.maxColorAttributeCount) {
                                    continue;
                                }
                            }
                            else if (attributeName.starts_with("JOINTS")) {
                                if (parseIndex(attributeName, 7) >= config.maxJointsAttributeCount) {
                                    continue;
                                }
                            }
                            else if (attributeName.starts_with("WEIGHTS")) {
                                if (parseIndex(attributeName, 8) >= config.maxWeightsAttributeCount) {
                                    continue;
                                }
                            }
                            else {
                                continue;
                            }

                            if (auto usage = config.usageFlagsFn(accessor)) {
                                bufferViewUsages[*accessor.bufferViewIndex] |= *usage;
                                accessors.emplace(accessorIndex);
                            }
                        }

                        for (const auto &attributes : primitive.targets) {
                            for (const auto &[attributeName, accessorIndex] : attributes) {
                                const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                                if (!accessor.bufferViewIndex) continue;

                                if (attributeName == "POSITION"sv || attributeName == "NORMAL"sv || attributeName == "TANGENT"sv) { }
                                else if (attributeName.starts_with("TEXCOORD_")) {
                                    if (parseIndex(attributeName, 9) >= config.maxTexcoordAttributeCount) {
                                        continue;
                                    }
                                }
                                else if (attributeName.starts_with("COLOR_")) {
                                    if (parseIndex(attributeName, 6) >= config.maxColorAttributeCount) {
                                        continue;
                                    }
                                }
                                else {
                                    continue;
                                }

                                if (auto usage = config.usageFlagsFn(accessor)) {
                                    bufferViewUsages[*accessor.bufferViewIndex] |= *usage;
                                    accessors.emplace(accessorIndex);
                                }
                            }
                        }
                    }
                }

                // Create Vulkan buffers for the buffer views and copy the data into them.
                for (const auto &[bufferViewIndex, usage] : bufferViewUsages) {
                    const std::span<const std::byte> bufferViewData = config.adapter(asset, bufferViewIndex);
                    auto &buffer = bufferViewBuffers.emplace(
                        bufferViewIndex,
                        std::make_shared<vku::AllocatedBuffer>(
                            allocator,
                            vk::BufferCreateInfo {
                                {},
                                bufferViewData.size(),
                                usage,
                                config.queueFamilies.size() < 2 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
                                config.queueFamilies,
                            },
                            config.allocationCreateInfo)).first->second;
                    allocator.copyMemoryToAllocation(bufferViewData.data(), buffer->allocation, 0, buffer->size);
                }

                for (std::size_t accessorIndex : accessors) {
                    accessorAttributeInfos.emplace(
                        accessorIndex,
                        createAttributeInfoFromBufferViewCache(asset, asset.accessors[accessorIndex]));
                }
            }

        private:
            [[nodiscard]] AttributeInfo createAttributeInfoFromBufferViewCache(const fastgltf::Asset &asset, const fastgltf::Accessor &accessor) const;
        };

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        class Config {
        public:
            const BufferDataAdapter &adapter;

            /**
             * @brief A cache that contains Vulkan buffers that store either glTF buffer view data.
             *
             * This can be used across multiple <tt>PrimitiveAttributeBuffers</tt> instances to avoid creating redundant Vulkan buffers.
             */
            const AttributeInfoCache *cache = nullptr;

            /**
             * @brief Maximum count of <tt>TEXCOORD_<i></tt> attributes to parse. Default by 2.
             *
             * For example, if this value is 2, only <tt>TEXCOORD_0</tt> and <tt>TEXCOORD_1</tt> attributes
             * are parsed.
             *
             * @note You should match this value to <tt>AttributeInfoCache::Config::maxTexcoordAttributeCount</tt> if provided.
             */
            std::size_t maxTexcoordAttributeCount = 2;

            /**
             * @brief Maximum count of <tt>COLOR_<i></tt> attributes to parse. Default by 1.
             *
             * For example, if this value is 2, only <tt>COLOR_0</tt> and <tt>COLOR_1</tt> attributes
             * are parsed.
             *
             * @note You would usually remain this value as 0, as only <tt>COLOR_0</tt> attribute is used for vertex color in glTF 2.0.
             * @note You should match this value to <tt>AttributeInfoCache::Config::maxColorAttributeCount</tt> if provided.
             */
            std::size_t maxColorAttributeCount = 1;

            /**
             * @brief Maximum count of <tt>JOINTS_<i></tt> attributes to parse. Default by 1.
             *
             * For example, if this value is 2, only <tt>JOINTS_0</tt> and <tt>JOINTS_1</tt> attributes
             * are parsed.
             *
             * @note You should match this value to <tt>AttributeInfoCache::Config::maxJointsAttributeCount</tt> if provided.
             */
            std::size_t maxJointsAttributeCount = 1;

            /**
             * @brief Maximum count of <tt>WEIGHTS_<i></tt> attributes to parse. Default by 1.
             *
             * For example, if this value is 2, only <tt>WEIGHTS_0</tt> and <tt>WEIGHTS_1</tt> attributes
             * are parsed.
             *
             * @note You should match this value to <tt>AttributeInfoCache::Config::maxWeightsAttributeCount</tt> if provided.
             */
            std::size_t maxWeightsAttributeCount = 1;

        #ifdef USE_MIKKTSPACE
            /**
             * @brief Generate missing tangents using MikkTSpace algorithm at the initialization, with given component type.
             *
             * glTF 2.0 specification:
             *   When tangents are not specified, client implementations SHOULD calculate tangents using default
             *   MikkTSpace algorithms with the specified vertex positions, normals, and texture coordinates
             *   associated with the normal texture.
             *   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
             *
             * If the followings are satisfied, setting this to the proper component type (either <tt>Float</tt>,
             * <tt>Byte normalized</tt> or <tt>Short normalized</tt>) will generate tight-packed tangent data using
             * MikkTSpace algorithm in the constructor.
             *
             * - The given primitive is:
             *   - indexed,
             *   - has <tt>POSITION</tt>, <tt>NORMAL</tt> but not <tt>TANGENT</tt> attribute,
             *   - has a material with a normal texture, and has corresponding <tt>TEXCOORD_<i></tt> attribute,
             *   - and all the attributes referenced in above don't have morph targets.
             */
            std::optional<fastgltf::ComponentType> mikkTSpaceTangentComponentType = fastgltf::ComponentType::Float;
        #endif

            /**
             * @brief A functor that converts the component type of the generated accessor data.
             *
             * This can be useful if your renderer doesn't support quantized vertex attributes, by passing a function
             * that converts the quantized component type to <tt>float</tt>. Then all components are converted to 32-bit
             * floating type, and you can consistently use <tt>vk::Format::eR32G32B32Sfloat</tt> for <tt>POSITION</tt>
             * and <tt>NORMAL</tt>, <tt>vk::Format::eR32G32B32A32Sfloat</tt> for <tt>TANGENT</tt>, and
             * <tt>vk::Format::eR32G32Sfloat</tt> for <tt>TEXCOORD_<i></tt> attributes.
             *
             * @note It only affects to the generated accessor data, not the cached buffer view data (as it uses data
             * inside in the glTF buffer view as-is). For the accessor you want to convert the component type,
             * make <tt>AttributeInfoCache::Config::usageFlagsFn</tt> returns <tt>std::nullopt</tt>.
             * @warning Do not convert unnormalized component type to the normalized one as it is unsupported.
             */
            std::function<fastgltf::ComponentType(const fastgltf::Accessor&)> componentTypeFn = &fastgltf::Accessor::componentType;

            /**
             * @brief Vulkan buffer usage flags for the buffer creation.
             */
            vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eVertexBuffer;

            /**
             * @brief Queue family indices that the buffer can be concurrently accessed.
             *
             * If its size is less than 2, <tt>sharingMode</tt> of the buffer will be set to <tt>vk::SharingMode::eExclusive</tt>.
             */
            vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};

            /**
             * @brief VMA allocation creation flags for the buffer allocation.
             */
            vma::AllocationCreateInfo allocationCreateInfo = {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                vma::MemoryUsage::eAutoPreferHost,
            };
        };

        AttributeInfoWithMorphTargets position;
        std::optional<AttributeInfoWithMorphTargets> normal;
        std::optional<AttributeInfoWithMorphTargets> tangent;
        std::vector<AttributeInfoWithMorphTargets> texcoords;
        std::vector<AttributeInfoWithMorphTargets> colors;
        std::vector<AttributeInfo> joints;
        std::vector<AttributeInfo> weights;

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        PrimitiveAttributeBuffers(
            const fastgltf::Asset &asset,
            const fastgltf::Primitive &primitive,
            vma::Allocator allocator,
            const Config<BufferDataAdapter> &config = {}
        ) : asset { asset },
            primitive { primitive } {
            const auto getAttributeInfo = [&](std::size_t accessorIndex) -> AttributeInfo {
                if (config.cache) {
                    if (auto it = config.cache->accessorAttributeInfos.find(accessorIndex); it != config.cache->accessorAttributeInfos.end()) {
                        return it->second;
                    }
                }

                // Cache miss. Create a new dedicated accessor attribute info.
                const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];

                const fastgltf::ComponentType componentType = config.componentTypeFn(accessor);
                const std::size_t elementByteSize = getElementByteSize(accessor.type, componentType);

                // glTF 2.0 specification:
                //   For performance and compatibility reasons, each element of a vertex attribute MUST be aligned to 4-byte
                //   boundaries inside a bufferView (i.e., accessor.byteOffset and bufferView.byteStride MUST be multiples
                //   of 4).
                //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#data-alignment
                const vk::DeviceSize byteStride = (elementByteSize / 4 + (elementByteSize % 4 != 0)) * 4;

                AttributeInfo result {
                    .buffer = std::make_shared<vku::AllocatedBuffer>(
                        allocator,
                        vk::BufferCreateInfo {
                            {},
                            byteStride * accessor.count,
                            config.usageFlags,
                            config.queueFamilies.size() < 2 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
                            config.queueFamilies,
                        },
                        config.allocationCreateInfo),
                    .offset = 0,
                    .size = byteStride * (accessor.count - 1) + elementByteSize,
                    .stride = byteStride,
                    .componentType = componentType,
                    // glTF 2.0 specification:
                    //   Specifies whether integer data values are normalized (true) to [0, 1] (for unsigned types) or to
                    //   [-1, 1] (for signed types) when they are accessed. This property MUST NOT be set to true for
                    //   accessors with FLOAT or UNSIGNED_INT component type.
                    //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_accessor_normalized
                    // If accessor component type is converted to FLOAT, normalized must be false.
                    .normalized = accessor.normalized && componentType != fastgltf::ComponentType::Float,
                };

                // Map memory, copy data from the accessor, unmap memory.
                void* const mapped = allocator.mapMemory(result.buffer->allocation);
                visit([&]<typename T>(auto N, std::type_identity<T>) {
                    using ElementType = std::conditional_t<N == 1, T, fastgltf::math::vec<T, N>>;
                    constexpr std::size_t Stride = (sizeof(ElementType) / 4 + (sizeof(ElementType) % 4 != 0)) * 4;
                    copyFromAccessor<ElementType, Stride>(asset, accessor, mapped, config.adapter);
                }, getComponentCountVariant(accessor.type), getComponentTypeVariant(componentType));
                allocator.unmapMemory(result.buffer->allocation);

                const vk::MemoryPropertyFlags memoryProps = allocator.getAllocationMemoryProperties(result.buffer->allocation);
                if (!vku::contains(memoryProps, vk::MemoryPropertyFlagBits::eHostCoherent)) {
                    // Created buffer is non-coherent, flush the mapped memory range.
                    allocator.flushAllocation(result.buffer->allocation, 0, vk::WholeSize);
                }

                return result;
            };

            std::vector<std::pair<std::size_t, std::size_t>> orderedTexcoordAccessors;
            std::vector<std::pair<std::size_t, std::size_t>> orderedColorAccessors;
            std::vector<std::pair<std::size_t, std::size_t>> orderedJointsAccessors;
            std::vector<std::pair<std::size_t, std::size_t>> orderedWeightsAccessors;
            for (const auto &[attributeName, accessorIndex] : primitive.attributes) {
                if (attributeName == "POSITION"sv) {
                    position.attributeInfo = getAttributeInfo(accessorIndex);
                }
                else if (attributeName == "NORMAL"sv) {
                    normal.emplace(getAttributeInfo(accessorIndex));
                }
                else if (attributeName == "TANGENT"sv) {
                    tangent.emplace(getAttributeInfo(accessorIndex));
                }
                else if (attributeName.starts_with("TEXCOORD")) {
                    if (std::size_t attributeIndex = parseIndex(attributeName, 9); attributeIndex < config.maxTexcoordAttributeCount) {
                        orderedTexcoordAccessors.emplace_back(attributeIndex, accessorIndex);
                    }
                }
                else if (attributeName.starts_with("COLOR")) {
                    if (std::size_t attributeIndex = parseIndex(attributeName, 6); attributeIndex < config.maxColorAttributeCount) {
                        orderedColorAccessors.emplace_back(attributeIndex, accessorIndex);
                    }
                }
                else if (attributeName.starts_with("JOINTS")) {
                    if (std::size_t attributeIndex = parseIndex(attributeName, 7); attributeIndex < config.maxJointsAttributeCount) {
                        orderedJointsAccessors.emplace_back(attributeIndex, accessorIndex);
                    }
                }
                else if (attributeName.starts_with("WEIGHTS")) {
                    if (std::size_t attributeIndex = parseIndex(attributeName, 8); attributeIndex < config.maxWeightsAttributeCount) {
                        orderedWeightsAccessors.emplace_back(attributeIndex, accessorIndex);
                    }
                }
            }

            std::ranges::sort(orderedTexcoordAccessors);
            for (std::size_t accessorIndex : orderedTexcoordAccessors | std::views::values) {
                texcoords.emplace_back(getAttributeInfo(accessorIndex));
            }

            std::ranges::sort(orderedColorAccessors);
            for (std::size_t accessorIndex : orderedColorAccessors | std::views::values) {
                colors.emplace_back(getAttributeInfo(accessorIndex));
            }

            std::ranges::sort(orderedJointsAccessors);
            for (std::size_t accessorIndex : orderedJointsAccessors | std::views::values) {
                joints.emplace_back(getAttributeInfo(accessorIndex));
            }

            std::ranges::sort(orderedWeightsAccessors);
            for (std::size_t accessorIndex : orderedWeightsAccessors | std::views::values) {
                weights.emplace_back(getAttributeInfo(accessorIndex));
            }

            if (tangent && !normal) {
                // glTF 2.0 specification:
                //   When normals are not specified, client implementations MUST calculate flat normals and the provided
                //   tangents (if present) MUST be ignored.
                //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
                tangent.reset();
            }
        #ifdef USE_MIKKTSPACE
            else if (config.mikkTSpaceTangentComponentType && needMikkTSpaceTangents()) {
                emplaceMikkTSpaceTangents(*config.mikkTSpaceTangentComponentType, allocator, config.usageFlags, {}, config.allocationCreateInfo, config.adapter);
            }
        #endif

            // Morph targets.
            for (const auto &attributes : primitive.targets) {
                for (const auto &[attributeName, accessorIndex] : attributes) {
                    if (attributeName == "POSITION"sv) {
                        position.morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                    else if (attributeName == "NORMAL"sv) {
                        // glTF 2.0 specification:
                        //   For each morph target attribute, an original attribute MUST be present in the mesh primitive.
                        //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#morph-targets
                        // Valueless doesn't have to be handled.
                        normal.value().morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                    else if (attributeName == "TANGENT"sv) {
                        tangent.value().morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                    else if (attributeName.starts_with("TEXCOORD")) {
                        if (std::size_t attributeIndex = parseIndex(attributeName, 9); attributeIndex < config.maxTexcoordAttributeCount) {
                            texcoords.at(attributeIndex).morphTargets.push_back(getAttributeInfo(accessorIndex));
                        }
                    }
                    else if (attributeName.starts_with("COLOR")) {
                        if (std::size_t attributeIndex = parseIndex(attributeName, 6); attributeIndex < config.maxColorAttributeCount) {
                            colors.at(attributeIndex).morphTargets.push_back(getAttributeInfo(accessorIndex));
                        }
                    }
                }
            }
        }

        [[nodiscard]] bool needMikkTSpaceTangents() const noexcept;

    #ifdef USE_MIKKTSPACE
        /**
         * @brief Calculate MikkTSpace tangents for the primitive, and store them in <tt>tangent</tt> attribute.
         * @tparam BufferDataAdapter A functor type that returns the bytes span from a glTF buffer view.
         * @param componentType Component type of the generated tangents. Must be either <tt>Float</tt>, <tt>Short</tt> or <tt>Byte</tt>.
         * @param allocator Vulkan memory allocator.
         * @param usageFlags Vulkan buffer usage flags for the buffer.
         * @param allocationCreateInfo VMA allocation creation flags for the buffer.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        void emplaceMikkTSpaceTangents(
            fastgltf::ComponentType componentType,
            vma::Allocator allocator,
            vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eVertexBuffer,
            vk::ArrayProxy<const std::uint32_t> queueFamilies = {},
            const vma::AllocationCreateInfo &allocationCreateInfo = vma::AllocationCreateInfo {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                vma::MemoryUsage::eAutoPreferHost,
            },
            const BufferDataAdapter &adapter = {}
        ) {
            std::variant<std::vector<fastgltf::math::fvec4>, std::vector<fastgltf::math::s16vec4>, std::vector<fastgltf::math::s8vec4>> generated;
            switch (componentType) {
                case fastgltf::ComponentType::Float:
                    generated = createMikkTSpaceTangents<float>(asset, primitive, adapter);
                    break;
                case fastgltf::ComponentType::Short:
                    generated = createMikkTSpaceTangents<std::int16_t>(asset, primitive, adapter);
                    break;
                case fastgltf::ComponentType::Byte:
                    generated = createMikkTSpaceTangents<std::int8_t>(asset, primitive, adapter);
                    break;
                default:
                    throw std::runtime_error { "Unsupported MikkTSpace tangent component type: only FLOAT, SHORT normalized and BYTE normalized are supported." };
            }

            // Erase type.
            const std::span generatedBytes = visit([](const auto &generated) { return as_bytes(std::span { generated }); }, generated);

            tangent.emplace().attributeInfo = {
                .buffer = std::make_shared<vku::AllocatedBuffer>(
                    allocator,
                    vk::BufferCreateInfo {
                        {},
                        generatedBytes.size_bytes(),
                        usageFlags,
                        queueFamilies.size() < 2 ? vk::SharingMode::eExclusive : vk::SharingMode::eConcurrent,
                        queueFamilies,
                    },
                    allocationCreateInfo),
                .size = generatedBytes.size_bytes(),
                .stride = 4 * getComponentByteSize(componentType),
                .componentType = componentType,
                .normalized = componentType != fastgltf::ComponentType::Float,
            };
            allocator.copyMemoryToAllocation(generatedBytes.data(), tangent->attributeInfo.buffer->allocation, 0, tangent->attributeInfo.size);
        }
    #endif

    private:
        std::reference_wrapper<const fastgltf::Asset> asset;
        std::reference_wrapper<const fastgltf::Primitive> primitive;
    };

    export template <>
    class PrimitiveAttributeBuffers::AttributeInfoCache::Config<fastgltf::DefaultBufferDataAdapter> {
        static constexpr fastgltf::DefaultBufferDataAdapter adapter;

        friend class AttributeInfoCache;

    public:
        std::size_t maxTexcoordAttributeCount = 2;
        std::size_t maxColorAttributeCount = 1;
        std::size_t maxJointsAttributeCount = 1;
        std::size_t maxWeightsAttributeCount = 1;

        std::function<std::optional<vk::BufferUsageFlags>(const fastgltf::Accessor&)> usageFlagsFn
            = [](const fastgltf::Accessor &accessor) -> std::optional<vk::BufferUsageFlags> {
                if (accessor.sparse) return std::nullopt;
                return vk::BufferUsageFlagBits::eVertexBuffer;
            };
        vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};
        vma::AllocationCreateInfo allocationCreateInfo = {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
            vma::MemoryUsage::eAutoPreferHost,
        };
    };

    export template <>
    class PrimitiveAttributeBuffers::Config<fastgltf::DefaultBufferDataAdapter> {
        static constexpr fastgltf::DefaultBufferDataAdapter adapter;

        friend class PrimitiveAttributeBuffers;

    public:
        const AttributeInfoCache *cache = nullptr;
        std::size_t maxTexcoordAttributeCount = 2;
        std::size_t maxColorAttributeCount = 1;
        std::size_t maxJointsAttributeCount = 1;
        std::size_t maxWeightsAttributeCount = 1;
    #ifdef USE_MIKKTSPACE
        std::optional<fastgltf::ComponentType> mikkTSpaceTangentComponentType = fastgltf::ComponentType::Float;
    #endif
        std::function<fastgltf::ComponentType(const fastgltf::Accessor&)> componentTypeFn = &fastgltf::Accessor::componentType;
        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eVertexBuffer;
        vk::ArrayProxyNoTemporaries<const std::uint32_t> queueFamilies = {};
        vma::AllocationCreateInfo allocationCreateInfo = {
            vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
            vma::MemoryUsage::eAutoPreferHost,
        };
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

ComponentCountVariant getComponentCountVariant(fastgltf::AccessorType type) noexcept {
    switch (type) {
        case fastgltf::AccessorType::Scalar:
            return std::integral_constant<int, 1>{};
        case fastgltf::AccessorType::Vec2:
            return std::integral_constant<int, 2>{};
        case fastgltf::AccessorType::Vec3:
            return std::integral_constant<int, 3>{};
        case fastgltf::AccessorType::Vec4:
            return std::integral_constant<int, 4>{};
        default:
            std::unreachable();
    }
}

ComponentTypeVariant getComponentTypeVariant(fastgltf::ComponentType componentType) noexcept {
    switch (componentType) {
        case fastgltf::ComponentType::Byte:
            return std::type_identity<std::int8_t>{};
        case fastgltf::ComponentType::UnsignedByte:
            return std::type_identity<std::uint8_t>{};
        case fastgltf::ComponentType::Short:
            return std::type_identity<std::int16_t>{};
        case fastgltf::ComponentType::UnsignedShort:
            return std::type_identity<std::uint16_t>{};
        case fastgltf::ComponentType::UnsignedInt:
            return std::type_identity<std::uint32_t>{};
        case fastgltf::ComponentType::Float:
            return std::type_identity<float>{};
        default:
            std::unreachable();
    }
}

std::size_t parseIndex(std::string_view attributeName, std::size_t prefixSize) {
    if (std::size_t value; std::from_chars(&attributeName[prefixSize], attributeName.data() + attributeName.size(), value).ec == std::errc{}) {
        return value;
    }

    // glTF 2.0 specification:
    //   TEXCOORD_n, COLOR_n, JOINTS_n, and WEIGHTS_n attribute semantic property names MUST be of the
    //   form [semantic]_[set_index], e.g., TEXCOORD_0, TEXCOORD_1, COLOR_0. All indices for indexed
    //   attribute semantics MUST start with 0 and be consecutive positive integers: TEXCOORD_0,
    //   TEXCOORD_1, etc. Indices MUST NOT use leading zeroes to pad the number of digits (e.g.,
    //   TEXCOORD_01 is not allowed).
    throw std::invalid_argument { "Invalid indexed attribute name format" };
}

vkgltf::PrimitiveAttributeBuffers::AttributeInfo vkgltf::PrimitiveAttributeBuffers::AttributeInfoCache::createAttributeInfoFromBufferViewCache(
    const fastgltf::Asset &asset,
    const fastgltf::Accessor &accessor
) const {
    const std::size_t bufferViewIndex = *accessor.bufferViewIndex;
    const fastgltf::BufferView &bufferView = asset.bufferViews[bufferViewIndex];
    const vk::DeviceSize elementByteSize = getElementByteSize(accessor.type, accessor.componentType);
    const vk::DeviceSize stride = bufferView.byteStride.value_or(elementByteSize);
    return {
        .buffer = bufferViewBuffers.at(bufferViewIndex),
        .offset = accessor.byteOffset,
        .size = stride * (accessor.count - 1) + elementByteSize,
        .stride = stride,
        .componentType = accessor.componentType,
        .normalized = accessor.normalized,
    };
}

bool vkgltf::PrimitiveAttributeBuffers::needMikkTSpaceTangents() const noexcept {
    if (tangent || !primitive.get().indicesAccessor || !normal || !primitive.get().materialIndex) {
        return false;
    }

    const fastgltf::Material &material = asset.get().materials[*primitive.get().materialIndex];
    if (!material.normalTexture) {
        return false;
    }

    // Check if any POSITION, NORMAL or TEXCOORD_<i> (where <i> is the texture coordinate index of the normal texture)
    // attributes are referenced in morph targets.
    const std::size_t normalTextureTexcoordIndex = getTexcoordIndex(*material.normalTexture);
    for (const auto &morphTargets : primitive.get().targets) {
        for (const auto &[attributeName, _] : morphTargets) {
            if (attributeName == "POSITION"sv || attributeName == "NORMAL"sv) {
                return false;
            }
            if (attributeName.starts_with("TEXCOORD_")) {
                if (parseIndex(attributeName, 9) == normalTextureTexcoordIndex) {
                    return false;
                }
            }
        }
    }

    return true;
}