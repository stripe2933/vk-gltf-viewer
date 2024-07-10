module;

#include <shaderc/shaderc.hpp>

export module vku:pipelines.Shader;

import std;
export import vulkan_hpp;

template <>
struct std::formatter<std::source_location> : formatter<std::string_view> {
    constexpr auto format(std::source_location srcLoc, auto &ctx) const {
        return format_to(ctx.out(), "{}:{}:{}", srcLoc.file_name(), srcLoc.line(), srcLoc.column());
    }
};

namespace vku {
    export struct Shader {
        vk::ShaderStageFlagBits stage;
        std::vector<std::uint32_t> code;
        const char *entryPoint;

        Shader(
            const shaderc::Compiler &compiler,
            std::string_view glsl,
            vk::ShaderStageFlagBits stage,
            const char *entryPoint = "main",
            const char *identifier = std::format("{}", std::source_location{}).c_str()
        );
        Shader(const std::filesystem::path &path, vk::ShaderStageFlagBits stage, const char *entryPoint = "main");
    };
}

// module:private;

[[nodiscard]] constexpr auto getShaderKind(vk::ShaderStageFlagBits stage) -> shaderc_shader_kind {
    switch (stage) {
        case vk::ShaderStageFlagBits::eVertex:
            return shaderc_glsl_vertex_shader;
        case vk::ShaderStageFlagBits::eTessellationControl:
            return shaderc_glsl_tess_control_shader;
        case vk::ShaderStageFlagBits::eTessellationEvaluation:
            return shaderc_glsl_tess_evaluation_shader;
        case vk::ShaderStageFlagBits::eGeometry:
            return shaderc_glsl_geometry_shader;
        case vk::ShaderStageFlagBits::eFragment:
            return shaderc_glsl_fragment_shader;
        case vk::ShaderStageFlagBits::eCompute:
            return shaderc_glsl_compute_shader;
        default:
            throw std::runtime_error { std::format("Unsupported shader stage: {}", to_string(stage)) };
    }
}

[[nodiscard]] auto loadFileAsBinary(const std::filesystem::path &path) -> std::vector<std::uint32_t> {
    std::ifstream file { path, std::ios::binary };
    if (!file) {
        throw std::runtime_error { std::format("Failed to open file: {} (error code={})", std::strerror(errno), errno) };
    }

    file.seekg(0, std::ios::end);
    const auto fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<std::uint32_t> result(fileSize / sizeof(std::uint32_t));
    file.read(reinterpret_cast<char*>(result.data()), fileSize);

    return result;
}

vku::Shader::Shader(
    const shaderc::Compiler &compiler,
    std::string_view glsl,
    vk::ShaderStageFlagBits stage,
    const char *entryPoint,
    const char *identifier
) : stage { stage },
    entryPoint { entryPoint } {
    shaderc::CompileOptions compileOptions;
    // TODO: parameter option for selecting Vulkan version?
    compileOptions.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
#ifdef NDEBUG
    compileOptions.SetOptimizationLevel(shaderc_optimization_level_performance);
#endif

    const auto result = compiler.CompileGlslToSpv(
        glsl.data(), glsl.size(),
        getShaderKind(stage),
        entryPoint, identifier, compileOptions);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error { std::format("Failed to compile shader: {}", result.GetErrorMessage()) };
    }

    code = { std::from_range, result };
}

vku::Shader::Shader(
    const std::filesystem::path &path,
    vk::ShaderStageFlagBits stage,
    const char *entryPoint
) : stage { stage },
    code { loadFileAsBinary(path) },
    entryPoint { entryPoint } { }