#pragma once

#ifndef VKU_USE_MODULE
#include <cassert>
#include <cstdint>
#include <algorithm>
#include <array>
#include <bit>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <numeric>
#include <ranges>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include <vulkan/vulkan_raii.hpp>
#include <vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>
#endif

#ifdef VKU_USE_MODULE
#define VKU_EXPORT export
#else
#define VKU_EXPORT
#endif

#define VKU_INDEX_SEQ(Is, N, ...) [&]<auto ...Is>(std::index_sequence<Is...>) __VA_ARGS__ (std::make_index_sequence<N>{})

#if defined(__clang__)
#define VKU_LIFETIMEBOUND [[clang::lifetimebound]]
#elif defined(_MSC_VER)
#define VKU_LIFETIMEBOUND [[msvc::lifetimebound]]
#else
#define VKU_LIFETIMEBOUND
#endif

namespace vku {
namespace details {
    // https://stackoverflow.com/questions/70130735/c-concept-to-check-for-derived-from-template-specialization
    template <template <auto...> typename Template, auto... Args>
    void derived_from_value_specialization_impl(const Template<Args...>&);

    template <typename T, template <auto...> class Template>
    concept derived_from_value_specialization_of = requires(const T& t) {
        derived_from_value_specialization_impl<Template>(t);
    };

    [[nodiscard]] VULKAN_HPP_NAMESPACE::ImageAspectFlags getAspectFlags(VULKAN_HPP_NAMESPACE::Format format);

    template <typename>
    struct DescriptorTypeTraits;

    template <>
    struct DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorBufferInfo> {
        using type = VULKAN_HPP_NAMESPACE::DescriptorBufferInfo;
        static constexpr auto dataGetter = &VULKAN_HPP_NAMESPACE::WriteDescriptorSet::pBufferInfo;
    };

    template <>
    struct DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> {
        using type = VULKAN_HPP_NAMESPACE::DescriptorImageInfo;
        static constexpr auto dataGetter = &VULKAN_HPP_NAMESPACE::WriteDescriptorSet::pImageInfo;
    };

    template <>
    struct DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::BufferView> {
        using type = VULKAN_HPP_NAMESPACE::BufferView;
        static constexpr auto dataGetter = &VULKAN_HPP_NAMESPACE::WriteDescriptorSet::pTexelBufferView;
    };

    template <VULKAN_HPP_NAMESPACE::DescriptorType> struct WriteDescriptorSetTraits;

    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eUniformBuffer>         : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorBufferInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eStorageBuffer>         : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorBufferInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eUniformBufferDynamic>  : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorBufferInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eStorageBufferDynamic>  : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorBufferInfo> { };

    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eSampler>               : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eCombinedImageSampler>  : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eSampledImage>          : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eStorageImage>          : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eInputAttachment>       : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eSampleWeightImageQCOM> : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eBlockMatchImageQCOM>   : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::DescriptorImageInfo> { };

    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eUniformTexelBuffer>    : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::BufferView> { };
    template <> struct WriteDescriptorSetTraits<VULKAN_HPP_NAMESPACE::DescriptorType::eStorageTexelBuffer>    : DescriptorTypeTraits<VULKAN_HPP_NAMESPACE::BufferView> { };
}

    VKU_EXPORT template <typename T>
    [[nodiscard]] const T &lvalue(const T &&rvalue VKU_LIFETIMEBOUND) noexcept {
        return rvalue;
    }

    VKU_EXPORT template <typename T>
    [[nodiscard]] const std::initializer_list<T> &lvalue(const std::initializer_list<T> &&rvalue VKU_LIFETIMEBOUND) noexcept {
        return rvalue;
    }

    VKU_EXPORT template <typename T>
        requires (VULKAN_HPP_NAMESPACE::FlagTraits<T>::isBitmask)
    [[nodiscard]] constexpr bool contains(VULKAN_HPP_NAMESPACE::Flags<T> flags, T bit) noexcept {
        return static_cast<bool>(flags & bit);
    }

    VKU_EXPORT template <typename T>
        requires (VULKAN_HPP_NAMESPACE::FlagTraits<T>::isBitmask)
    [[nodiscard]] constexpr bool contains(VULKAN_HPP_NAMESPACE::Flags<T> super, VULKAN_HPP_NAMESPACE::Flags<T> sub) noexcept {
        return (super & sub) == sub;
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Extent2D toExtent2D(const VULKAN_HPP_NAMESPACE::Extent3D &extent) noexcept {
        return { extent.width, extent.height };
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Extent2D toExtent2D(const VULKAN_HPP_NAMESPACE::Offset2D &offset) noexcept {
        return { static_cast<std::uint32_t>(offset.x), static_cast<std::uint32_t>(offset.y) };
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Offset2D toOffset2D(const VULKAN_HPP_NAMESPACE::Offset3D &extent) noexcept {
        return { extent.x, extent.y };
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Offset2D toOffset2D(const VULKAN_HPP_NAMESPACE::Extent2D &extent) noexcept {
        return { static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height) };
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Extent3D toExtent3D(const VULKAN_HPP_NAMESPACE::Offset3D &offset) noexcept {
        return { static_cast<std::uint32_t>(offset.x), static_cast<std::uint32_t>(offset.y), static_cast<std::uint32_t>(offset.z) };
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Offset3D toOffset3D(const VULKAN_HPP_NAMESPACE::Extent3D &extent) noexcept {
        return { static_cast<std::int32_t>(extent.width), static_cast<std::int32_t>(extent.height), static_cast<std::int32_t>(extent.depth) };
    }

    VKU_EXPORT
    [[nodiscard]] constexpr float aspect(const VULKAN_HPP_NAMESPACE::Extent2D &extent) noexcept {
        return static_cast<float>(extent.width) / extent.height;
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Extent2D mipExtent(VULKAN_HPP_NAMESPACE::Extent2D extent, std::uint32_t level) noexcept {
        extent.width = std::max(extent.width >> level, 1U);
        extent.height = std::max(extent.height >> level, 1U);
        return extent;
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Extent3D mipExtent(VULKAN_HPP_NAMESPACE::Extent3D extent, std::uint32_t level) noexcept {
        extent.width = std::max(extent.width >> level, 1U);
        extent.height = std::max(extent.height >> level, 1U);
        extent.depth = std::max(extent.depth >> level, 1U);
        return extent;
    }

    VKU_EXPORT
    [[nodiscard]] constexpr std::uint32_t maxMipLevels(std::uint32_t size) noexcept {
        return std::bit_width(size);
    }

    VKU_EXPORT
    [[nodiscard]] constexpr std::uint32_t maxMipLevels(const VULKAN_HPP_NAMESPACE::Extent2D &extent) noexcept {
        return maxMipLevels(std::max(extent.width, extent.height));
    }

    VKU_EXPORT
    [[nodiscard]] constexpr std::uint32_t maxMipLevels(const VULKAN_HPP_NAMESPACE::Extent3D &extent) noexcept {
        return maxMipLevels(std::max({ extent.width, extent.height, extent.depth }));
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Viewport toViewport(const VULKAN_HPP_NAMESPACE::Rect2D &rect, bool negativeHeight = false) noexcept {
        if (negativeHeight) {
            return {
                static_cast<float>(rect.offset.x),
                static_cast<float>(rect.offset.y + rect.extent.height),
                static_cast<float>(rect.extent.width),
                -static_cast<float>(rect.extent.height),
                0.f,
                1.f,
            };
        }
        else {
            return {
                static_cast<float>(rect.offset.x),
                static_cast<float>(rect.offset.y),
                static_cast<float>(rect.extent.width),
                static_cast<float>(rect.extent.height),
                0.f,
                1.f,
            };
        }
    }

    VKU_EXPORT template <typename T>
    [[nodiscard]] constexpr typename T::CType toCType(T handle) noexcept {
        return static_cast<typename T::CType>(handle);
    }

    VKU_EXPORT template <typename T>
    [[nodiscard]] constexpr std::uint64_t toUint64(T handle) noexcept {
        return reinterpret_cast<std::uint64_t>(toCType(handle));
    }

    VKU_EXPORT template <typename T>
    [[nodiscard]] VULKAN_HPP_NAMESPACE::DebugUtilsObjectNameInfoEXT getDebugUtilsObjectNameInfoEXT(
        T handle,
        const char *name VKU_LIFETIMEBOUND
    ) noexcept {
        return { T::objectType, toUint64(handle), name };
    }

    VKU_EXPORT template <typename T>
    [[nodiscard]] VULKAN_HPP_NAMESPACE::DebugUtilsObjectTagInfoEXT getDebugUtilsObjectTagInfoEXT(
        T handle,
        std::uint64_t tagName,
        std::uint32_t tagSize,
        const void *pTag VKU_LIFETIMEBOUND
    ) noexcept {
        return { T::objectType, toUint64(handle), tagName, tagSize, pTag };
    }

#ifndef VULKAN_HPP_DISABLE_ENHANCED_MODE
    VKU_EXPORT template <typename T>
    [[nodiscard]] VULKAN_HPP_NAMESPACE::DebugUtilsObjectTagInfoEXT getDebugUtilsObjectTagInfoEXT(
        T handle,
        std::uint64_t tagName,
        const VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<const std::byte> &tag VKU_LIFETIMEBOUND
    ) noexcept {
        return getDebugUtilsObjectTagInfoEXT(handle, tagName, tag.size(), tag.data());
    }
#endif

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::ImageSubresourceRange fullSubresourceRange(
        VULKAN_HPP_NAMESPACE::ImageAspectFlags flags
    ) noexcept {
        return { flags, 0, VULKAN_HPP_NAMESPACE::RemainingMipLevels, 0, VULKAN_HPP_NAMESPACE::RemainingArrayLayers };
    }

    VKU_EXPORT
    [[nodiscard]] constexpr bool isSrgb(VULKAN_HPP_NAMESPACE::Format format) noexcept {
        switch (format) {
            case VULKAN_HPP_NAMESPACE::Format::eR8Srgb:
            case VULKAN_HPP_NAMESPACE::Format::eR8G8Srgb:
            case VULKAN_HPP_NAMESPACE::Format::eR8G8B8Srgb:
            case VULKAN_HPP_NAMESPACE::Format::eB8G8R8Srgb:
            case VULKAN_HPP_NAMESPACE::Format::eR8G8B8A8Srgb:
            case VULKAN_HPP_NAMESPACE::Format::eB8G8R8A8Srgb:
            case VULKAN_HPP_NAMESPACE::Format::eA8B8G8R8SrgbPack32:
            case VULKAN_HPP_NAMESPACE::Format::eBc1RgbSrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eBc1RgbaSrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eBc2SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eBc3SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eBc7SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eEtc2R8G8B8SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eEtc2R8G8B8A1SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eEtc2R8G8B8A8SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc4x4SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc5x4SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc5x5SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc6x5SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc6x6SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc8x5SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc8x6SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc8x8SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc10x5SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc10x6SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc10x8SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc10x10SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc12x10SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::eAstc12x12SrgbBlock:
            case VULKAN_HPP_NAMESPACE::Format::ePvrtc12BppSrgbBlockIMG:
            case VULKAN_HPP_NAMESPACE::Format::ePvrtc14BppSrgbBlockIMG:
            case VULKAN_HPP_NAMESPACE::Format::ePvrtc22BppSrgbBlockIMG:
            case VULKAN_HPP_NAMESPACE::Format::ePvrtc24BppSrgbBlockIMG:
                return true;
            default:
                return false;
        }
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::Format toggleSrgb(VULKAN_HPP_NAMESPACE::Format format) noexcept {
        switch (format) {
            #define VKU_BIMAP(x, y) \
                case VULKAN_HPP_NAMESPACE::Format::x: return VULKAN_HPP_NAMESPACE::Format::y; \
                case VULKAN_HPP_NAMESPACE::Format::y: return VULKAN_HPP_NAMESPACE::Format::x
            VKU_BIMAP(eR8Unorm, eR8Srgb);
            VKU_BIMAP(eR8G8Unorm, eR8G8Srgb);
            VKU_BIMAP(eR8G8B8Unorm, eR8G8B8Srgb);
            VKU_BIMAP(eB8G8R8Unorm, eB8G8R8Srgb);
            VKU_BIMAP(eR8G8B8A8Unorm, eR8G8B8A8Srgb);
            VKU_BIMAP(eB8G8R8A8Unorm, eB8G8R8A8Srgb);
            VKU_BIMAP(eA8B8G8R8UnormPack32, eA8B8G8R8SrgbPack32);
            VKU_BIMAP(eBc1RgbUnormBlock, eBc1RgbSrgbBlock);
            VKU_BIMAP(eBc1RgbaUnormBlock, eBc1RgbaSrgbBlock);
            VKU_BIMAP(eBc2UnormBlock, eBc2SrgbBlock);
            VKU_BIMAP(eBc3UnormBlock, eBc3SrgbBlock);
            VKU_BIMAP(eBc7UnormBlock, eBc7SrgbBlock);
            VKU_BIMAP(eEtc2R8G8B8UnormBlock, eEtc2R8G8B8SrgbBlock);
            VKU_BIMAP(eEtc2R8G8B8A1UnormBlock, eEtc2R8G8B8A1SrgbBlock);
            VKU_BIMAP(eEtc2R8G8B8A8UnormBlock, eEtc2R8G8B8A8SrgbBlock);
            VKU_BIMAP(eAstc4x4UnormBlock, eAstc4x4SrgbBlock);
            VKU_BIMAP(eAstc5x4UnormBlock, eAstc5x4SrgbBlock);
            VKU_BIMAP(eAstc5x5UnormBlock, eAstc5x5SrgbBlock);
            VKU_BIMAP(eAstc6x5UnormBlock, eAstc6x5SrgbBlock);
            VKU_BIMAP(eAstc6x6UnormBlock, eAstc6x6SrgbBlock);
            VKU_BIMAP(eAstc8x5UnormBlock, eAstc8x5SrgbBlock);
            VKU_BIMAP(eAstc8x6UnormBlock, eAstc8x6SrgbBlock);
            VKU_BIMAP(eAstc8x8UnormBlock, eAstc8x8SrgbBlock);
            VKU_BIMAP(eAstc10x5UnormBlock, eAstc10x5SrgbBlock);
            VKU_BIMAP(eAstc10x6UnormBlock, eAstc10x6SrgbBlock);
            VKU_BIMAP(eAstc10x8UnormBlock, eAstc10x8SrgbBlock);
            VKU_BIMAP(eAstc10x10UnormBlock, eAstc10x10SrgbBlock);
            VKU_BIMAP(eAstc12x10UnormBlock, eAstc12x10SrgbBlock);
            VKU_BIMAP(eAstc12x12UnormBlock, eAstc12x12SrgbBlock);
            VKU_BIMAP(ePvrtc12BppUnormBlockIMG, ePvrtc12BppSrgbBlockIMG);
            VKU_BIMAP(ePvrtc14BppUnormBlockIMG, ePvrtc14BppSrgbBlockIMG);
            VKU_BIMAP(ePvrtc22BppUnormBlockIMG, ePvrtc22BppSrgbBlockIMG);
            VKU_BIMAP(ePvrtc24BppUnormBlockIMG, ePvrtc24BppSrgbBlockIMG);
            #undef VKU_BIMAP
            default:
                return VULKAN_HPP_NAMESPACE::Format::eUndefined;
        }
    }

    VKU_EXPORT enum class TopologyClass : std::uint8_t {
        ePoint = static_cast<std::underlying_type_t<VULKAN_HPP_NAMESPACE::PrimitiveTopology>>(VULKAN_HPP_NAMESPACE::PrimitiveTopology::ePointList),
        eLine = static_cast<std::underlying_type_t<VULKAN_HPP_NAMESPACE::PrimitiveTopology>>(VULKAN_HPP_NAMESPACE::PrimitiveTopology::eLineList),
        eTriangle = static_cast<std::underlying_type_t<VULKAN_HPP_NAMESPACE::PrimitiveTopology>>(VULKAN_HPP_NAMESPACE::PrimitiveTopology::eTriangleList),
        ePatch = static_cast<std::underlying_type_t<VULKAN_HPP_NAMESPACE::PrimitiveTopology>>(VULKAN_HPP_NAMESPACE::PrimitiveTopology::ePatchList),
    };

    VKU_EXPORT constexpr TopologyClass getTopologyClass(VULKAN_HPP_NAMESPACE::PrimitiveTopology topology) noexcept {
        switch (topology) {
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::ePointList:
                return TopologyClass::ePoint;
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eLineList:
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eLineStrip:
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eLineListWithAdjacency:
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eLineStripWithAdjacency:
                return TopologyClass::eLine;
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eTriangleList:
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eTriangleStrip:
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eTriangleFan:
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eTriangleListWithAdjacency:
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::eTriangleStripWithAdjacency:
                return TopologyClass::eTriangle;
            case VULKAN_HPP_NAMESPACE::PrimitiveTopology::ePatchList:
                return TopologyClass::ePatch;
        }
        std::unreachable();
    }

    VKU_EXPORT constexpr VULKAN_HPP_NAMESPACE::PrimitiveTopology getListPrimitiveTopology(TopologyClass topologyClass) noexcept {
        return static_cast<VULKAN_HPP_NAMESPACE::PrimitiveTopology>(static_cast<std::underlying_type_t<TopologyClass>>(topologyClass));
    }

    VKU_EXPORT template <std::invocable<VULKAN_HPP_NAMESPACE::CommandBuffer> F, typename Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
    void executeSingleCommand(
        VULKAN_HPP_NAMESPACE::Device device,
        VULKAN_HPP_NAMESPACE::CommandPool commandPool,
        VULKAN_HPP_NAMESPACE::Queue queue,
        F &&f,
        VULKAN_HPP_NAMESPACE::Fence fence = {},
        const Dispatch &d VULKAN_HPP_DEFAULT_DISPATCHER_ASSIGNMENT
    ) {
        VULKAN_HPP_NAMESPACE::CommandBuffer commandBuffer = device.allocateCommandBuffers({
            commandPool,
            VULKAN_HPP_NAMESPACE::CommandBufferLevel::ePrimary,
            1,
        }, d).front();

        commandBuffer.begin(VULKAN_HPP_NAMESPACE::CommandBufferBeginInfo { VULKAN_HPP_NAMESPACE::CommandBufferUsageFlagBits::eOneTimeSubmit }, d);
        std::invoke(std::forward<F>(f), commandBuffer);
        commandBuffer.end(d);

        VULKAN_HPP_NAMESPACE::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        queue.submit(submitInfo, fence, d);
        if (fence) {
            std::ignore = device.waitForFences(fence, true, ~0ULL, d);
        }
    }

    VKU_EXPORT template <std::invocable<VULKAN_HPP_NAMESPACE::CommandBuffer> F>
    void executeSingleCommand(
        const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
        VULKAN_HPP_NAMESPACE::CommandPool commandPool,
        VULKAN_HPP_NAMESPACE::Queue queue,
        F &&f,
        VULKAN_HPP_NAMESPACE::Fence fence = {}
    ) {
        VULKAN_HPP_NAMESPACE::CommandBuffer commandBuffer = (*device).allocateCommandBuffers({
            commandPool,
            VULKAN_HPP_NAMESPACE::CommandBufferLevel::ePrimary,
            1,
        }, *device.getDispatcher()).front();

        commandBuffer.begin(VULKAN_HPP_NAMESPACE::CommandBufferBeginInfo { VULKAN_HPP_NAMESPACE::CommandBufferUsageFlagBits::eOneTimeSubmit }, *device.getDispatcher());
        std::invoke(std::forward<F>(f), commandBuffer);
        commandBuffer.end(*device.getDispatcher());

        VULKAN_HPP_NAMESPACE::SubmitInfo submitInfo;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        queue.submit(submitInfo, fence, *device.getDispatcher());
        if (fence) {
            std::ignore = device.waitForFences(fence, true, ~0ULL);
        }
    }

#ifndef VULKAN_HPP_DISABLE_ENHANCED_MODE
    VKU_EXPORT template <typename Dispatch = VULKAN_HPP_DEFAULT_DISPATCHER_TYPE>
    void allocateDescriptorSets(
        VULKAN_HPP_NAMESPACE::Device device,
        VULKAN_HPP_NAMESPACE::DescriptorPool descriptorPool,
        std::initializer_list<std::pair<VULKAN_HPP_NAMESPACE::DescriptorSetLayout, VULKAN_HPP_NAMESPACE::ArrayProxy<std::reference_wrapper<VULKAN_HPP_NAMESPACE::DescriptorSet>>>> pairs,
        const void *pNext = nullptr,
        const Dispatch &d VULKAN_HPP_DEFAULT_DISPATCHER_ASSIGNMENT
    ) {
        std::vector<VULKAN_HPP_NAMESPACE::DescriptorSetLayout> descriptorSetLayouts;
        for (const auto &[layout, sets] : pairs) {
            std::fill_n(back_inserter(descriptorSetLayouts), sets.size(), layout);
        }

        VULKAN_HPP_NAMESPACE::DescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.descriptorSetCount = static_cast<std::uint32_t>(descriptorSetLayouts.size());
        allocateInfo.pSetLayouts = descriptorSetLayouts.data();
        allocateInfo.pNext = pNext;

        const std::vector<VULKAN_HPP_NAMESPACE::DescriptorSet> allocatedDescriptorSets = device.allocateDescriptorSets(allocateInfo, d);
        auto it = allocatedDescriptorSets.begin();
        for (const auto &sets : pairs | std::views::values) {
            for (const auto &setRef : sets) {
                setRef.get() = *it++;
            }
        }
        assert(it == allocatedDescriptorSets.end() && "Internal error: not all descriptor sets are assigned");
    }

    VKU_EXPORT void allocateDescriptorSets(
        const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
        VULKAN_HPP_NAMESPACE::DescriptorPool descriptorPool,
        std::initializer_list<std::pair<VULKAN_HPP_NAMESPACE::DescriptorSetLayout, VULKAN_HPP_NAMESPACE::ArrayProxy<std::reference_wrapper<VULKAN_HPP_NAMESPACE::DescriptorSet>>>> pairs,
        const void *pNext = nullptr
    );
#endif

    VKU_EXPORT struct Buffer {
        VULKAN_HPP_NAMESPACE::Buffer buffer;
        VULKAN_HPP_NAMESPACE::DeviceSize size;

        [[nodiscard]] operator VULKAN_HPP_NAMESPACE::Buffer() const noexcept {
            return buffer;
        }

        [[nodiscard]] VULKAN_HPP_NAMESPACE::BufferViewCreateInfo getViewCreateInfo(
            VULKAN_HPP_NAMESPACE::Format format,
            VULKAN_HPP_NAMESPACE::DeviceSize offset = 0,
            VULKAN_HPP_NAMESPACE::DeviceSize range = VULKAN_HPP_NAMESPACE::WholeSize
        ) const noexcept {
            return { {}, buffer, format, offset, range };
        }
    };

    VKU_EXPORT struct Image {
        VULKAN_HPP_NAMESPACE::Image image;
        VULKAN_HPP_NAMESPACE::Extent3D extent;
        VULKAN_HPP_NAMESPACE::Format format;
        std::uint32_t mipLevels;
        std::uint32_t arrayLayers;

        [[nodiscard]] operator VULKAN_HPP_NAMESPACE::Image() const noexcept {
            return image;
        }

        [[nodiscard]] VULKAN_HPP_NAMESPACE::ImageSubresourceRange getSubresourceRange(
            std::uint32_t baseMipLevel = 0,
            std::uint32_t levelCount = VULKAN_HPP_NAMESPACE::RemainingMipLevels,
            std::uint32_t baseArrayLayer = 0,
            std::uint32_t layerCount = VULKAN_HPP_NAMESPACE::RemainingArrayLayers
        ) const {
            return { details::getAspectFlags(format), baseMipLevel, levelCount, baseArrayLayer, layerCount };
        }

        [[nodiscard]] VULKAN_HPP_NAMESPACE::ImageSubresourceRange getFullSubresourceRange() const {
            return getSubresourceRange();
        }

        [[nodiscard]] VULKAN_HPP_NAMESPACE::ImageViewCreateInfo getViewCreateInfo(VULKAN_HPP_NAMESPACE::ImageViewType type) const {
            return { {}, image, type, format, {}, getSubresourceRange() };
        }

        [[nodiscard]] VULKAN_HPP_NAMESPACE::ImageViewCreateInfo getViewCreateInfo(
            VULKAN_HPP_NAMESPACE::ImageViewType type,
            const VULKAN_HPP_NAMESPACE::ImageSubresourceRange &subresourceRange
        ) const noexcept {
            return { {}, image, type, format, {}, subresourceRange };
        }

        // FIXME: inline keyword is redundant as it is template function, but Clang < 21 and the latest GCC have bug
        //  for it and can be workarounded by adding the keyword. Remove the keyword when fixed.
        [[nodiscard]] inline auto getPerMipLevelViewCreateInfos(VULKAN_HPP_NAMESPACE::ImageViewType type) const {
            return std::views::iota(0U, mipLevels)
                | std::views::transform([this, type](std::uint32_t level) {
                    return getViewCreateInfo(type, getSubresourceRange(level, 1));
                });
        }

        // FIXME: inline keyword is redundant as it is template function, but Clang < 21 and the latest GCC have bug
        //  for it and can be workarounded by adding the keyword. Remove the keyword when fixed.
        [[nodiscard]] inline auto getPerArrayLayerViewCreateInfos(VULKAN_HPP_NAMESPACE::ImageViewType type) const {
            return std::views::iota(0U, arrayLayers)
                | std::views::transform([this, type](std::uint32_t layer) {
                    return getViewCreateInfo(type, getSubresourceRange(0, VULKAN_HPP_NAMESPACE::RemainingMipLevels, layer, 1));
                });
        }
    };

#ifndef VULKAN_HPP_NO_EXCEPTIONS
namespace raii {
    VKU_EXPORT struct AllocatedBuffer : Buffer {
        VMA_HPP_NAMESPACE::Allocator allocator;
        VMA_HPP_NAMESPACE::Allocation allocation;

        AllocatedBuffer(
            VMA_HPP_NAMESPACE::Allocator allocator,
            const VULKAN_HPP_NAMESPACE::BufferCreateInfo &createInfo,
            const VMA_HPP_NAMESPACE::AllocationCreateInfo &allocationCreateInfo,
            VULKAN_HPP_NAMESPACE::Optional<VMA_HPP_NAMESPACE::AllocationInfo> allocationInfo = nullptr
        );
        AllocatedBuffer(const AllocatedBuffer&) = delete;
        AllocatedBuffer(AllocatedBuffer &&src) noexcept;
        AllocatedBuffer &operator=(const AllocatedBuffer&) = delete;
        AllocatedBuffer &operator=(AllocatedBuffer &&src) noexcept;

        ~AllocatedBuffer();

        [[nodiscard]] VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Buffer createAlias(
            const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
            const VULKAN_HPP_NAMESPACE::BufferCreateInfo &createInfo,
            VULKAN_HPP_NAMESPACE::DeviceSize allocationLocalOffset = 0,
            VULKAN_HPP_NAMESPACE::Optional<const VULKAN_HPP_NAMESPACE::AllocationCallbacks> allocator = nullptr
        ) const;
    };

    VKU_EXPORT struct AllocatedImage : Image {
        VMA_HPP_NAMESPACE::Allocator allocator;
        VMA_HPP_NAMESPACE::Allocation allocation;

        AllocatedImage(
            VMA_HPP_NAMESPACE::Allocator allocator,
            const VULKAN_HPP_NAMESPACE::ImageCreateInfo &createInfo,
            const VMA_HPP_NAMESPACE::AllocationCreateInfo &allocationCreateInfo,
            VULKAN_HPP_NAMESPACE::Optional<VMA_HPP_NAMESPACE::AllocationInfo> allocationInfo = nullptr
        );
        AllocatedImage(const AllocatedImage&) = delete;
        AllocatedImage(AllocatedImage &&src) noexcept;
        AllocatedImage &operator=(const AllocatedImage&) = delete;
        AllocatedImage &operator=(AllocatedImage &&src) noexcept;

        ~AllocatedImage();

        [[nodiscard]] VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Image createAlias(
            const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
            const VULKAN_HPP_NAMESPACE::ImageCreateInfo &createInfo,
            VULKAN_HPP_NAMESPACE::DeviceSize allocationLocalOffset = 0,
            VULKAN_HPP_NAMESPACE::Optional<const VULKAN_HPP_NAMESPACE::AllocationCallbacks> allocator = nullptr
        ) const;
    };

    VKU_EXPORT template <VULKAN_HPP_NAMESPACE::DescriptorType... BindingTypes>
    struct DescriptorSetLayout : VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::DescriptorSetLayout {
        /// Number of bindings.
        static constexpr std::uint32_t bindingCount = sizeof...(BindingTypes);

        /// Binding types.
        static constexpr std::array bindingTypes = { BindingTypes... };

        /// Number of descriptors for each binding.
        std::array<std::uint32_t, bindingCount> descriptorCounts;

        DescriptorSetLayout(
            const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
            const VULKAN_HPP_NAMESPACE::DescriptorSetLayoutCreateInfo &createInfo,
            VULKAN_HPP_NAMESPACE::Optional<const VULKAN_HPP_NAMESPACE::AllocationCallbacks> allocator = nullptr
        ) : VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::DescriptorSetLayout { device, createInfo, allocator },
            descriptorCounts { VKU_INDEX_SEQ(Is, bindingCount, {
                return std::array { createInfo.pBindings[Is].descriptorCount... };
            }) } {
            assert(bindingCount == createInfo.bindingCount && "The binding count must match the template parameter count.");
            VKU_INDEX_SEQ(Is, bindingCount, {
                assert(((createInfo.pBindings[Is].descriptorType == BindingTypes) && ...) && "The descriptor types must match the template parameter.");
            });
        }

        template <std::uint32_t I>
        [[nodiscard]] static VULKAN_HPP_NAMESPACE::DescriptorSetLayoutBinding getCreateInfoBinding(
            std::uint32_t descriptorCount = {},
            VULKAN_HPP_NAMESPACE::ShaderStageFlags stageFlags = {},
            const VULKAN_HPP_NAMESPACE::Sampler *pImmutableSamplers = {}
        ) noexcept {
            return { I , get<I>(bindingTypes), descriptorCount , stageFlags , pImmutableSamplers };
        }

    #ifndef VULKAN_HPP_DISABLE_ENHANCED_MODE
        template <std::uint32_t I>
        [[nodiscard]] static VULKAN_HPP_NAMESPACE::DescriptorSetLayoutBinding getCreateInfoBinding(
            VULKAN_HPP_NAMESPACE::ShaderStageFlags stageFlags = {},
            const VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<const VULKAN_HPP_NAMESPACE::Sampler> &immutableSamplers = {}
        ) noexcept {
            return getCreateInfoBinding<I>(immutableSamplers.size(), stageFlags, immutableSamplers.data());
        }
    #endif

        template <std::uint32_t I>
        [[nodiscard]] static VULKAN_HPP_NAMESPACE::WriteDescriptorSet getWriteDescriptorSet(
            VULKAN_HPP_NAMESPACE::DescriptorSet dstSet = {},
            std::uint32_t dstArrayElement = {},
            std::uint32_t descriptorCount = {},
            const typename details::WriteDescriptorSetTraits<get<I>(bindingTypes)>::type *pDescriptorInfos = {}
        ) noexcept {
            VULKAN_HPP_NAMESPACE::WriteDescriptorSet result { dstSet, I, dstArrayElement, descriptorCount, bindingTypes[I] };
            result.*(details::WriteDescriptorSetTraits<get<I>(bindingTypes)>::dataGetter) = pDescriptorInfos;
            return result;
        }

    #ifndef VULKAN_HPP_DISABLE_ENHANCED_MODE
        template <std::uint32_t I>
        [[nodiscard]] static VULKAN_HPP_NAMESPACE::WriteDescriptorSet getWriteDescriptorSet(
            VULKAN_HPP_NAMESPACE::DescriptorSet dstSet = {},
            std::uint32_t dstArrayElement = {},
            const VULKAN_HPP_NAMESPACE::ArrayProxyNoTemporaries<const typename details::WriteDescriptorSetTraits<get<I>(bindingTypes)>::type> &descriptorInfos = {}
        ) noexcept {
            return getWriteDescriptorSet<I>(dstSet, dstArrayElement, descriptorInfos.size(), descriptorInfos.data());
        }
    #endif
    };
}
#endif

    VKU_EXPORT class DescriptorPoolSize {
    public:
        template <details::derived_from_value_specialization_of<raii::DescriptorSetLayout>... Layouts>
        explicit(sizeof...(Layouts) == 1) DescriptorPoolSize(const Layouts &...layouts) noexcept
            : setCount { 0 } {
            (add(layouts), ...);
        }

        template <VULKAN_HPP_NAMESPACE::DescriptorType... Types>
        void add(const raii::DescriptorSetLayout<Types...> &layout, std::uint32_t duplicate = 1) noexcept {
            setCount += duplicate;
            VKU_INDEX_SEQ(Is, sizeof...(Types), {
                ((void)(descriptorCounts[Types] += duplicate * layout.descriptorCounts[Is]), ...);
            });
        }

        DescriptorPoolSize &operator+=(const DescriptorPoolSize &rhs) noexcept;
        DescriptorPoolSize &operator+=(DescriptorPoolSize &&rhs) noexcept;
        [[nodiscard]] DescriptorPoolSize operator+(DescriptorPoolSize rhs) const noexcept;
        DescriptorPoolSize &operator*=(std::uint32_t rhs) noexcept;
        [[nodiscard]] DescriptorPoolSize operator*(std::uint32_t rhs) const noexcept;

        [[nodiscard]] friend DescriptorPoolSize operator*(std::uint32_t lhs, DescriptorPoolSize rhs) noexcept {
            rhs *= lhs;
            return rhs;
        }

        [[nodiscard]] std::uint32_t getSetCount() const noexcept { return setCount; }
        [[nodiscard]] std::vector<VULKAN_HPP_NAMESPACE::DescriptorPoolSize> getDescriptorPoolSizes() const;

    private:
        std::uint32_t setCount;
        std::unordered_map<vk::DescriptorType, std::uint32_t> descriptorCounts;
    };

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::PipelineInputAssemblyStateCreateInfo defaultPipelineInputAssemblyState(
        VULKAN_HPP_NAMESPACE::PrimitiveTopology topology = {},
        bool primitiveRestartEnable = {}
    ) noexcept {
        VULKAN_HPP_NAMESPACE::PipelineInputAssemblyStateCreateInfo result;
        result.topology = topology;
        result.primitiveRestartEnable = primitiveRestartEnable;
        return result;
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::PipelineRasterizationStateCreateInfo defaultPipelineRasterizationState(
        VULKAN_HPP_NAMESPACE::PolygonMode polygonMode = {},
        VULKAN_HPP_NAMESPACE::CullModeFlags cullMode = {}
    ) noexcept {
        VULKAN_HPP_NAMESPACE::PipelineRasterizationStateCreateInfo result;
        result.polygonMode = polygonMode;
        result.cullMode = cullMode;
        result.lineWidth = 1.f;
        return result;
    }

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::PipelineColorBlendAttachmentState defaultPipelineColorBlendAttachmentState() noexcept {
        VULKAN_HPP_NAMESPACE::PipelineColorBlendAttachmentState result;
        result.colorWriteMask = VULKAN_HPP_NAMESPACE::ColorComponentFlagBits::eR
                              | VULKAN_HPP_NAMESPACE::ColorComponentFlagBits::eG
                              | VULKAN_HPP_NAMESPACE::ColorComponentFlagBits::eB
                              | VULKAN_HPP_NAMESPACE::ColorComponentFlagBits::eA;
        return result;
    }

namespace details {
    // FIXME: this should be inside the defaultPipelineColorBlendState function
	// definition with static storage, but MSVC rejects it. Move it there when fixed.
    inline constexpr std::array colorBlendAttachmentStates{
        defaultPipelineColorBlendAttachmentState(),
        defaultPipelineColorBlendAttachmentState(),
        defaultPipelineColorBlendAttachmentState(),
        defaultPipelineColorBlendAttachmentState(),
        defaultPipelineColorBlendAttachmentState(),
        defaultPipelineColorBlendAttachmentState(),
        defaultPipelineColorBlendAttachmentState(),
        defaultPipelineColorBlendAttachmentState(),
    };
}

    VKU_EXPORT
    [[nodiscard]] constexpr VULKAN_HPP_NAMESPACE::PipelineColorBlendStateCreateInfo defaultPipelineColorBlendState(std::uint32_t attachmentCount = 0) noexcept {
        assert(attachmentCount <= details::colorBlendAttachmentStates.size());

        VULKAN_HPP_NAMESPACE::PipelineColorBlendStateCreateInfo result;
        result.attachmentCount = attachmentCount;
        result.pAttachments = details::colorBlendAttachmentStates.data();
        result.blendConstants[0] = 1.f;
        result.blendConstants[1] = 1.f;
        result.blendConstants[2] = 1.f;
        result.blendConstants[3] = 1.f;

        return result;
    }
}

#ifdef VKU_IMPLEMENTATION
VULKAN_HPP_NAMESPACE::ImageAspectFlags vku::details::getAspectFlags(VULKAN_HPP_NAMESPACE::Format format) {
    assert(format != VULKAN_HPP_NAMESPACE::Format::eUndefined && "Format must be defined.");
    switch (format) {
        case VULKAN_HPP_NAMESPACE::Format::eUndefined:
            std::unreachable();
        case VULKAN_HPP_NAMESPACE::Format::eD16Unorm:
        case VULKAN_HPP_NAMESPACE::Format::eD32Sfloat:
            return VULKAN_HPP_NAMESPACE::ImageAspectFlagBits::eDepth;
        case VULKAN_HPP_NAMESPACE::Format::eD16UnormS8Uint:
        case VULKAN_HPP_NAMESPACE::Format::eD24UnormS8Uint:
        case VULKAN_HPP_NAMESPACE::Format::eD32SfloatS8Uint:
            return VULKAN_HPP_NAMESPACE::ImageAspectFlagBits::eDepth | VULKAN_HPP_NAMESPACE::ImageAspectFlagBits::eStencil;
        case VULKAN_HPP_NAMESPACE::Format::eS8Uint:
            return VULKAN_HPP_NAMESPACE::ImageAspectFlagBits::eStencil;
        default:
            return VULKAN_HPP_NAMESPACE::ImageAspectFlagBits::eColor;
    }
}

#ifndef VULKAN_HPP_DISABLE_ENHANCED_MODE
void vku::allocateDescriptorSets(
    const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
    VULKAN_HPP_NAMESPACE::DescriptorPool descriptorPool,
    std::initializer_list<std::pair<VULKAN_HPP_NAMESPACE::DescriptorSetLayout, VULKAN_HPP_NAMESPACE::ArrayProxy<std::reference_wrapper<VULKAN_HPP_NAMESPACE::DescriptorSet>>>> pairs,
    const void *pNext
) {
    return allocateDescriptorSets(*device, descriptorPool, pairs, pNext, *device.getDispatcher());
}

vku::raii::AllocatedBuffer::AllocatedBuffer(
    VMA_HPP_NAMESPACE::Allocator allocator,
    const VULKAN_HPP_NAMESPACE::BufferCreateInfo &createInfo,
    const VMA_HPP_NAMESPACE::AllocationCreateInfo &allocationCreateInfo,
    VULKAN_HPP_NAMESPACE::Optional<VMA_HPP_NAMESPACE::AllocationInfo> allocationInfo
) : Buffer { nullptr, createInfo.size },
    allocator { allocator } {
    std::tie(buffer, allocation) = allocator.createBuffer(createInfo, allocationCreateInfo, allocationInfo);
}

vku::raii::AllocatedBuffer::AllocatedBuffer(AllocatedBuffer &&src) noexcept
    : Buffer { static_cast<Buffer>(src) }
    , allocator { src.allocator }
    , allocation { std::exchange(src.allocation, {}) } { }

vku::raii::AllocatedBuffer &vku::raii::AllocatedBuffer::operator=(AllocatedBuffer &&src) noexcept {
    if (allocation) {
        allocator.destroyBuffer(buffer, allocation);
    }

    static_cast<Buffer&>(*this) = static_cast<Buffer>(src);
    allocator = src.allocator;
    allocation = std::exchange(src.allocation, {});
    return *this;
}

vku::raii::AllocatedBuffer::~AllocatedBuffer() {
    if (allocation) {
        allocator.destroyBuffer(buffer, allocation);
    }
}

VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Buffer vku::raii::AllocatedBuffer::createAlias(
    const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
    const VULKAN_HPP_NAMESPACE::BufferCreateInfo &createInfo,
    VULKAN_HPP_NAMESPACE::DeviceSize allocationLocalOffset,
    VULKAN_HPP_NAMESPACE::Optional<const VULKAN_HPP_NAMESPACE::AllocationCallbacks> allocator
) const {
    return { device, this->allocator.createAliasingBuffer2(allocation, allocationLocalOffset, createInfo), allocator };
}

vku::raii::AllocatedImage::AllocatedImage(
    VMA_HPP_NAMESPACE::Allocator allocator,
    const VULKAN_HPP_NAMESPACE::ImageCreateInfo &createInfo,
    const VMA_HPP_NAMESPACE::AllocationCreateInfo &allocationCreateInfo,
    VULKAN_HPP_NAMESPACE::Optional<VMA_HPP_NAMESPACE::AllocationInfo> allocationInfo
) : Image { nullptr, createInfo.extent, createInfo.format, createInfo.mipLevels, createInfo.arrayLayers },
    allocator { allocator } {
    std::tie(image, allocation) = allocator.createImage(createInfo, allocationCreateInfo, allocationInfo);
}

vku::raii::AllocatedImage::AllocatedImage(AllocatedImage &&src) noexcept
    : Image { static_cast<Image>(src) }
    , allocator { src.allocator }
    , allocation { std::exchange(src.allocation, {}) } { }

vku::raii::AllocatedImage &vku::raii::AllocatedImage::operator=(AllocatedImage &&src) noexcept {
    if (allocation) {
        allocator.destroyImage(image, allocation);
    }

    static_cast<Image&>(*this) = static_cast<Image>(src);
    allocator = src.allocator;
    allocation = std::exchange(src.allocation, {});
    return *this;
}

vku::raii::AllocatedImage::~AllocatedImage() {
    if (allocation) {
        allocator.destroyImage(image, allocation);
    }
}

VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Image vku::raii::AllocatedImage::createAlias(
    const VULKAN_HPP_NAMESPACE::VULKAN_HPP_RAII_NAMESPACE::Device &device,
    const VULKAN_HPP_NAMESPACE::ImageCreateInfo &createInfo,
    VULKAN_HPP_NAMESPACE::DeviceSize allocationLocalOffset,
    VULKAN_HPP_NAMESPACE::Optional<const VULKAN_HPP_NAMESPACE::AllocationCallbacks> allocator
) const {
    return { device, this->allocator.createAliasingImage2(allocation, allocationLocalOffset, createInfo), allocator };
}

vku::DescriptorPoolSize &vku::DescriptorPoolSize::operator+=(const DescriptorPoolSize &rhs) noexcept {
    setCount += rhs.setCount;
    for (const auto &[type, count] : rhs.descriptorCounts) {
        descriptorCounts[type] += count;
    }
    return *this;
}

vku::DescriptorPoolSize &vku::DescriptorPoolSize::operator+=(DescriptorPoolSize &&rhs) noexcept {
    setCount += rhs.setCount;
    for (auto it = rhs.descriptorCounts.begin(); it != rhs.descriptorCounts.end(); ++it) {
        // Move rhs nodes to the self.
        const auto [position, inserted, _] = descriptorCounts.insert(rhs.descriptorCounts.extract(it));
        if (!inserted) {
            // Self already contains node with the same descriptor type. Just increment the count.
            position->second += it->second;
        }
    }
    return *this;
}

vku::DescriptorPoolSize vku::DescriptorPoolSize::operator+(DescriptorPoolSize rhs) const noexcept {
    rhs += *this;
    return rhs;
}

vku::DescriptorPoolSize &vku::DescriptorPoolSize::operator*=(std::uint32_t rhs) noexcept {
    setCount *= rhs;
    for (std::uint32_t &count : descriptorCounts | std::views::values) {
        count *= rhs;
    }
    return *this;
}

vku::DescriptorPoolSize vku::DescriptorPoolSize::operator*(std::uint32_t rhs) const noexcept {
    DescriptorPoolSize result { *this };
    result *= rhs;
    return result;
}

std::vector<VULKAN_HPP_NAMESPACE::DescriptorPoolSize> vku::DescriptorPoolSize::getDescriptorPoolSizes() const {
    std::vector<VULKAN_HPP_NAMESPACE::DescriptorPoolSize> result;
    for (const auto &[type, count] : descriptorCounts) {
        result.emplace_back(type, count);
    }
    return result;
}
#endif
#endif