#pragma once

#include <cstddef>

#define ASSERT_ALIGNMENT(Type, Member) static_assert(offsetof(Type, Member) % sizeof(Type::Member) == 0);