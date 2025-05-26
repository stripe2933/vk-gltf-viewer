module;

#include <vulkan/vulkan_hpp_macros.hpp>

export module vk_gltf_viewer:vulkan.buffer.CombinedDrawIndirectCommands;

import std;
export import fastgltf;
export import vku;
import :helpers.concepts;
import :helpers.functional;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)

template <typename T, typename... Ts>
concept one_of = (std::same_as<T, Ts> || ...);

namespace vk_gltf_viewer::vulkan::buffer {
    /**
     * @brief Vulkan buffer that has capacity of all needed <tt>vk::Draw(Indexed)IndirectCommand</tt> for rendering the
     * asset, in tight-packed layout.
     *
     * Its constructor accepts <tt>fastgltf::Asset</tt> and estimate the all node-mesh primitive combinations, and
     * allocate memory of the estimated size. Then, you can call <tt>fillCommands</tt> method to fill the draw indirect
     * commands that are separated by the given key.
     *
     * Indexed/non-indexed draw commands are stored in the separate region of the buffer. The layout is like the below:
     * (DIC = <tt>vk::DrawIndirectCommand</tt>, DIIC = <tt>vk::DrawIndexedIndirectCommand</tt>)
     *
     * <-     maxDrawIndirectCommandCount    -> <-    maxDrawIndexedIndirectCommandCount    ->
     * <- drawIndirectCommandCount ->           <- drawIndexedIndirectCommandCount ->
     * +-------------------------------------------------------------------------------------+
     * | DIC0 | DIC1 | ... | DIC<N> |          | DIIC0  |  DIIC1  |  ...  |  DIIC<M> |       |
     * +-------------------------------------------------------------------------------------+
     */
    export class CombinedDrawIndirectCommands : public vku::MappedBuffer {
        std::uint32_t maxDrawIndirectCommandCount;
        std::uint32_t maxDrawIndexedIndirectCommandCount;

        std::uint32_t drawIndirectCommandCount = 0;
        std::uint32_t drawIndexedIndirectCommandCount = 0;

    public:
        struct Segment {
            vk::Buffer buffer;
            void *mapped;
            vk::DeviceSize offset;
            bool indexed;
            std::uint32_t maxDrawCount;
            std::uint32_t drawCount = maxDrawCount;

            [[nodiscard]] std::variant<std::span<const vk::DrawIndirectCommand>, std::span<const vk::DrawIndexedIndirectCommand>> getDrawIndirectCommands() const noexcept {
                if (indexed) {
                    return std::span { reinterpret_cast<const vk::DrawIndexedIndirectCommand*>(static_cast<const char*>(mapped) + offset), maxDrawCount };
                }
                else {
                    return std::span { reinterpret_cast<const vk::DrawIndirectCommand*>(static_cast<const char*>(mapped) + offset), maxDrawCount };
                }
            }

            [[nodiscard]] std::variant<std::span<vk::DrawIndirectCommand>, std::span<vk::DrawIndexedIndirectCommand>> getDrawIndirectCommands() noexcept {
                if (indexed) {
                    return std::span { reinterpret_cast<vk::DrawIndexedIndirectCommand*>(static_cast<char*>(mapped) + offset), maxDrawCount };
                }
                else {
                    return std::span { reinterpret_cast<vk::DrawIndirectCommand*>(static_cast<char*>(mapped) + offset), maxDrawCount };
                }
            }

            /**
             * @brief Reset the draw count to the number of commands in the buffer.
             */
            void resetDrawCount() noexcept {
                drawCount = maxDrawCount;
            }

            /**
             * @brief Record draw commands.
             * @param cb Command buffer to be recorded.
             */
            void recordDrawCommands(vk::CommandBuffer cb) const {
                if (indexed) {
                    cb.drawIndexedIndirect(buffer, offset, drawCount, sizeof(vk::DrawIndexedIndirectCommand));
                }
                else {
                    cb.drawIndirect(buffer, offset, drawCount, sizeof(vk::DrawIndirectCommand));
                }
            }
        };

        CombinedDrawIndirectCommands(std::uint32_t maxDrawIndirectCommandCount, std::uint32_t maxDrawIndexedIndirectCommandCount, vma::Allocator allocator)
            : MappedBuffer { allocator, vk::BufferCreateInfo {
                {},
                sizeof(vk::DrawIndirectCommand) * maxDrawIndirectCommandCount + sizeof(vk::DrawIndexedIndirectCommand) * maxDrawIndexedIndirectCommandCount,
                vk::BufferUsageFlagBits::eIndirectBuffer,
            }, vku::allocation::hostRead }
            , maxDrawIndirectCommandCount { maxDrawIndirectCommandCount }
            , maxDrawIndexedIndirectCommandCount { maxDrawIndexedIndirectCommandCount } { }

        template <std::invocable<std::size_t, const fastgltf::Primitive&> DrawCommandGetter>
        std::array<Segment, 2> fillCommands(
            const fastgltf::Asset &asset,
            std::ranges::input_range auto &&nodeIndices,
            DrawCommandGetter &&drawCommandGetter
        ) {
            drawIndirectCommandCount = 0;
            drawIndexedIndirectCommandCount = 0;

            const vk::DeviceSize indexedDrawCommandBytesOffset = maxDrawIndirectCommandCount * sizeof(vk::DrawIndirectCommand);
            auto drawIndirectCommandIt = asRange<vk::DrawIndirectCommand>().subspan(0, maxDrawIndirectCommandCount).begin();
            auto drawIndexedIndirectCommandIt = asRange<vk::DrawIndexedIndirectCommand>(indexedDrawCommandBytesOffset).begin();

            const multilambda pushDrawCommand {
                [&](const vk::DrawIndirectCommand &command) {
                    *drawIndirectCommandIt++ = command;
                    ++drawIndirectCommandCount;
                },
                [&](const vk::DrawIndexedIndirectCommand &command) {
                    *drawIndexedIndirectCommandIt++ = command;
                    ++drawIndexedIndirectCommandCount;
                },
            };

            for (std::size_t nodeIndex : FWD(nodeIndices)) {
                const fastgltf::Node &node = asset.nodes[nodeIndex];
                if (!node.meshIndex) continue;

                const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    if constexpr (one_of<std::invoke_result_t<DrawCommandGetter, std::size_t, const fastgltf::Primitive&>, vk::DrawIndirectCommand, vk::DrawIndexedIndirectCommand>) {
                        pushDrawCommand(std::invoke(drawCommandGetter, nodeIndex, primitive));
                    }
                    else {
                        visit(pushDrawCommand, std::invoke(drawCommandGetter, nodeIndex, primitive));
                    }
                }
            }

            return {
                Segment { *this, data, 0, false, drawIndirectCommandCount },
                Segment { *this, data, indexedDrawCommandBytesOffset, true, drawIndexedIndirectCommandCount },
            };
        }

        template <
            std::invocable<const fastgltf::Primitive&> KeyGetter,
            typename Key = std::invoke_result_t<KeyGetter, const fastgltf::Primitive&>,
            typename Compare = std::less<Key>>
        [[nodiscard]] std::map<Key, Segment, Compare> fillCommands(
            const fastgltf::Asset &asset,
            std::ranges::input_range auto &&nodeIndices,
            KeyGetter &&keyGetter,
            concepts::signature_of<std::variant<vk::DrawIndirectCommand, vk::DrawIndexedIndirectCommand>(std::size_t, const fastgltf::Primitive&)> auto &&drawCommandGetter
        ) {
            drawIndirectCommandCount = 0;
            drawIndexedIndirectCommandCount = 0;

            std::map<Key, std::pair<bool /* indexed? */, std::vector<std::byte>>> groupedDrawCommandBytes;
            for (std::size_t nodeIndex : FWD(nodeIndices)) {
                const fastgltf::Node &node = asset.nodes[nodeIndex];
                if (!node.meshIndex) continue;

                const fastgltf::Mesh &mesh = asset.meshes[*node.meshIndex];
                for (const fastgltf::Primitive &primitive : mesh.primitives) {
                    auto &[indexed, drawCommandBytes] = groupedDrawCommandBytes[std::invoke(keyGetter, primitive)];
                    visit(multilambda {
                        [&](const vk::DrawIndirectCommand &command) {
                            indexed = false;
                            drawCommandBytes.append_range(as_bytes(std::span<const vk::DrawIndirectCommand, 1> { &command, 1 }));
                            ++drawIndirectCommandCount;
                        },
                        [&](const vk::DrawIndexedIndirectCommand &command) {
                            indexed = true;
                            drawCommandBytes.append_range(as_bytes(std::span<const vk::DrawIndexedIndirectCommand, 1> { &command, 1 }));
                            ++drawIndexedIndirectCommandCount;
                        },
                    }, std::invoke(drawCommandGetter, nodeIndex, primitive));
                }
            }

            const vk::DeviceSize indexedDrawCommandBytesOffset = maxDrawIndirectCommandCount * sizeof(vk::DrawIndirectCommand);
            const auto drawCommandBytesStart = asRange<std::byte>().subspan(0, indexedDrawCommandBytesOffset).begin();
            auto drawCommandBytesIt = drawCommandBytesStart;
            const auto indexedDrawCommandBytesStart = asRange<std::byte>(indexedDrawCommandBytesOffset).begin();
            auto indexedDrawCommandBytesIt = indexedDrawCommandBytesStart;

            std::map<Key, Segment, Compare> result;
            for (auto &[key, indexedAndDrawCommandBytes] : std::move(groupedDrawCommandBytes)) {
                const auto &[indexed, drawCommandBytes] = indexedAndDrawCommandBytes;
                if (indexed) {
                    result.try_emplace(
                        std::move(key),
                        buffer,
                        data,
                        std::distance(indexedDrawCommandBytesStart, indexedDrawCommandBytesIt),
                        true,
                        drawCommandBytes.size() / sizeof(vk::DrawIndexedIndirectCommand));
                    indexedDrawCommandBytesIt = std::ranges::copy(drawCommandBytes, indexedDrawCommandBytesIt).out;
                }
                else {
                    result.try_emplace(
                        std::move(key),
                        buffer,
                        data,
                        std::distance(drawCommandBytesStart, drawCommandBytesIt),
                        false,
                        drawCommandBytes.size() / sizeof(vk::DrawIndirectCommand));
                    drawCommandBytesIt = std::ranges::copy(drawCommandBytes, drawCommandBytesIt).out;
                }
            }

            return result;
        }
    };
}