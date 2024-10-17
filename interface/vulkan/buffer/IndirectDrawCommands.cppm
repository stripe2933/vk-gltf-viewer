export module vk_gltf_viewer:vulkan.buffer.IndirectDrawCommands;

import std;
export import vku;

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Vulkan buffer that represents indirect draw commands.
     *
     * It contains draw count in the first [0, <tt>sizeof(std::uint32_t)</tt>) bytes, and the actual draw commands
     * (which is either <tt>vk::DrawIndexedIndirectCommand</tt> or <tt>vk::DrawIndirectCommand</tt> based on the
     * template parameter) in the rest of the buffer.
     *
     * It provides some convenient methods that reorder the draw commands based on the predicate, and a method to reset the draw count to
     *
     * @tparam Indexed Boolean flag to indicate whether the draw command is indexed or not.
     */
    export template <bool Indexed>
    struct IndirectDrawCommands : vku::MappedBuffer {
        using command_t = std::conditional_t<Indexed, vk::DrawIndexedIndirectCommand, vk::DrawIndirectCommand>;

        IndirectDrawCommands(vma::Allocator allocator, std::span<const command_t> commands)
            : MappedBuffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(std::uint32_t) /* draw count */ + sizeof(command_t) * commands.size(),
                vk::BufferUsageFlagBits::eIndirectBuffer,
            }, vku::allocation::hostRead } {
            asValue<std::uint32_t>() = commands.size();
            std::ranges::copy(as_bytes(commands), static_cast<std::byte*>(data) + sizeof(std::uint32_t));
        }

        /**
         * @brief Number of draw commands that should be executed in the buffer.
         * @return Number of draw commands.
         */
        [[nodiscard]] std::uint32_t drawCount() const noexcept {
            return asValue<const std::uint32_t>();
        }

        /**
         * @brief Number of the actual draw commands in the buffer.
         * @return Number of draw commands.
         */
        [[nodiscard]] std::uint32_t maxDrawCount() const noexcept {
            return (size - sizeof(std::uint32_t)) / sizeof(command_t);
        }

        /**
         * @brief Reorder the indirect draw commands to be in the head whose corresponding predicate returns <tt>true</tt>, and adjust the draw count accordingly.
         * @tparam F
         * @param f Predicate to determine the command to be in the head (true) or in the tail (false).
         */
        template <std::invocable<const command_t&> F>
        void partition(F &&f) noexcept(std::is_nothrow_invocable_v<F>) {
            const std::span commands = asRange<command_t>(sizeof(std::uint32_t));
            const auto tail = std::ranges::partition(commands, f);
            asValue<std::uint32_t>() = std::distance(commands.begin(), tail.begin());
        }

        /**
         * @brief Reset the draw count to the number of commands in the buffer.
         */
        void resetDrawCount() noexcept {
            asValue<std::uint32_t>() = maxDrawCount();
        }

        /**
         * @brief Record draw command based on \p drawIndirectCount feature availability.
         * @param cb Command buffer to be recorded.
         * @param drawIndirectCount Whether to use <tt>vkCmdDrawIndexedIndirectCount</tt> or <tt>vkCmdDrawIndexedIndirect</tt>.
         */
        void recordDrawCommand(vk::CommandBuffer cb, bool drawIndirectCount) const {
            if constexpr (Indexed) {
                if (drawIndirectCount) {
                    cb.drawIndexedIndirectCount(*this, sizeof(std::uint32_t), *this, 0, maxDrawCount(), sizeof(vk::DrawIndexedIndirectCommand));
                }
                else {
                    cb.drawIndexedIndirect(*this, sizeof(std::uint32_t), drawCount(), sizeof(vk::DrawIndexedIndirectCommand));
                }
            }
            else {
                if (drawIndirectCount) {
                    cb.drawIndirectCount(*this, sizeof(std::uint32_t), *this, 0, maxDrawCount(), sizeof(vk::DrawIndirectCommand));
                }
                else {
                    cb.drawIndirect(*this, sizeof(std::uint32_t), drawCount(), sizeof(vk::DrawIndirectCommand));
                }
            }
        }
    };
}