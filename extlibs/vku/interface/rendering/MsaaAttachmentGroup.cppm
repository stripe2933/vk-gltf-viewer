module;

#include <cassert>
#include <compare>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <vector>

export module vku:rendering.MsaaAttachmentGroup;

export import vk_mem_alloc_hpp;
export import vulkan_hpp;
import :details;
export import :images.Image;
export import :images.AllocatedImage;
export import :rendering.Attachment;
import :rendering.AttachmentGroupBase;
export import :rendering.MsaaAttachment;
import :utils.RefHolder;

namespace vku {
    export struct MsaaAttachmentGroup : AttachmentGroupBase {
        struct ColorAttachmentInfo {
            vk::AttachmentLoadOp loadOp;
            vk::AttachmentStoreOp storeOp;
            vk::ClearColorValue clearValue;
            vk::ResolveModeFlagBits resolveMode = vk::ResolveModeFlagBits::eAverage;
        };

        struct DepthStencilAttachmentInfo {
            vk::AttachmentLoadOp loadOp;
            vk::AttachmentStoreOp storeOp;
            vk::ClearDepthStencilValue clearValue;
        };

        vk::SampleCountFlagBits sampleCount;
        std::vector<MsaaAttachment> colorAttachments;
        std::optional<Attachment> depthStencilAttachment;

        explicit MsaaAttachmentGroup(const vk::Extent2D &extent, vk::SampleCountFlagBits sampleCount);
        MsaaAttachmentGroup(const MsaaAttachmentGroup&) = delete;
        MsaaAttachmentGroup(MsaaAttachmentGroup&&) noexcept = default;
        auto operator=(const MsaaAttachmentGroup&) -> MsaaAttachmentGroup& = delete;
        auto operator=(MsaaAttachmentGroup&&) noexcept -> MsaaAttachmentGroup& = default;
        ~MsaaAttachmentGroup() override = default;

        auto addColorAttachment(
            const vk::raii::Device &device,
            const Image &image,
            const Image &resolveImage,
            vk::Format format = {},
            const vk::ImageSubresourceRange &subresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 },
            const vk::ImageSubresourceRange &resolveSubresourceRange = { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        ) -> const MsaaAttachment&;

        [[nodiscard]] auto createColorImage(
            vma::Allocator allocator,
            vk::Format format,
            vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransientAttachment,
            const vma::AllocationCreateInfo &allocationCreateInfo = { {}, vma::MemoryUsage::eAutoPreferDevice, {}, vk::MemoryPropertyFlagBits::eLazilyAllocated }
        ) const -> AllocatedImage;

        [[nodiscard]] auto createResolveImage(
            vma::Allocator allocator,
            vk::Format format,
            vk::ImageUsageFlags usage = {},
            const vma::AllocationCreateInfo &allocationCreateInfo = { {}, vma::MemoryUsage::eAutoPreferDevice }
        ) const -> AllocatedImage;

        auto setDepthAttachment(
            const vk::raii::Device &device,
            const Image &image,
            vk::Format format = {},
            const vk::ImageSubresourceRange &subresourceRange = { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 }
        ) -> const Attachment&;

        auto setStencilAttachment(
            const vk::raii::Device &device,
            const Image &image,
            vk::Format format = {},
            const vk::ImageSubresourceRange &subresourceRange = { vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1 }
        ) -> const Attachment&;

        auto setDepthStencilAttachment(
            const vk::raii::Device &device,
            const Image &image,
            vk::Format format = {},
            const vk::ImageSubresourceRange &subresourceRange = { vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil, 0, 1, 0, 1 }
        ) -> const Attachment&;

        [[nodiscard]] auto createDepthStencilImage(
            vma::Allocator allocator,
            vk::Format format,
            vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransientAttachment,
            const vma::AllocationCreateInfo &allocationCreateInfo = { {}, vma::MemoryUsage::eAutoPreferDevice, {}, vk::MemoryPropertyFlagBits::eLazilyAllocated }
        ) const -> AllocatedImage;

        [[nodiscard]] auto getRenderingInfo(
            std::span<const ColorAttachmentInfo> colorAttachmentInfos = {},
            const std::optional<DepthStencilAttachmentInfo> &depthStencilAttachmentInfo = {}
        ) const -> RefHolder<vk::RenderingInfo, std::vector<vk::RenderingAttachmentInfo>, std::optional<vk::RenderingAttachmentInfo>>;
    };
}

// module:private;

vku::MsaaAttachmentGroup::MsaaAttachmentGroup(
    const vk::Extent2D &extent,
    vk::SampleCountFlagBits sampleCount
) : AttachmentGroupBase { extent },
    sampleCount { sampleCount } { }

auto vku::MsaaAttachmentGroup::addColorAttachment(
    const vk::raii::Device &device,
    const Image &image,
    const Image &resolveImage, vk::Format format,
    const vk::ImageSubresourceRange &subresourceRange,
    const vk::ImageSubresourceRange &resolveSubresourceRange
) -> const MsaaAttachment& {
    return colorAttachments.emplace_back(
        image,
        vk::raii::ImageView { device, vk::ImageViewCreateInfo {
            {},
            image,
            vk::ImageViewType::e2D,
            format == vk::Format::eUndefined ? image.format : format,
            {},
            subresourceRange,
        } },
        resolveImage,
        vk::raii::ImageView { device, vk::ImageViewCreateInfo {
            {},
            resolveImage,
            vk::ImageViewType::e2D,
            format == vk::Format::eUndefined ? resolveImage.format : format,
            {},
            resolveSubresourceRange,
        } }
    );
}

auto vku::MsaaAttachmentGroup::createColorImage(
    vma::Allocator allocator,
    vk::Format format,
    vk::ImageUsageFlags usage,
    const vma::AllocationCreateInfo &allocationCreateInfo
) const -> AllocatedImage {
    return createAttachmentImage(
        allocator,
        format,
        sampleCount,
        usage | vk::ImageUsageFlagBits::eColorAttachment,
        allocationCreateInfo);
}

auto vku::MsaaAttachmentGroup::createResolveImage(
    vma::Allocator allocator,
    vk::Format format,
    vk::ImageUsageFlags usage,
    const vma::AllocationCreateInfo &allocationCreateInfo
) const -> AllocatedImage {
    return createAttachmentImage(
        allocator,
        format,
        vk::SampleCountFlagBits::e1,
        usage | vk::ImageUsageFlagBits::eColorAttachment,
        allocationCreateInfo);
}

auto vku::MsaaAttachmentGroup::setDepthAttachment(
    const vk::raii::Device &device,
    const Image &image,
    vk::Format format,
    const vk::ImageSubresourceRange &subresourceRange
) -> const Attachment& {
    return depthStencilAttachment.emplace(
        image,
        vk::raii::ImageView { device, vk::ImageViewCreateInfo {
            {},
            image,
            vk::ImageViewType::e2D,
            format == vk::Format::eUndefined ? image.format : format,
            {},
            subresourceRange,
        } });
}

auto vku::MsaaAttachmentGroup::setStencilAttachment(
    const vk::raii::Device &device,
    const Image &image,
    vk::Format format,
    const vk::ImageSubresourceRange &subresourceRange
) -> const Attachment& {
    return depthStencilAttachment.emplace(
        image,
        vk::raii::ImageView { device, vk::ImageViewCreateInfo {
            {},
            image,
            vk::ImageViewType::e2D,
            format == vk::Format::eUndefined ? image.format : format,
            {},
            subresourceRange,
        } });
}

auto vku::MsaaAttachmentGroup::setDepthStencilAttachment(
    const vk::raii::Device &device,
    const Image &image,
    vk::Format format,
    const vk::ImageSubresourceRange &subresourceRange
) -> const Attachment& {
    return depthStencilAttachment.emplace(
        image,
        vk::raii::ImageView { device, vk::ImageViewCreateInfo {
            {},
            image,
            vk::ImageViewType::e2D,
            format == vk::Format::eUndefined ? image.format : format,
            {},
            subresourceRange,
        } });
}

auto vku::MsaaAttachmentGroup::createDepthStencilImage(
    vma::Allocator allocator,
    vk::Format format,
    vk::ImageUsageFlags usage,
    const vma::AllocationCreateInfo &allocationCreateInfo
) const -> AllocatedImage {
    return createAttachmentImage(
        allocator,
        format,
        sampleCount,
        usage | vk::ImageUsageFlagBits::eDepthStencilAttachment,
        allocationCreateInfo);
}

auto vku::MsaaAttachmentGroup::getRenderingInfo(
    std::span<const ColorAttachmentInfo> colorAttachmentInfos,
    const std::optional<DepthStencilAttachmentInfo> &depthStencilAttachmentInfo
) const -> RefHolder<vk::RenderingInfo, std::vector<vk::RenderingAttachmentInfo>, std::optional<vk::RenderingAttachmentInfo>> {
    assert(colorAttachments.size() == colorAttachmentInfos.size() && "Color attachment info count mismatch");
    assert(depthStencilAttachment.has_value() == depthStencilAttachmentInfo.has_value() && "Depth-stencil attachment info mismatch");
    return {
        [this](std::span<const vk::RenderingAttachmentInfo> colorAttachmentInfos, const std::optional<vk::RenderingAttachmentInfo> &depthStencilAttachmentInfo) {
            return vk::RenderingInfo {
                {},
                { { 0, 0 }, extent },
                1,
                {},
                colorAttachmentInfos,
                depthStencilAttachmentInfo.transform([](const auto& x) { return &x;  }).value_or(nullptr),
            };
        },
        ranges::views::zip_transform([](const MsaaAttachment &attachment, const ColorAttachmentInfo &info) {
            return vk::RenderingAttachmentInfo {
                *attachment.view, vk::ImageLayout::eColorAttachmentOptimal,
                info.resolveMode, *attachment.resolveView,
                vk::ImageLayout::eColorAttachmentOptimal,
                info.loadOp, info.storeOp, info.clearValue,
            };
        }, colorAttachments, colorAttachmentInfos) | std::ranges::to<std::vector>(),
        depthStencilAttachment.transform([&](const Attachment &attachment) {
            const auto& info = *depthStencilAttachmentInfo;
            return vk::RenderingAttachmentInfo {
                *attachment.view, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                {}, {}, {},
                info.loadOp, info.storeOp, info.clearValue,
            };
        }),
    };
}