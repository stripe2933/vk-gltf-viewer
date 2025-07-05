#pragma once

#if defined(_MSC_VER)
#define LIFETIMEBOUND [[msvc::lifetimebound]]
#elif defined(__clang__)
#define LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define LIFETIMEBOUND
#endif