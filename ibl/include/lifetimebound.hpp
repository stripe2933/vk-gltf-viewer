#pragma once

#if defined(_MSC_VER)
#define LIFETIMEBOUND [[msvc::lifetimebound]]
#elif defined(__clang__)
#define LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define LIFETIMEBOUND
#endif

#if __clang_major__ >= 20
#define LIFETIME_CAPTURE_BY(...) [[clang::lifetime_capture_by(__VA_ARGS__)]]
#else
#define LIFETIME_CAPTURE_BY(...)
#endif