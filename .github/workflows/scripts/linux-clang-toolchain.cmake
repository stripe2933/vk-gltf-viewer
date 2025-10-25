include($ENV{VCPKG_ROOT}/scripts/toolchains/linux.cmake)

set(CMAKE_C_COMPILER /usr/bin/clang-19)
set(CMAKE_CXX_COMPILER /usr/bin/clang++-19)