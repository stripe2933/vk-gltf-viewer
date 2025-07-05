module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vkgltf.PrimitiveAttributeBuffers;

import std;
export import fastgltf;
export import vku;

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

[[nodiscard]] std::optional<std::size_t> parse(std::string_view str) noexcept;

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
             * @brief Offset in the Vulkan buffer where the attribute data starts.
             */
            vk::DeviceSize offset;

            /**
             * @brief Size of the attribute data in the Vulkan buffer.
             */
            vk::DeviceSize size;

            /**
             * @brief Stride of the attribute data in the Vulkan buffer. Always non-zero.
             */
            vk::DeviceSize stride;
        };

        class AttributeInfoCache {
        public:
            template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
            class Config {
            public:
                const BufferDataAdapter &adapter;

                std::function<std::optional<vk::BufferUsageFlags>(const fastgltf::Accessor&)> usageFlagsFn
                    = [](const fastgltf::Accessor &accessor) -> std::optional<vk::BufferUsageFlags> {
                        if (accessor.sparse || !accessor.bufferViewIndex) {
                            return std::nullopt;
                        }
                        return vk::BufferUsageFlagBits::eVertexBuffer;
                    };

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
             * @note \p usageFlagsFn MUST return <tt>std::nullopt</tt> for accessors that don't have a buffer view.
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
                            if (attributeName == "POSITION"sv ||
                                attributeName == "NORMAL"sv ||
                                attributeName == "TANGENT"sv ||
                                attributeName.starts_with("TEXCOORD_"sv) ||
                                attributeName.starts_with("COLOR_"sv) ||
                                attributeName.starts_with("JOINTS_"sv) ||
                                attributeName.starts_with("WEIGHTS_"sv)) {
                                const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                                if (auto usage = std::invoke(config.usageFlagsFn, accessor)) {
                                    bufferViewUsages[accessor.bufferViewIndex.value()] |= *usage;
                                    accessors.emplace(accessorIndex);
                                }
                            }
                        }

                        for (const auto &attributes : primitive.targets) {
                            for (const auto &[attributeName, accessorIndex] : attributes) {
                                const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                                if (attributeName == "POSITION"sv ||
                                    attributeName == "NORMAL"sv ||
                                    attributeName == "TANGENT"sv ||
                                    attributeName.starts_with("TEXCOORD_"sv) ||
                                    attributeName.starts_with("COLOR_"sv)) {
                                    if (auto usage = std::invoke(config.usageFlagsFn, accessor)) {
                                        bufferViewUsages[accessor.bufferViewIndex.value()] |= *usage;
                                        accessors.emplace(accessorIndex);
                                    }
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
                            vk::BufferCreateInfo { {}, bufferViewData.size(), usage },
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

        struct AttributeInfoWithMorphTargets {
            AttributeInfo attributeInfo;
            std::vector<AttributeInfo> morphTargets;
        };

        template <typename BufferDataAdapter = fastgltf::DefaultBufferDataAdapter>
        class Config {
        public:
            const BufferDataAdapter &adapter;

            /**
             * @brief A cache that contains Vulkan buffers that store either glTF buffer view data/accessor data.
             *
             * This can be used across multiple <tt>PrimitiveAttributeBuffers</tt> instances to avoid creating redundant Vulkan buffers.
             */
            AttributeInfoCache *cache;

            /**
             * @brief Vulkan buffer usage flags for the buffer creation.
             */
            vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eVertexBuffer;

            vma::AllocationCreateInfo allocationCreateInfo {
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
        ) {
            const auto getAttributeInfo = [&](std::size_t accessorIndex) -> AttributeInfo {
                const fastgltf::Accessor &accessor = asset.accessors[accessorIndex];
                if (!config.cache) {
                    // Cache is not provided. Create a new dedicated accessor attribute info.
                    return createAccessorAttributeInfo(asset, accessor, allocator, config.usageFlags, config.allocationCreateInfo, config.adapter);
                }

                auto it = config.cache->accessorAttributeInfos.find(accessorIndex);
                if (it == config.cache->accessorAttributeInfos.end()) {
                    // Accessor buffer view data is incompatible with Vulkan buffer. Generate a dedicated one.
                    it = config.cache->accessorAttributeInfos.emplace_hint(
                        it,
                        accessorIndex,
                        createAccessorAttributeInfo(asset, accessor, allocator, config.usageFlags, config.allocationCreateInfo, config.adapter));
                }

                return it->second;
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
                else if (attributeName.starts_with("TEXCOORD_"sv)) {
                    // glTF 2.0 specification:
                    //   TEXCOORD_n, COLOR_n, JOINTS_n, and WEIGHTS_n attribute semantic property names MUST be of the
                    //   form [semantic]_[set_index], e.g., TEXCOORD_0, TEXCOORD_1, COLOR_0. All indices for indexed
                    //   attribute semantics MUST start with 0 and be consecutive positive integers: TEXCOORD_0,
                    //   TEXCOORD_1, etc. Indices MUST NOT use leading zeroes to pad the number of digits (e.g.,
                    //   TEXCOORD_01 is not allowed).
                    //
                    // Parse failure doesn't have to be handled.
                    orderedTexcoordAccessors.emplace_back(parse(std::string_view { attributeName }.substr(9)).value(), accessorIndex);
                }
                else if (attributeName.starts_with("COLOR_"sv)) {
                    orderedColorAccessors.emplace_back(parse(std::string_view { attributeName }.substr(6)).value(), accessorIndex);
                }
                else if (attributeName.starts_with("JOINTS_"sv)) {
                    orderedJointsAccessors.emplace_back(parse(std::string_view { attributeName }.substr(7)).value(), accessorIndex);
                }
                else if (attributeName.starts_with("WEIGHTS_"sv)) {
                    orderedWeightsAccessors.emplace_back(parse(std::string_view { attributeName }.substr(8)).value(), accessorIndex);
                }
            }

            std::ranges::sort(orderedTexcoordAccessors);
            for (std::size_t accessorIndex: orderedTexcoordAccessors | std::views::values) {
                texcoords.emplace_back(getAttributeInfo(accessorIndex));
            }

            std::ranges::sort(orderedColorAccessors);
            for (std::size_t accessorIndex: orderedColorAccessors | std::views::values) {
                colors.emplace_back(getAttributeInfo(accessorIndex));
            }

            std::ranges::sort(orderedJointsAccessors);
            for (std::size_t accessorIndex: orderedJointsAccessors | std::views::values) {
                joints.emplace_back(getAttributeInfo(accessorIndex));
            }

            std::ranges::sort(orderedWeightsAccessors);
            for (std::size_t accessorIndex: orderedWeightsAccessors | std::views::values) {
                weights.emplace_back(getAttributeInfo(accessorIndex));
            }

            // glTF 2.0 specification:
            //   When normals are not specified, client implementations MUST calculate flat normals and the provided
            //   tangents (if present) MUST be ignored.
            //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
            if (tangent && !normal) {
                tangent.reset();
            }

            // Morph targets.
            for (const auto &attributes : primitive.targets) {
                for (const auto &[attributeName, accessorIndex] : attributes) {
                    if (attributeName == "POSITION"sv) {
                        position.morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                    else if (attributeName == "NORMAL"sv) {
                        normal.value().morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                    else if (attributeName == "TANGENT"sv) {
                        tangent.value().morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                    else if (attributeName.starts_with("TEXCOORD_"sv)) {
                        texcoords.at(parse(std::string_view { attributeName }.substr(9)).value()).morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                    else if (attributeName.starts_with("COLOR_"sv)) {
                        colors.at(parse(std::string_view { attributeName }.substr(6)).value()).morphTargets.push_back(getAttributeInfo(accessorIndex));
                    }
                }
            }
        }

    private:
        /**
         * @brief Create dedicated <tt>AttributeInfo</tt> for \p accessor, whose <tt>buffer</tt> has packed data of \p accessor.
         * @tparam BufferDataAdapter A functor type that return the bytes span from a glTF buffer view.
         * @param asset glTF asset.
         * @param accessor glTF accessor from \p asset.
         * @param allocator VMA allocator.
         * @param usageFlags Vulkan buffer usage.
         * @param allocationCreateInfo Vulkan buffer allocation create info.
         * @param adapter Buffer data adapter.
         */
        template <typename BufferDataAdapter>
        [[nodiscard]] static AttributeInfo createAccessorAttributeInfo(
            const fastgltf::Asset &asset,
            const fastgltf::Accessor &accessor,
            vma::Allocator allocator,
            vk::BufferUsageFlags usageFlags,
            const vma::AllocationCreateInfo &allocationCreateInfo,
            const BufferDataAdapter &adapter
        ) {
            const std::size_t elementByteSize = getElementByteSize(accessor.type, accessor.componentType);

            // glTF 2.0 specification:
            //   For performance and compatibility reasons, each element of a vertex attribute MUST be aligned to 4-byte
            //   boundaries inside a bufferView (i.e., accessor.byteOffset and bufferView.byteStride MUST be multiples
            //   of 4).
            //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#data-alignment
            const vk::DeviceSize byteStride = (elementByteSize / 4 + (elementByteSize % 4 != 0)) * 4;

            AttributeInfo result {
                .buffer = std::make_shared<vku::AllocatedBuffer>(
                    allocator,
                    vk::BufferCreateInfo { {}, byteStride * accessor.count, usageFlags },
                    allocationCreateInfo),
                .offset = 0,
                .size = byteStride * (accessor.count - 1) + elementByteSize,
                .stride = byteStride,
            };

            // Map memory, copy data from the accessor, unmap memory.
            void* const mapped = allocator.mapMemory(result.buffer->allocation);
            visit([&]<typename T>(auto N, std::type_identity<T>) {
                using ElementType = std::conditional_t<N == 1, T, fastgltf::math::vec<T, N>>;
                constexpr std::size_t Stride = (sizeof(ElementType) / 4 + (sizeof(ElementType) % 4 != 0)) * 4;
                copyFromAccessor<ElementType, Stride>(asset, accessor, mapped, adapter);
            }, getComponentCountVariant(accessor.type), getComponentTypeVariant(accessor.componentType));
            allocator.unmapMemory(result.buffer->allocation);

            const vk::MemoryPropertyFlags memoryProps = allocator.getAllocationMemoryProperties(result.buffer->allocation);
            if (!vku::contains(memoryProps, vk::MemoryPropertyFlagBits::eHostCoherent)) {
                // Created buffer is non-coherent, flush the mapped memory range.
                allocator.flushAllocation(result.buffer->allocation, 0, vk::WholeSize);
            }

            return result;
        }
    };

    export template <>
    class PrimitiveAttributeBuffers::AttributeInfoCache::Config<fastgltf::DefaultBufferDataAdapter> {
        static constexpr fastgltf::DefaultBufferDataAdapter adapter;

        friend class AttributeInfoCache;

    public:
        std::function<std::optional<vk::BufferUsageFlags>(const fastgltf::Accessor&)> usageFlagsFn
            = [](const fastgltf::Accessor &accessor) -> std::optional<vk::BufferUsageFlags> {
                if (accessor.sparse || !accessor.bufferViewIndex) {
                    return std::nullopt;
                }
                return vk::BufferUsageFlagBits::eVertexBuffer;
        };

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
        /**
         * @brief A cache that contains Vulkan buffers that store either glTF buffer view data/accessor data.
         *
         * This can be used across multiple <tt>PrimitiveAttributeBuffers</tt> instances to avoid creating redundant Vulkan buffers.
         */
        AttributeInfoCache *cache;

        vk::BufferUsageFlags usageFlags = vk::BufferUsageFlagBits::eVertexBuffer;

        vma::AllocationCreateInfo allocationCreateInfo {
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

std::optional<std::size_t> parse(std::string_view str) noexcept {
    if (std::size_t value; std::from_chars(str.data(), str.data() + str.size(), value).ec == std::errc{}) {
        return value;
    }
    return std::nullopt;
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
    };
}