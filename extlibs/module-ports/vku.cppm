module;

#include <cassert>
#include <version>

#include <vulkan/vulkan_hpp_macros.hpp>

export module vku;

import std;
export import vulkan;
export import vk_mem_alloc;

#if !defined(VMA_HPP_NAMESPACE)
#define VMA_HPP_NAMESPACE vma
#endif
#if !defined(VMA_HPP_RAII_NAMESPACE)
#define VMA_HPP_RAII_NAMESPACE raii
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winclude-angled-in-module-purview"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5244) // Including header in the purview of module 'vku' appears erroneous.
#endif

#define VKU_USE_MODULE
#define VKU_EXPORT export
#define VKU_IMPLEMENTATION
#include <vku.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif