module;

#include <vulkan/vulkan_hpp_macros.hpp>

#include <lifetimebound.hpp>

export module vk_gltf_viewer.vulkan.buffer.PrimitiveAttributes;

import std;
export import BS.thread_pool;
export import vkgltf;

export import vk_gltf_viewer.gltf.AssetExtended;
export import vk_gltf_viewer.vulkan.Gpu;

namespace vk_gltf_viewer::vulkan::buffer {
    export
    [[nodiscard]] std::unordered_map<const fastgltf::Primitive*, vkgltf::PrimitiveAttributeBuffers> createPrimitiveAttributeBuffers(
        const gltf::AssetExtended &assetExtended LIFETIMEBOUND,
        const Gpu &gpu LIFETIMEBOUND,
        vkgltf::StagingBufferStorage &stagingBufferStorage,
        BS::thread_pool<> &threadPool
    );

}

#if !defined(__GNUC__) || defined(__clang__)
module :private;
#endif

std::unordered_map<const fastgltf::Primitive*, vkgltf::PrimitiveAttributeBuffers> vk_gltf_viewer::vulkan::buffer::createPrimitiveAttributeBuffers(
    const gltf::AssetExtended &assetExtended,
    const Gpu &gpu,
    vkgltf::StagingBufferStorage &stagingBufferStorage,
    BS::thread_pool<> &threadPool
) {
    const vkgltf::PrimitiveAttributeBuffers::AttributeInfoCache cache {
        assetExtended.asset,
        gpu.allocator,
        vkgltf::PrimitiveAttributeBuffers::AttributeInfoCache::Config {
            .adapter = assetExtended.externalBuffers,
            .maxTexcoordAttributeCount = 4,
            .maxJointsAttributeCount = std::numeric_limits<std::size_t>::max(),
            .maxWeightsAttributeCount = std::numeric_limits<std::size_t>::max(),
            .usageFlagsFn = [](const fastgltf::Accessor &accessor) -> std::optional<vk::BufferUsageFlags> {
                if (accessor.sparse) return std::nullopt;
                return vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc;
            },
        },
    };

    const vkgltf::PrimitiveAttributeBuffers::Config config {
        .adapter = assetExtended.externalBuffers,
        .cache = &cache,
        .maxTexcoordAttributeCount = 4,
        .maxJointsAttributeCount = std::numeric_limits<std::size_t>::max(),
        .maxWeightsAttributeCount = std::numeric_limits<std::size_t>::max(),
        .mikkTSpaceTangentComponentType = std::nullopt, // will be generated manually with threadPool
        .usageFlags = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
    };

    // -----
    // Generate vkgltf::PrimitiveAttributeBuffers per primitive to result and collect primitives that need generated
    // MikkTSpace tangents
    // -----

    std::unordered_map<const fastgltf::Primitive*, vkgltf::PrimitiveAttributeBuffers> result;
    std::vector<const fastgltf::Primitive*> primitiveNeedsMikkTSpaceTangents;
    for (const fastgltf::Mesh &mesh : assetExtended.asset.meshes) {
        for (const fastgltf::Primitive &primitive : mesh.primitives) {
            if (primitive.findAttribute("POSITION") == primitive.attributes.end()) {
                // glTF 2.0 specification:
                //   When positions are not specified, client implementations SHOULD skip primitive’s rendering unless its
                //   positions are provided by other means (e.g., by an extension). This applies to both indexed and
                //   non-indexed geometry.
                //   https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#meshes-overview
                continue;
            }

            const vkgltf::PrimitiveAttributeBuffers &emplaced = result.try_emplace(
                &primitive,
                assetExtended.asset, primitive, gpu.allocator, config).first->second;
            if (emplaced.needMikkTSpaceTangents()) {
                primitiveNeedsMikkTSpaceTangents.push_back(&primitive);
            }
        }
    }

    // ----- Generate MikkTSpace tangents with threads -----

    threadPool.submit_loop(0, primitiveNeedsMikkTSpaceTangents.size(), [&](std::size_t i) {
        const fastgltf::Primitive *primitive = primitiveNeedsMikkTSpaceTangents[i];
        result.at(primitive).emplaceMikkTSpaceTangents(
            fastgltf::ComponentType::Byte,
            gpu.allocator,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
            gpu.queueFamilies.uniqueIndices,
            vma::AllocationCreateInfo {
                vma::AllocationCreateFlagBits::eHostAccessSequentialWrite,
                vma::MemoryUsage::eAutoPreferHost,
            },
            assetExtended.externalBuffers);
    }).wait();

    // ----- Collect distinct buffers for staging -----

    std::unordered_set<vku::AllocatedBuffer*> distinctBuffers;
    for (const vkgltf::PrimitiveAttributeBuffers &attributeBuffers : result | std::views::values) {
        // POSITION
        distinctBuffers.emplace(attributeBuffers.position.attributeInfo.buffer.get());
        for (const auto &info : attributeBuffers.position.morphTargets) {
            distinctBuffers.emplace(info.buffer.get());
        }

        // NORMAL
        if (attributeBuffers.normal) {
            distinctBuffers.emplace(attributeBuffers.normal->attributeInfo.buffer.get());
            for (const auto &info : attributeBuffers.normal->morphTargets) {
                distinctBuffers.emplace(info.buffer.get());
            }

            // TANGENT
            if (attributeBuffers.tangent) {
                distinctBuffers.emplace(attributeBuffers.tangent->attributeInfo.buffer.get());
                for (const auto &info : attributeBuffers.tangent->morphTargets) {
                    distinctBuffers.emplace(info.buffer.get());
                }
            }
        }

        // TEXCOORD_<i>
        for (const auto &info : attributeBuffers.texcoords) {
            distinctBuffers.emplace(info.attributeInfo.buffer.get());
        }

        // COLOR_0
        if (!attributeBuffers.colors.empty()) {
            distinctBuffers.emplace(attributeBuffers.colors[0].attributeInfo.buffer.get());
        }

        // JOINTS_<i>
        for (const auto &info : attributeBuffers.joints) {
            distinctBuffers.emplace(info.buffer.get());
        }

        // WEIGHTS_<i>
        for (const auto &info : attributeBuffers.weights) {
            distinctBuffers.emplace(info.buffer.get());
        }
    }

    for (vku::AllocatedBuffer *buffer : distinctBuffers) {
        stagingBufferStorage.stage(*buffer, vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress);
    }

    return result;
}