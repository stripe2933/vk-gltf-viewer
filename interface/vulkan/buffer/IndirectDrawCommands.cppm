module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer.vulkan.buffer.IndirectDrawCommands;

import std;
export import fastgltf;
export import vku;

import vk_gltf_viewer.helpers.concepts;
import vk_gltf_viewer.helpers.ranges;

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
            setDrawCount(commands.size());
            std::ranges::copy(commands, get<std::span<Command>>(drawIndirectCommands()).begin());
        }

        /**
         * @brief Number of draw commands that should be executed in the buffer.
         * @return Number of draw commands.
         */
        [[nodiscard]] std::uint32_t drawCount() const noexcept;

        /**
         * @brief Set the number of draw commands that should be executed in the buffer.
         * @param drawCount Number of draw commands to be set.
         * @throw std::invalid_argument If \p drawCount exceeds the maximum draw count of the buffer.
         */
        void setDrawCount(std::uint32_t drawCount);

        /**
         * @brief Number of the actual draw commands in the buffer.
         * @return Number of draw commands.
         */
        [[nodiscard]] std::uint32_t maxDrawCount() const noexcept;

        /**
         * @brief Get draw indirect commands in the buffer, either as <tt>vk::DrawIndirectCommand</tt> or <tt>vk::DrawIndexedIndirectCommand</tt> based on the \p indexed property.
         * @return A span of draw commands.
         */
        [[nodiscard]] std::variant<std::span<const vk::DrawIndirectCommand>, std::span<const vk::DrawIndexedIndirectCommand>> drawIndirectCommands() const noexcept;

        /**
         * @copydoc drawIndirectCommands()
         */
        [[nodiscard]] std::variant<std::span<vk::DrawIndirectCommand>, std::span<vk::DrawIndexedIndirectCommand>> drawIndirectCommands() noexcept;

        /**
         * @brief Reset the draw count to the number of commands in the buffer.
         */
        void resetDrawCount() noexcept;

        /**
         * @brief Record draw command based on \p drawIndirectCount feature availability.
         * @param cb Command buffer to be recorded.
         * @param drawIndirectCount Whether to use <tt>vkCmdDraw(Indexed)IndirectCount</tt> or <tt>vkCmdDraw(Indexed)Indirect</tt>.
         */
        void recordDrawCommand(vk::CommandBuffer cb, bool drawIndirectCount) const;
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

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::uint32_t vk_gltf_viewer::vulkan::buffer::IndirectDrawCommands::drawCount() const noexcept {
    return asValue<const std::uint32_t>();
}

void vk_gltf_viewer::vulkan::buffer::IndirectDrawCommands::setDrawCount(std::uint32_t drawCount) {
    if (drawCount > maxDrawCount()) {
        throw std::invalid_argument { "drawCount > maxDrawCount" };
    }

    asValue<std::uint32_t>() = drawCount;
}

std::uint32_t vk_gltf_viewer::vulkan::buffer::IndirectDrawCommands::maxDrawCount() const noexcept {
    return (size - sizeof(std::uint32_t)) / (indexed ? sizeof(vk::DrawIndexedIndirectCommand) : sizeof(vk::DrawIndirectCommand));
}

std::variant<std::span<const vk::DrawIndirectCommand>, std::span<const vk::DrawIndexedIndirectCommand>> vk_gltf_viewer::vulkan::buffer::IndirectDrawCommands::drawIndirectCommands() const noexcept {
    if (indexed) {
        return asRange<const vk::DrawIndexedIndirectCommand>(sizeof(std::uint32_t));
    }
    else {
        return asRange<const vk::DrawIndirectCommand>(sizeof(std::uint32_t));
    }
}

std::variant<std::span<vk::DrawIndirectCommand>, std::span<vk::DrawIndexedIndirectCommand>> vk_gltf_viewer::vulkan::buffer::IndirectDrawCommands::drawIndirectCommands() noexcept {
    if (indexed) {
        return asRange<vk::DrawIndexedIndirectCommand>(sizeof(std::uint32_t));
    }
    else {
        return asRange<vk::DrawIndirectCommand>(sizeof(std::uint32_t));
    }
}

void vk_gltf_viewer::vulkan::buffer::IndirectDrawCommands::resetDrawCount() noexcept {
    setDrawCount(maxDrawCount());
}

void vk_gltf_viewer::vulkan::buffer::IndirectDrawCommands::recordDrawCommand(vk::CommandBuffer cb, bool drawIndirectCount) const {
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