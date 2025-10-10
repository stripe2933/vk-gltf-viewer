module;

#include <lifetimebound.hpp>

export module vkgltf.StagingBufferStorage;

import std;
export import vku;

namespace vkgltf {
    export class StagingBufferStorage {
    public:
        StagingBufferStorage(
            const vk::raii::Device &device LIFETIMEBOUND,
            vk::CommandPool transferCommandPool LIFETIMEBOUND,
            vk::Queue transferQueue
        );
        
        ~StagingBufferStorage();

        /**
         * @brief Replace \p buffer with same size and device local buffer, and record copy command.
         *
         * The replacement will be done only if the \p buffer is not device local.
         *
         * @param buffer Buffer to be used as a staging buffer. Its ownership will be transferred to the storage, and
         * replaced with the newly created device local buffer.
         * @param usage Usage flags of the device local buffer. <tt>vk::BufferUsageFlagBits::eTransferDst</tt> will be
         * automatically added.
         * @param queueFamilies Queue family indices that the buffer can be concurrently accessed. If its size is less
         * than 2, buffer sharing mode will be set to <tt>vk::SharingMode::eExclusive</tt>.
         * @return <tt>true</tt> if \p buffer is staged, <tt>false</tt> if \p buffer is already device local and does
         * not need staging.
         */
        bool stage(vku::raii::AllocatedBuffer &buffer, vk::BufferUsageFlags usage, vk::ArrayProxy<const std::uint32_t> queueFamilies = {});

        /**
         * @brief Take ownership of \p buffer and record buffer to image copy command.
         *
         * @param buffer Buffer to be used as a staging buffer. Its ownership will be transferred to the storage.
         * @param image Destination image to be copied to.
         * @param layout Destination image layout.
         * @param copyRegions Regions to be copied from the buffer to the image.
         */
        void stage(vku::raii::AllocatedBuffer &&buffer, vk::Image image, vk::ImageLayout layout, vk::ArrayProxy<const vk::BufferImageCopy> copyRegions);

        /**
         * @brief Record pipeline barrier with <tt>vk::ImageMemoryBarrier</tt>, whose layout transition from
         * <tt>vk::ImageLayout::eUndefined</tt> to \p dstLayout before the <tt>vk::PipelineStageFlagBits::eTransfer</tt>
         * stage and <tt>vk::AccessFlagBits::eTransferWrite</tt> access mask, with queue family ownership acquirement from
         * \p srcQueueFamilyIndex to \p dstQueueFamilyIndex.
         *
         * This method is intended to do the image layout transition before using \p image as the copy destination.
         *
         * @param image Image to be transitioned.
         * @param dstLayout Destination image layout.
         * @param srcQueueFamilyIndex Source queue family index.
         * @param dstQueueFamilyIndex Destination queue family index.
         * @param subresourceRange Subresource range to be transitioned.
         */
        void memoryBarrierFromTop(
            vk::Image image,
            vk::ImageLayout dstLayout,
            std::uint32_t srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            std::uint32_t dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            const vk::ImageSubresourceRange &subresourceRange = vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor)
        );

        /**
         * @brief Add pipeline barrier with <tt>vk::BufferMemoryBarrier</tt>, after the <tt>vk::PipelineStageFlagBits::eTransfer</tt>
         * stage and <tt>vk::AccessFlagBits::eTransferWrite</tt> access mask.
         *
         * The pipeline barrier is deferred until calling <tt>execute()</tt> method or destructor.
         *
         * This method is intended to release the queue family ownership after using \p buffer as the copy destination.
         *
         * @param buffer Buffer affected by pipeline barrier.
         * @param srcQueueFamilyIndex Source queue family index.
         * @param dstQueueFamilyIndex Destination queue family index.
         * @param offset Byte offset of the affected buffer region.
         * @param size Byte size of the affected buffer region, or <tt>vk::WholeSize</tt> to use the range from \p
         * offset to the end of the buffer.
         */
        void memoryBarrierToBottom(
            vk::Buffer buffer,
            std::uint32_t srcQueueFamilyIndex,
            std::uint32_t dstQueueFamilyIndex,
            vk::DeviceSize offset = 0,
            vk::DeviceSize size = vk::WholeSize
        );

        /**
         * @brief Add pipeline barrier with <tt>vk::ImageMemoryBarrier</tt>, whose layout transition from
         * <tt>vk::ImageLayout::eTransferDstOptimal</tt> to \p dstLayout after the <tt>vk::PipelineStageFlagBits::eTransfer</tt>
         * stage and <tt>vk::AccessFlagBits::eTransferWrite</tt> access mask, with queue family ownership acquirement from
         * \p srcQueueFamilyIndex to \p dstQueueFamilyIndex.
         *
         * Unlike <tt>memoryBarrierFromTop()</tt> method, the pipeline barrier is  deferred until calling
         * <tt>execute()</tt> method or destructor.
         *
         * This method is intended to do the image layout transition after using \p image as the copy destination.
         *
         * @param image Image to be transitioned.
         * @param srcLayout Source image layout, usually <tt>vk::ImageLayout::eTransferDstOptimal</tt>.
         * @param dstLayout Destination image layout.
         * @param srcQueueFamilyIndex Source queue family index.
         * @param dstQueueFamilyIndex Destination queue family index.
         * @param subresourceRange Subresource range to be transitioned.
         */
        void memoryBarrierToBottom(
            vk::Image image,
            vk::ImageLayout srcLayout,
            vk::ImageLayout dstLayout,
            std::uint32_t srcQueueFamilyIndex = vk::QueueFamilyIgnored,
            std::uint32_t dstQueueFamilyIndex = vk::QueueFamilyIgnored,
            const vk::ImageSubresourceRange &subresourceRange = vku::fullSubresourceRange(vk::ImageAspectFlagBits::eColor)
        );

        /**
         * @brief Record all deferred pipeline barriers to the command buffer, execute the command buffer and signal
         * the given \p signalSemaphores and \p fence. It is actually no-op if the command buffer has no recorded
         * commands.
         *
         * This method is automatically called from destructor when there are remaining command buffers, and blocked
         * during the command buffer execution. Usually, this method is called with subsequent <tt>reset()</tt> only if
         * you want to explicitly signal semaphores or a fence after the command buffer execution, or using the class
         * instance multiple times.
         *
         * This method does not deallocate the staging buffers. <tt>reset()</tt> method will do the role.
         *
         * @param signalSemaphores Semaphores to be signalled when copy command execution is end.
         * @param fence Fence to be signalled when copy command execution is end.
         * @warning You MUST call <tt>reset()</tt> method after calling this method. Otherwise, the destructor will
         * re-submit the command buffer to the queue, which violates the command buffer usage.
         */
        void execute(vk::ArrayProxy<const vk::Semaphore> signalSemaphores = {}, vk::Fence fence = {});

        /**
         * @brief Clear all staging buffers and copy/transition commands from memory.
         *
         * This MUST be called only after <tt>execute()</tt> method.
         *
         * @param beginCommandBuffer Begin the command buffer after reset. Can be set to <tt>false</tt> if you don't
         * have to reuse the class instance.
         */
        void reset(bool beginCommandBuffer = true);

    private:
        std::reference_wrapper<const vk::raii::Device> device;
        vk::Queue queue;

        vk::CommandBuffer cb;
        bool commandRecorded;

        std::vector<vku::raii::AllocatedBuffer> stagingBuffers;
        std::vector<vk::BufferMemoryBarrier> bufferMemoryBarriersToBottom;
        std::vector<vk::ImageMemoryBarrier> imageMemoryBarriersToBottom;
    };

    export struct StagingInfo {
        std::reference_wrapper<StagingBufferStorage> stagingBufferStorage;

        /**
         * @brief Source/destination queue family indices that are used for queue family ownership transfer.
         *
         * If set, pipeline barrier's <tt>srcQueueFamilyIndex</tt> and <tt>dstQueueFamilyIndex</tt> will be set for
         * releasing the queue family ownership.
         */
        std::optional<std::pair<std::uint32_t, std::uint32_t>> queueFamilyOwnershipTransfer;

        /**
         * @brief Mutex to protect the command buffer recording.
         *
         * If set, command buffer is recorded under the mutex lock.
         */
        std::mutex *mutex;

        /**
         * @brief Convenience method to stage \p buffer.
         *
         * First \p mutex lock is acquired if it is set, then the \p buffer is staged with the given \p usageFlags and \p queueFamilies.
         *
         * The replacement will be done only if the \p buffer is not device local.
         *
         * @param buffer Buffer to be staged. It will be replaced with the same size and given \p usageFlags and \p queueFamilies device local buffer.
         * @param usageFlags Usage flags of the device local buffer. <tt>vk::BufferUsageFlagBits::eTransferDst</tt> will be automatically added.
         * @param queueFamilies Queue family indices that the buffer can be concurrently accessed. If its size is less than 2, buffer sharing mode will be set to <tt>vk::SharingMode::eExclusive</tt>.
         * @return <tt>true</tt> if \p buffer is staged, <tt>false</tt> if \p buffer is already device local and does not need staging.
         */
        bool stage(vku::raii::AllocatedBuffer &buffer, vk::BufferUsageFlags usageFlags, vk::ArrayProxy<const std::uint32_t> queueFamilies = {}) const;
    };
}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

vkgltf::StagingBufferStorage::StagingBufferStorage(
    const vk::raii::Device &device,
    vk::CommandPool transferCommandPool,
    vk::Queue transferQueue
) : device { device },
    queue { transferQueue },
    cb { (*device).allocateCommandBuffers({ transferCommandPool, vk::CommandBufferLevel::ePrimary, 1 }, *device.getDispatcher())[0] },
    commandRecorded { false } {
    cb.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, *device.getDispatcher());
}

vkgltf::StagingBufferStorage::~StagingBufferStorage() {
    if (commandRecorded || !bufferMemoryBarriersToBottom.empty() || !imageMemoryBarriersToBottom.empty()) {
        vk::raii::Fence fence { device, vk::FenceCreateInfo{} };
        execute({}, *fence);
        std::ignore = device.get().waitForFences(*fence, true, ~0ULL);
    }
}

bool vkgltf::StagingBufferStorage::stage(vku::raii::AllocatedBuffer &buffer, vk::BufferUsageFlags usage, vk::ArrayProxy<const std::uint32_t> queueFamilies) {
    if (vku::contains(buffer.allocator.getAllocationMemoryProperties(buffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal)) {
        return false;
    }

    vku::raii::AllocatedBuffer deviceLocalBuffer {
        buffer.allocator,
        vk::BufferCreateInfo {
            {},
            buffer.size,
            vk::BufferUsageFlagBits::eTransferDst | usage,
            vku::getSharingMode(queueFamilies),
            queueFamilies,
        },
        vma::AllocationCreateInfo { {}, vma::MemoryUsage::eAutoPreferDevice },
    };
    cb.copyBuffer(
        stagingBuffers.emplace_back(std::move(buffer)),
        deviceLocalBuffer,
        vk::BufferCopy { 0, 0, deviceLocalBuffer.size },
        *device.get().getDispatcher());
    commandRecorded = true;
    buffer = std::move(deviceLocalBuffer);

    return true;
}

void vkgltf::StagingBufferStorage::stage(
    vku::raii::AllocatedBuffer &&buffer,
    vk::Image image,
    vk::ImageLayout layout,
    vk::ArrayProxy<const vk::BufferImageCopy> copyRegions
) {
    cb.copyBufferToImage(
        stagingBuffers.emplace_back(std::move(buffer)), image,
        layout, copyRegions,
        *device.get().getDispatcher());
    commandRecorded = true;
}

void vkgltf::StagingBufferStorage::memoryBarrierFromTop(
    vk::Image image,
    vk::ImageLayout dstLayout,
    std::uint32_t srcQueueFamilyIndex,
    std::uint32_t dstQueueFamilyIndex,
    const vk::ImageSubresourceRange &subresourceRange
) {
    cb.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
        {}, {}, {},
        vk::ImageMemoryBarrier {
            {}, vk::AccessFlagBits::eTransferWrite,
            {}, dstLayout,
            srcQueueFamilyIndex, dstQueueFamilyIndex,
            image, subresourceRange,
        },
        *device.get().getDispatcher());
    commandRecorded = true;
}

void vkgltf::StagingBufferStorage::memoryBarrierToBottom(
    vk::Buffer buffer,
    std::uint32_t srcQueueFamilyIndex,
    std::uint32_t dstQueueFamilyIndex,
    vk::DeviceSize offset,
    vk::DeviceSize size
) {
    bufferMemoryBarriersToBottom.push_back({
        vk::AccessFlagBits::eTransferWrite, {},
        srcQueueFamilyIndex, dstQueueFamilyIndex,
        buffer, offset, size,
    });
}

void vkgltf::StagingBufferStorage::memoryBarrierToBottom(
    vk::Image image,
    vk::ImageLayout srcLayout,
    vk::ImageLayout dstLayout,
    std::uint32_t srcQueueFamilyIndex,
    std::uint32_t dstQueueFamilyIndex,
    const vk::ImageSubresourceRange &subresourceRange
) {
    imageMemoryBarriersToBottom.push_back({
        vk::AccessFlagBits::eTransferWrite, {},
        srcLayout, dstLayout,
        srcQueueFamilyIndex, dstQueueFamilyIndex,
        image, subresourceRange,
    });
}

void vkgltf::StagingBufferStorage::execute(vk::ArrayProxy<const vk::Semaphore> signalSemaphores, vk::Fence fence) {
    if (!bufferMemoryBarriersToBottom.empty() || !imageMemoryBarriersToBottom.empty()) {
        cb.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, bufferMemoryBarriersToBottom, imageMemoryBarriersToBottom,
            *device.get().getDispatcher());
        commandRecorded = true;
    }

    if (commandRecorded) {
        cb.end(*device.get().getDispatcher());
        queue.submit(vk::SubmitInfo {
            {},
            {},
            cb,
            signalSemaphores,
        }, fence, *device.get().getDispatcher());

        // Command buffer is allocated for one time submit.
        commandRecorded = false;
    }
}

void vkgltf::StagingBufferStorage::reset(bool beginCommandBuffer) {
    stagingBuffers.clear();
    bufferMemoryBarriersToBottom.clear();
    imageMemoryBarriersToBottom.clear();

    if (beginCommandBuffer) {
        cb.reset({}, *device.get().getDispatcher());
        cb.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit }, *device.get().getDispatcher());
    }
}

bool vkgltf::StagingInfo::stage(vku::raii::AllocatedBuffer &buffer, vk::BufferUsageFlags usageFlags, vk::ArrayProxy<const std::uint32_t> queueFamilies) const {
    std::unique_lock<std::mutex> lock;
    if (mutex) {
        lock = std::unique_lock { *mutex };
    }

    if (!stagingBufferStorage.get().stage(buffer, usageFlags, queueFamilies)) {
        return false;
    }

    if (queueFamilyOwnershipTransfer) {
        const auto &[src, dst] = *queueFamilyOwnershipTransfer;
        stagingBufferStorage.get().memoryBarrierToBottom(buffer, src, dst);
    }

    return true;
}