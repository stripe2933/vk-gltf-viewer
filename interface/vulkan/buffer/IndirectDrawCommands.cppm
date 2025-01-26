export module vk_gltf_viewer:vulkan.buffer.IndirectDrawCommands;

import std;
export import fastgltf;
export import :gltf.AssetPrimitiveInfo;
import :helpers.concepts;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
export import :vulkan.Gpu;

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

    export template <
        std::invocable<const gltf::AssetPrimitiveInfo&> CriteriaGetter,
        typename Criteria = std::invoke_result_t<CriteriaGetter, const gltf::AssetPrimitiveInfo&>,
        typename Compare = std::less<Criteria>>
    [[nodiscard]] std::map<Criteria, std::variant<IndirectDrawCommands<false>, IndirectDrawCommands<true>>, Compare> createIndirectDrawCommandBuffers(
        const fastgltf::Asset &asset,
        vma::Allocator allocator,
        const CriteriaGetter &criteriaGetter,
        const std::unordered_set<std::uint16_t> &nodeIndices,
        concepts::compatible_signature_of<const gltf::AssetPrimitiveInfo&, const fastgltf::Primitive&> auto const &primitiveInfoGetter
    ) {
        std::map<Criteria, std::variant<std::vector<vk::DrawIndirectCommand>, std::vector<vk::DrawIndexedIndirectCommand>>> commandGroups;

        for (std::uint16_t nodeIndex : nodeIndices) {
            const fastgltf::Node &node = asset.nodes[nodeIndex];
            if (!node.meshIndex) {
                continue;
            }

            // EXT_mesh_gpu_instancing support.
            std::uint32_t instanceCount = 1;
            if (!node.instancingAttributes.empty()) {
                instanceCount = asset.accessors[node.instancingAttributes[0].accessorIndex].count;
            }

            const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
            for (const fastgltf::Primitive &primitive : mesh.primitives) {
                const gltf::AssetPrimitiveInfo &primitiveInfo = primitiveInfoGetter(primitive);
                const Criteria criteria = criteriaGetter(primitiveInfo);
                if (const auto &indexInfo = primitiveInfo.indexInfo) {
                    const std::size_t indexByteSize = [=]() {
                        switch (indexInfo->type) {
                            case vk::IndexType::eUint8KHR: return sizeof(std::uint8_t);
                            case vk::IndexType::eUint16: return sizeof(std::uint16_t);
                            case vk::IndexType::eUint32: return sizeof(std::uint32_t);
                            default: std::unreachable();
                        }
                    }();

                    auto &commandGroup = commandGroups
                        .try_emplace(criteria, std::in_place_type<std::vector<vk::DrawIndexedIndirectCommand>>)
                        .first->second;
                    const std::uint32_t firstIndex = static_cast<std::uint32_t>(primitiveInfo.indexInfo->offset / indexByteSize);
                    get_if<std::vector<vk::DrawIndexedIndirectCommand>>(&commandGroup)
                        ->emplace_back(
                            primitiveInfo.drawCount, instanceCount, firstIndex, 0,
                            (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveInfo.index));
                }
                else {
                    auto &commandGroup = commandGroups
                        .try_emplace(criteria, std::in_place_type<std::vector<vk::DrawIndirectCommand>>)
                        .first->second;
                    get_if<std::vector<vk::DrawIndirectCommand>>(&commandGroup)
                        ->emplace_back(
                            primitiveInfo.drawCount, instanceCount, 0,
                            (static_cast<std::uint32_t>(nodeIndex) << 16U) | static_cast<std::uint32_t>(primitiveInfo.index));
                }
            }
        }

        using result_type = std::variant<IndirectDrawCommands<false>, IndirectDrawCommands<true>>;
        return commandGroups
            | ranges::views::value_transform([allocator](const auto &variant) {
                return visit(multilambda {
                    [allocator](std::span<const vk::DrawIndirectCommand> commands) {
                        return result_type {
                            std::in_place_type<IndirectDrawCommands<false>>,
                            allocator,
                            commands,
                        };
                    },
                    [allocator](std::span<const vk::DrawIndexedIndirectCommand> commands) {
                        return result_type {
                            std::in_place_type<IndirectDrawCommands<true>>,
                            allocator,
                            commands,
                        };
                    },
                }, variant);
            })
            | std::ranges::to<std::map<Criteria, result_type, Compare>>();
    }
}