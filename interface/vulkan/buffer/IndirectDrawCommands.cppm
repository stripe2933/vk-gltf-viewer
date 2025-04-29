module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.IndirectDrawCommands;

import std;
export import fastgltf;
export import vku;
import :helpers.concepts;
import :helpers.ranges;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Vulkan buffer that represents indirect draw commands.
     *
     * It contains draw count in the first [0, <tt>sizeof(std::uint32_t)</tt>) bytes, and the actual draw commands
     * (which is either <tt>vk::DrawIndexedIndirectCommand</tt> or <tt>vk::DrawIndirectCommand</tt> based on the
     * template parameter) in the rest of the buffer.
     *
     * It provides some convenient methods that reorder the draw commands based on the predicate, and a method to reset the draw count to its maximum buffer size.
     */
    export struct IndirectDrawCommands : vku::MappedBuffer {
        bool indexed;

        template <concepts::one_of<vk::DrawIndirectCommand, vk::DrawIndexedIndirectCommand> Command>
        IndirectDrawCommands(vma::Allocator allocator, std::span<const Command> commands)
            : MappedBuffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(std::uint32_t) /* draw count */ + sizeof(Command) * commands.size(),
                vk::BufferUsageFlagBits::eIndirectBuffer,
            }, vku::allocation::hostRead }
            , indexed { std::same_as<Command, vk::DrawIndexedIndirectCommand> } {
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
            return (size - sizeof(std::uint32_t)) / (indexed ? sizeof(vk::DrawIndexedIndirectCommand) : sizeof(vk::DrawIndirectCommand));
        }

        /**
         * @brief Reorder the indirect draw commands to be in the head whose corresponding predicate returns <tt>true</tt>, and adjust the draw count accordingly.
         * @tparam F A functor type which determine the partition by take a draw command as an argument.
         * @param f Predicate to determine the command to be in the head (true) or in the tail (false).
         */
        template <typename F> requires
            std::predicate<F, const vk::DrawIndirectCommand&> && std::predicate<F, const vk::DrawIndexedIndirectCommand&>
        void partition(F &&f) noexcept(std::is_nothrow_invocable_v<F>) {
            if (indexed) {
                const std::span commands = asRange<vk::DrawIndexedIndirectCommand>(sizeof(std::uint32_t));
                const auto tail = std::ranges::partition(commands, FWD(f));
                asValue<std::uint32_t>() = std::distance(commands.begin(), tail.begin());
            }
            else {
                const std::span commands = asRange<vk::DrawIndirectCommand>(sizeof(std::uint32_t));
                const auto tail = std::ranges::partition(commands, FWD(f));
                asValue<std::uint32_t>() = std::distance(commands.begin(), tail.begin());
            }
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
         * @param drawIndirectCount Whether to use <tt>vkCmdDraw(Indexed)IndirectCount</tt> or <tt>vkCmdDraw(Indexed)Indirect</tt>.
         */
        void recordDrawCommand(vk::CommandBuffer cb, bool drawIndirectCount) const {
            if (indexed) {
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

    export template <
        std::invocable<const fastgltf::Primitive&> CriteriaGetter,
        typename Criteria = std::invoke_result_t<CriteriaGetter, const fastgltf::Primitive&>,
        typename Compare = std::less<Criteria>>
    [[nodiscard]] std::map<Criteria, IndirectDrawCommands, Compare> createIndirectDrawCommandBuffers(
        const fastgltf::Asset &asset,
        vma::Allocator allocator,
        const CriteriaGetter &criteriaGetter,
        std::ranges::input_range auto const &nodeIndices,
        concepts::signature_of<std::variant<vk::DrawIndirectCommand, vk::DrawIndexedIndirectCommand>(std::size_t, const fastgltf::Primitive&)> auto const &drawCommandGetter
    ) {
        std::map<Criteria, std::variant<std::vector<vk::DrawIndirectCommand>, std::vector<vk::DrawIndexedIndirectCommand>>> commandGroups;

        for (std::size_t nodeIndex : nodeIndices) {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            if (!node.meshIndex) {
                continue;
            }

            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            for (const fastgltf::Primitive &primitive : mesh.primitives) {
                const Criteria criteria = criteriaGetter(primitive);
                visit([&]<typename DrawCommand>(const DrawCommand &drawCommand) {
                    auto &commandGroup = commandGroups
                        .try_emplace(criteria, std::in_place_type<std::vector<DrawCommand>>)
                        .first->second;

                    // std::bad_variant_access in here means the criteria already exists in the commandGroups, but the
                    // indexed property is not matching, i.e. both indexed and non-indexed draw command is in the same
                    // criteria.
                    get<std::vector<DrawCommand>>(commandGroup).push_back(drawCommand);
                }, drawCommandGetter(nodeIndex, primitive));
            }
        }

        return commandGroups
            | ranges::views::value_transform([&](const auto &variant) {
                return visit([&](const auto &commands) {
                    return IndirectDrawCommands { allocator, std::span { commands } };
                }, variant);
            })
            | std::ranges::to<std::map<Criteria, IndirectDrawCommands, Compare>>();
    }
}