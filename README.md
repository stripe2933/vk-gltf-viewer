# Vulkan glTF Viewer

![Windows](https://github.com/stripe2933/vk-gltf-viewer/actions/workflows/windows.yml/badge.svg)
![macOS](https://github.com/stripe2933/vk-gltf-viewer/actions/workflows/macos.yml/badge.svg)
![Linux](https://github.com/stripe2933/vk-gltf-viewer/actions/workflows/linux.yml/badge.svg)

Blazingly fast[^1] Vulkan glTF viewer.

![Main screenshot](docs/images/screenshot.png)

## Features

- Support glTF 2.0, including:
  - PBR material rendering with runtime IBL resources (spherical harmonics + pre-filtered environment map) generation from input equirectangular map.
  - Support both indexed and non-indexed primitive.
    - Runtime missing tangent attribute generation using MikkTSpace algorithm for indexed primitive.
    - Runtime missing per-face normal and tangent attribute generation using tessellation shader (if supported) or screen-space fragment shader for non-indexed primitive.
  - No limits for `TEXCOORD_<i>` attributes: **can render a primitive that has arbitrary number of texture coordinates.**
  - Support `OPAQUE`, `MASKED` (using alpha testing and Alpha To Coverage) and `BLEND` (using Weighted Blended OIT) materials.
  - Support multiple scenes.
  - Support binary format (`.glb`).
- Use 4x MSAA by default.
- Load glTF asset and equirectangular map image using native file dialog.
- Pixel perfect node selection and transformation usin gizmo.
- Arbitrary sized outline rendering using [Jump Flooding Algorithm](https://en.wikipedia.org/wiki/Jump_flooding_algorithm).
- Conditionally render a node with three-state scene hierarchy view.
- GUI for used asset resources (buffers, images, samplers, etc.) list with docking support.

Followings are not supported:

- Primitive Type except for `TRIANGLES`.
- Animation.
- Normalized/Sparse accessors.

## Performance

Click the below image will redirect the page to the YouTube video (due to the GitHub's policy, I cannot directly embed the video into markdown).

[![Desktop glTF Application Performance Comparison](http://img.youtube.com/vi/MJ6-TLp0vtk/0.jpg)](https://youtu.be/MJ6-TLp0vtk)

See [Performance Comparison](docs/performance-comparison.md) page for the performance comparison with other desktop glTF viewers.

I initially developed this application for leveraging Vulkan's performance and using some modern GPU features, like bindless/GPU driven rendering/etc. There are two main goals for this application: speed and memory consumption. Here are some key points:

### Speed

- Fully bindless rendering: no descriptor set update/vertex buffer binding during rendering.
  - Descriptor sets are only updated at the model loading time.
  - Textures are accessed with runtime-descriptor indexing using `VK_EXT_descriptor_indexing` extension.
  - Use Vertex Pulling with [`VK_KHR_buffer_device_address`](https://docs.vulkan.org/samples/latest/samples/extensions/buffer_device_address/README.html). Only index buffers are bound to the command buffer.
- Fully GPU driven rendering: uses multi draw indirect with optimally sorted rendering order. **Regardless of the material count and scene's complexity, all scene nodes can be rendered with up to 24 draw calls** in the worst case.
  - Has 6 pipelines for 3 material types (Opaque, Masked, Blend) and 2 primitive types (Indexed, Non-Indexed) combinations.
  - Indexed primitive index type can be either `UNSIGNED_BYTE` (if GPU supports `VK_KHR_index_type_uint8`), `UNSIGNED_SHORT` or `UNSIGNED_INT`, and each type requires a single draw call.
  - Each material can be either double-sided or not, and cull mode have to be set based on this.
  - Therefore, if scene consists of the primitives of all combinations, it requires 24 draw calls. **Of course, it would be ~6 draw calls in most case.**
- Significant less asset loading time: **glTF buffer memories are directly `memcpy`ed into the GPU memory with dedicated transfer queue. No pre-processing is required!**
  - Thanks to the vertex pulling, pipeline is vertex input state agnostic, therefore no pre-processing is required.
  - For downside, it does not support normalized texture coordinate accessors and sparse accessors.
- **Asynchronous IBL resources generation using only compute shader**: cubemap generation (including mipmapping), spherical harmonics calculation and prefiltered map generation are done in compute shader, which can be done with the graphics operation in parallel.
  - Use subgroup operation to directly generate 5 mipmaps in a single dispatch with L2 cache friendly way (if you're wondering about this, here's [my repository](https://github.com/stripe2933/mipmap) which explains the method in detail).
  - Use subgroup operation to reduce the spherical harmonics.
- Multithreaded image decoding and MikkTSpace tangent attribute generation.
- Used frames in flight to stabilize the FPS.

### Memory Consumption

- Primary rendering pass is done with multiple subpasses, which makes most of the used attachment images memoryless. If your GPU is tile based, only jump flood images and swapchain images would be existed in the physical memory.
- Use explicit queue family ownership transfer and avoid `VK_IMAGE_USAGE_STORAGE_BIT` flags for the images, which can [enable the Delta Color Compression (DCC) in the AMD GPUs](https://gpuopen.com/presentations/2019/Vulkanised2019_06_optimising_aaa_vulkan_title_on_desktop.pdf) (I've not tested this in an AMD GPUs).
- As mentioned in above, direct copying the buffer data can reduce the memory footprint during the loading time.
- After IBL resource generation, equirectangular map and cubemap image sizes are reduced with pre-color correction. This leads up to ~4x smaller GPU memory usage if you're using higher resolution cubemap.

## Usage

### Vulkan Requirements

The extensions and feature used in this application are quite common in the modern desktop GPU drivers, so I hope you don't have any problem with this.

> [!TIP]
> My primary development environment is Apple M1 Pro, so if you're MoltenVK user, you'll be able to run this application.

<details>
<summary>Show requirements list</summary>

- Vulkan 1.2 or later
- Device Extensions
  - `VK_KHR_dynamic_rendering`
  - `VK_KHR_synchronization2`
  - `VK_EXT_extended_dynamic_state` (dynamic state cull mode)
  - `VK_KHR_swapchain`
  - `VK_KHR_swapchain_mutable_format` (proper ImGui gamma correction)
- Device Features
  - `VkPhysicalDeviceFeatures`
    - `samplerAnistropy`
    - `shaderInt64`
    - `multiDrawIndirect`
    - `shaderStorageImageWriteWithoutFormat`
    - `independentBlend` (Weighted Blended OIT)
    - (optional) `tessellationShader` (if not presented, non-indexed primitive's per-face normal/tangent will be generated in fragment shader)
  - `VkPhysicalDeviceVulkan11Features`
    - `shaderDrawParameters` (use `gl_BaseInstance` for primitive index)
    - `storageBuffer16BitAccess`
    - `uniformAndStorageBuffer16BitAccess`
    - `multiview` (reducing cubemap image precision with color correction)
  - `VkPhysicalDeviceVulkan12Features`
    - `bufferDeviceAddress`
    - `descriptorIndexing`
    - `descriptorBindingSampledImageUpdateAfterBind`
    - `descriptorBindingStorageImageUpdateAfterBind`
    - `runtimeDescriptorArray`
    - `storageBuffer8BitAccess`
    - `uniformAndStorageBuffer8BitAccess`
    - `storagePushConstant8`
    - `scalarBlockLayout`
    - `timelineSemaphore`
    - `shaderInt8`
  - `VkPhysicalDeviceDynamicRenderingFeatures`
  - `VkPhysicalDeviceSynchronization2Features`
  - `VkPhysicalDeviceExtendedDynamicStateFeaturesEXT`
  - (optional) `VkPhysicalDeviceIndexTypeUint8FeaturesKHR` (if not presented, unsigned byte primitive indices will re-generated with `uint16_t`s)
- Device Limits
  - Subgroup size must be at least 16 and 64 at maximum.
  - Sampler anisotropy must support 16x.
  - Loading asset texture count must be less than `maxDescriptorSetUpdateAfterBindSampledImages`
  - Cubemap size must be less than `2^(maxDescriptorSetUpdateAfterBindStorageImages)`.

</details>

### Build

This project requires support for C++20 modules and the C++23 standard library. The supported compiler is:
- Clang 18.1.2
- MSVC 19.40

The following build tools are required:
- CMake 3.30
- Ninja 1.11

Additionally, you need vcpkg for dependency management. Make sure `VCPKG_ROOT` environment variable is defined as your vcpkg source directory path!

#### Dependencies

This project depends on:
- [GLFW](https://github.com/glfw/glfw)
- [glm](https://github.com/g-truc/glm)
- [fastgltf](https://github.com/spnda/fastgltf)
- [ImGui](https://github.com/ocornut/imgui)
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)
- [MikkTSpace](http://www.mikktspace.com)
- [Native File Dialog Extended](https://github.com/btzy/nativefiledialog-extended)
- [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h)
- [boost-container](https://www.boost.org/doc/libs/1_86_0/doc/html/container.html) (it uses modular boost.)
- My own Vulkan-Hpp helper library, [vku](https://github.com/stripe2933/vku/tree/module) (branch `module`), which has the following dependencies:
  - [Vulkan-Hpp](https://github.com/KhronosGroup/Vulkan-Hpp)
  - [VulkanMemoryAllocator-Hpp](https://github.com/YaaZ/VulkanMemoryAllocator-Hpp)

Dependencies will be automatically fetched via vcpkg.

#### Build Steps

> [!TIP]
> This project uses GitHub Runner to ensure build compatibility on Windows (with MSVC), macOS and Linux (with Clang), with dependency management handled by vcpkg. You can check the workflow files in the [.github/workflows](.github/workflows) folder.

First, you have to clone the repository.

```sh
git clone https://github.com/stripe2933/vk-gltf-viewer
cd vk-gltf-viewer
```

##### 1. MSVC + Windows

The CMake preset is given by default.

```sh
cmake --preset=default
cmake --build build -t vk-gltf-viewer
```

The executable will be located in `build` folder.

##### 2. Clang + Linux

Install `libc++-dev` and `libc++abi-dev` that are matched to your Clang compiler.

```sh
sudo apt install libc++-dev libc++abi-dev
```

Add the following CMake user preset file in your project directory. I'll assume your Clang compiler executable is at `/usr/bin/`.

`CMakeUserPresets.json`
```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "linux-clang-18",
      "displayName": "Clang",
      "inherits": "default",
      "cacheVariables": {
        "CMAKE_C_COMPILER": "/usr/bin/clang-18",
        "CMAKE_CXX_COMPILER": "/usr/bin/clang++-18",
        "CMAKE_CXX_FLAGS": "-stdlib=libc++",
        "CMAKE_EXE_LINKER_FLAGS": "-stdlib=libc++ -lc++abi",
        "VCPKG_OVERLAY_TRIPLETS": "${sourceDir}/triplets",
        "VCPKG_TARGET_TRIPLET": "x64-linux-clang"
      }
    }
  ]
}
```

`VCPKG_OVERLAY_TRIPLETS` and `VCPKG_TARGET_TRIPLET` configuration parameters are mandatory for make vcpkg uses Clang compiler instead of the system default compiler. Add following vcpkg toolchain and triplet files.

`clang-toolchain.cmake`
```cmake
set(CMAKE_C_COMPILER /usr/bin/clang-18)
set(CMAKE_CXX_COMPILER /usr/bin/clang++-18)
set(CMAKE_CXX_FLAGS "-stdlib=libc++")
set(CMAKE_EXE_LINKER_FLAGS "-stdlib=libc++ -lc++abi")
```

`triplets/x64-linux-clang.cmake`
```cmake
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../clang-toolchain.cmake)
```

Configure and build the project with `linux-clang-18` configuration preset.

```sh
cmake --preset=linux-clang-18
cmake --build build -t vk-gltf-viewer
```

The executable will be located in `build` folder.

##### 3. Clang + macOS

Add the following CMake user preset file in your project directory. I'll assume your Clang compiler executable is at `/opt/homebrew/opt/llvm/bin/`.

`CMakeUserPresets.json`
```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "clang-arm64-macos",
      "inherits": "default",
      "binaryDir": "${sourceDir}/build",
      "cacheVariables": {
        "CMAKE_C_COMPILER": "/opt/homebrew/opt/llvm/bin/clang",
        "CMAKE_CXX_COMPILER": "/opt/homebrew/opt/llvm/bin/clang++",
        "CMAKE_CXX_FLAGS": "-nostdinc++ -nostdlib++ -isystem /opt/homebrew/opt/llvm/include/c++/v1",
        "CMAKE_EXE_LINKER_FLAGS": "-L /opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++ -lc++",
        "VCPKG_OVERLAY_TRIPLETS": "${sourceDir}/triplets",
        "VCPKG_TARGET_TRIPLET": "arm64-macos-clang"
      }
    }
  ]
}
```

`VCPKG_OVERLAY_TRIPLETS` and `VCPKG_TARGET_TRIPLET` configuration parameters are mandatory for make vcpkg uses Clang compiler instead of the system default compiler. Add following vcpkg toolchain and triplet files.

`clang-toolchain.cmake`
```cmake
set(CMAKE_C_COMPILER /opt/homebrew/opt/llvm/bin/clang)
set(CMAKE_CXX_COMPILER /opt/homebrew/opt/llvm/bin/clang++)
set(CMAKE_CXX_FLAGS "-nostdinc++ -nostdlib++ -isystem /opt/homebrew/opt/llvm/include/c++/v1")
set(CMAKE_EXE_LINKER_FLAGS "-L /opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++ -lc++")
```

`triplets/arm64-macos-clang.cmake`
```cmake
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Darwin)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE ${CMAKE_CURRENT_LIST_DIR}/../clang-toolchain.cmake)
```

Configure and build the project with `clang-arm64-macos` configuration preset.

```sh
cmake --preset=clang-arm64-macos
cmake --build build -t vk-gltf-viewer
```

The executable will be located in `build` folder.

#### Shader compilation

All shaders are located in the [shaders](/shaders) folder and need to be manually compiled into `<shader-filename>.spv` before the application launch. To make this easier, script files are available:

- Run [`compile.sh`](/shaders/compile.sh) script if you're using bash.
- Run [`compile.bat`](/shaders/compile.bat) batch file if you're using Windows.

## Milestones

- [ ] Basis Universal texture support (`KHR_texture_basisu`). (currently working)
- [ ] Reduce skybox memory usage with BC6H compressed cubemap.
- [ ] Frustum/occlusion culling.
- [ ] Automatic camera position adjustment based on the bounding sphere calculation.
- [ ] Animations.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE.txt) file for details.

[^1]: I like this term because it's hilarious for several reasons, but it's no joke! It has the **significantly faster glTF model loading speed than the other the viewers** I've tested. See [Performance Comparison](https://github.com/stripe2933/vk-gltf-viewer/blob/master/docs/performance-comparison.md) page for details.