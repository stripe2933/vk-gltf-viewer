#if __APPLE__ && __clang_major__ >= 21
// Workaround for https://github.com/llvm/llvm-project/commit/17d05695388128353662fbb80bbb7a13d172b41d
// This change is introduced in libc++21, but not applied to the AppleClang libc++ yet.
// We need to manually provide the definition of __hash_memory.
// TODO: remove it when fixed.

#include <__config> // _LIBCPP_VERSION

#if _LIBCPP_VERSION >= 210000
#include <functional> // __murmur2_or_cityhash

_LIBCPP_BEGIN_NAMESPACE_STD

size_t __hash_memory(_LIBCPP_NOESCAPE const void* ptr, size_t size) noexcept {
    return __murmur2_or_cityhash<size_t>()(ptr, size);
}

_LIBCPP_END_NAMESPACE_STD
#endif
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <vulkan/vulkan_hpp_macros.hpp>

import vulkan_hpp;

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE