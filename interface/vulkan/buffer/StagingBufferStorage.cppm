module;

#include <boost/container/small_vector.hpp>
#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.StagingBufferStorage;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::buffer {
    class StagingBufferStorage {
    public:
        /**
         * @brief Assign the device local buffer to the \p buffer and add staging task to the storage.
         * @param buffer Buffer to be used as a staging buffer. Its ownership will be transferred to the storage, and assigned with the newly created device local buffer.
         * @param usage Usage flags of the device local buffer.
         * @pre \p usage MUST contain <tt>VK_BUFFER_USAGE_TRANSFER_DST_BIT</tt>.
         */
        void stage(vku::AllocatedBuffer &buffer, vk::BufferUsageFlags usage) {
            vku::AllocatedBuffer deviceLocalBuffer { buffer.allocator, vk::BufferCreateInfo {
                {},
                buffer.size,
                usage,
            } };
            stagingInfos.emplace_back(
                std::move(buffer),
                deviceLocalBuffer,
                boost::container::small_vector<vk::BufferCopy, 1> { vk::BufferCopy { 0, 0, deviceLocalBuffer.size } });
            buffer = std::move(deviceLocalBuffer);
        }

        /**
         * @brief Determine if there is a staging task to be done.
         *
         * This could be useful if you want to selectively create a command pool, record commands, and submit them for only staging purpose.
         * @code
         * if (stagingBufferStorage.hasStagingCommands()) {
         *     const vk::raii::CommandPool transferCommandPool { ... };
         *     vku::executeSingleCommand(..., [](vk::CommandBuffer cb) {
         *         stagingBufferStorage.recordStagingCommands(cb);
         *     });
         * }
         * @endcode
         * When the method returns <tt>false</tt>, unnecessary command pool creation, recording, and submission can be avoided.
         *
         * @return <tt>true</tt> if there is a staging task to be recorded to command buffer, <tt>false</tt> otherwise.
         */
        [[nodiscard]] bool hasStagingCommands() const noexcept {
            return !stagingInfos.empty();
        }

        /**
         * @brief Record the staging commands to \p transferCommandBuffer.
         * @param transferCommandBuffer Command buffer to be recorded.
         * @pre \p transferCommandBuffer must be created from <tt>VK_QUEUE_TRANSFER_BIT</tt> capable queue family.
         * @note This will not clear the staging tasks, means that the staging tasks will be recorded again if this method is called multiple times.
         */
        void recordStagingCommands(vk::CommandBuffer transferCommandBuffer) const noexcept {
            for (const auto &[stagingBuffer, dstBuffer, copyRegions] : stagingInfos) {
                transferCommandBuffer.copyBuffer(stagingBuffer, dstBuffer, copyRegions);
            }
        }

        /**
         * @brief Determine the given \p buffer needs to be staged.
         *
         * If \p buffer's memory property flags does not contain <tt>VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT</tt>, it needs to be staged for faster access in GPU.
         *
         * @param buffer Buffer to be checked.
         * @return <tt>true</tt> if the buffer needs to be staged, <tt>false</tt> otherwise.
         */
        [[nodiscard]] static bool needStaging(const vku::AllocatedBuffer &buffer) noexcept {
            const vk::MemoryPropertyFlags memoryPropertyFlags = buffer.allocator.getAllocationMemoryProperties(buffer.allocation);
            return !vku::contains(memoryPropertyFlags, vk::MemoryPropertyFlagBits::eDeviceLocal);
        }

    private:
        struct StagingInfo {
            vku::AllocatedBuffer stagingBuffer;
            vk::Buffer dstBuffer;
            boost::container::small_vector<vk::BufferCopy, 1> copyRegions;
        };

        std::vector<StagingInfo> stagingInfos;
    };
}