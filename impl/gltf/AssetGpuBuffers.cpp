module;

#include <cassert>
#include <vulkan/vulkan_hpp_macros.hpp>

module vk_gltf_viewer;
import :gltf.AssetGpuBuffers;

import std;
import :helpers.fastgltf;
import :helpers.functional;
import :helpers.ranges;
import :vulkan.buffer;

#define FWD(...) static_cast<decltype(__VA_ARGS__) &&>(__VA_ARGS__)
#define LIFT(...) [&](auto &&...xs) { return __VA_ARGS__(FWD(xs)...); }

bool vk_gltf_viewer::gltf::AssetGpuBuffers::updatePrimitiveMaterial(
    const fastgltf::Primitive &primitive,
    std::uint32_t materialIndex,
    vk::CommandBuffer transferCommandBuffer
) {
    const std::uint16_t orderedPrimitiveIndex = primitiveInfos.at(&primitive).index;
    const std::uint32_t paddedMaterialIndex = materialBuffer.get().padMaterialIndex(materialIndex);
    return std::visit(multilambda {
        [&](vku::MappedBuffer &primitiveBuffer) {
            primitiveBuffer.asRange<GpuPrimitive>()[orderedPrimitiveIndex].materialIndex = paddedMaterialIndex;
            return false;
        },
        [&](vk::Buffer primitiveBuffer) {
            transferCommandBuffer.updateBuffer(
                primitiveBuffer,
                sizeof(GpuPrimitive) * orderedPrimitiveIndex + offsetof(GpuPrimitive, materialIndex),
                sizeof(GpuPrimitive::materialIndex),
                &paddedMaterialIndex);
            return true;
        }
    }, primitiveBuffer);
}

std::vector<const fastgltf::Primitive*> vk_gltf_viewer::gltf::AssetGpuBuffers::createOrderedPrimitives() const {
    return asset.meshes
        | std::views::transform(&fastgltf::Mesh::primitives)
        | std::views::join
        | ranges::views::addressof
        | std::ranges::to<std::vector>();
}

auto vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveInfos() const -> std::unordered_map<const fastgltf::Primitive*, AssetPrimitiveInfo> {
    return orderedPrimitives
        | ranges::views::enumerate
        | ranges::views::decompose_transform([](std::uint16_t primitiveIndex, const fastgltf::Primitive *pPrimitive) {
            return std::pair {
                pPrimitive,
                AssetPrimitiveInfo {
                    .index = primitiveIndex,
                },
            };
        })
        | std::ranges::to<std::unordered_map>();
}

std::variant<vku::AllocatedBuffer, vku::MappedBuffer> vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveBuffer() {
    vku::MappedBuffer buffer {
        gpu.allocator,
        std::from_range, orderedPrimitives | std::views::transform([this](const fastgltf::Primitive *pPrimitive) {
            const AssetPrimitiveInfo &primitiveInfo = primitiveInfos[pPrimitive];

            // If normal and tangent not presented (nullopt), it will use a faceted mesh renderer, and they will does not
            // dereference those buffers. Therefore, it is okay to pass nullptr into shaders
            const auto normalInfo = primitiveInfo.normalInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});
            const auto tangentInfo = primitiveInfo.tangentInfo.value_or(AssetPrimitiveInfo::AttributeBufferInfo{});

            // If color is not presented, it is not used in the shader. Therefore, it is okay to pass nullptr into shaders.
            const auto colorInfo = primitiveInfo.colorInfo.value_or(AssetPrimitiveInfo::ColorAttributeBufferInfo{});

            return GpuPrimitive {
                .pPositionBuffer = primitiveInfo.positionInfo.address,
                .pNormalBuffer = normalInfo.address,
                .pTangentBuffer = tangentInfo.address,
                .pTexcoordAttributeMappingInfoBuffer = primitiveInfo.texcoordsInfo.pMappingBuffer,
                .pColorBuffer = colorInfo.address,
                .positionByteStride = primitiveInfo.positionInfo.byteStride,
                .normalByteStride = normalInfo.byteStride,
                .tangentByteStride = tangentInfo.byteStride,
                .colorByteStride = colorInfo.byteStride,
                .materialIndex = to_optional(pPrimitive->materialIndex).transform(LIFT(materialBuffer.get().padMaterialIndex)).value_or(0U),
            };
        }),
        gpu.isUmaDevice ? vk::BufferUsageFlagBits::eStorageBuffer : vk::BufferUsageFlagBits::eTransferSrc,
    };
    if (!needStaging(buffer)) {
        return std::variant<vku::AllocatedBuffer, vku::MappedBuffer> { std::in_place_type<vku::MappedBuffer>, std::move(buffer) };
    }

    vku::AllocatedBuffer unmappedBuffer = std::move(buffer).unmap();
    stage(unmappedBuffer, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
    return unmappedBuffer;
}

void vk_gltf_viewer::gltf::AssetGpuBuffers::createPrimitiveIndexedAttributeMappingBuffers() {
    // Collect primitives that have any TEXCOORD attributes.
    const std::vector primitiveWithTexcoordAttributeInfos
        = primitiveInfos
        | std::views::values
        | std::views::filter([](const AssetPrimitiveInfo &primitiveInfo) { return !primitiveInfo.texcoordsInfo.attributeInfos.empty(); })
        | std::views::transform([](AssetPrimitiveInfo &primitiveInfo) { return std::tie(primitiveInfo, primitiveInfo.texcoordsInfo.attributeInfos); })
        | std::ranges::to<std::vector>();

    if (primitiveWithTexcoordAttributeInfos.empty()) {
        return;
    }

    auto [buffer, copyOffsets] = vulkan::buffer::createCombinedBuffer<true>(
        gpu.allocator,
        primitiveWithTexcoordAttributeInfos | std::views::values,
        gpu.isUmaDevice
            ? vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress
            : vk::BufferUsageFlagBits::eTransferSrc);
    stageIfNeeded(buffer, vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);

    const vk::DeviceAddress pIndexAttributeMappingBuffer = gpu.device.getBufferAddress({ buffer });
    for (auto &&[primitiveInfo, copyOffset] : std::views::zip(primitiveWithTexcoordAttributeInfos | std::views::keys, copyOffsets)) {
        primitiveInfo.texcoordsInfo.pMappingBuffer = pIndexAttributeMappingBuffer + copyOffset;
    }

    internalBuffers.emplace_back(std::move(buffer));
}

bool vk_gltf_viewer::gltf::AssetGpuBuffers::needStaging(const vku::AllocatedBuffer &buffer) const noexcept {
    return !gpu.isUmaDevice
        && !vku::contains(gpu.allocator.getAllocationMemoryProperties(buffer.allocation), vk::MemoryPropertyFlagBits::eDeviceLocal);
}

void vk_gltf_viewer::gltf::AssetGpuBuffers::stage(vku::AllocatedBuffer &buffer, vk::BufferUsageFlags usage) {
    vku::AllocatedBuffer deviceLocalBuffer { gpu.allocator, vk::BufferCreateInfo {
        {},
        buffer.size,
        usage,
    } };
    stagingInfos.emplace_back(
        std::move(buffer),
        deviceLocalBuffer,
        vk::BufferCopy { 0, 0, deviceLocalBuffer.size });
    buffer = std::move(deviceLocalBuffer);
}

void vk_gltf_viewer::gltf::AssetGpuBuffers::stageIfNeeded(vku::AllocatedBuffer &buffer, vk::BufferUsageFlags usage) {
    if (needStaging(buffer)) {
        stage(buffer, usage);
    }
}
