module;

#include <cassert>
#include <optional>
#include <ranges>
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

        auto getRenderingInfo(
            std::span<const std::tuple<vk::AttachmentLoadOp, vk::AttachmentStoreOp, vk::ClearColorValue>> colorAttachmentInfos = {},
            const std::optional<std::tuple<vk::AttachmentLoadOp, vk::AttachmentStoreOp, vk::ClearDepthStencilValue>> &depthStencilAttachmentInfo = {}
        ) const -> RefHolder<vk::RenderingInfo, std::vector<vk::RenderingAttachmentInfo>, std::optional<vk::RenderingAttachmentInfo>> override;
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
    std::span<const std::tuple<vk::AttachmentLoadOp, vk::AttachmentStoreOp, vk::ClearColorValue>> colorAttachmentInfos,
    const std::optional<std::tuple<vk::AttachmentLoadOp, vk::AttachmentStoreOp, vk::ClearDepthStencilValue>> &depthStencilAttachmentInfo
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
                depthStencilAttachmentInfo ? &*depthStencilAttachmentInfo : nullptr,
            };
        },
        ranges::views::zip_transform([](const MsaaAttachment &attachment, const std::tuple<vk::AttachmentLoadOp, vk::AttachmentStoreOp, vk::ClearColorValue> &info) {
            return vk::RenderingAttachmentInfo {
                *attachment.view, vk::ImageLayout::eColorAttachmentOptimal,
                vk::ResolveModeFlagBits::eAverage, *attachment.resolveView,
                vk::ImageLayout::eColorAttachmentOptimal,
                std::get<0>(info), std::get<1>(info), std::get<2>(info),
            };
        }, colorAttachments, colorAttachmentInfos) | std::ranges::to<std::vector>(),
        depthStencilAttachment.transform([info = *depthStencilAttachmentInfo](const Attachment &attachment) {
            return vk::RenderingAttachmentInfo {
                *attachment.view, vk::ImageLayout::eDepthStencilAttachmentOptimal,
                {}, {}, {},
                std::get<0>(info), std::get<1>(info), std::get<2>(info),
            };
        }),
    };
}