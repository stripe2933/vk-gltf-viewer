module;

#include <format>
#include <source_location>

#include <shaderc/shaderc.hpp>

export module vku:pipelines.Shader;

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
        const char *entryPoint = "main";

        Shader(
            const shaderc::Compiler &compiler,
            std::string_view glsl,
            vk::ShaderStageFlagBits stage,
            const char *entryPoint = "main",
            const char *identifier = std::format("{}", std::source_location{}).c_str()
        );
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
    }
    throw std::runtime_error { std::format("Unsupported shader stage: {}", to_string(stage)) };
}

vku::Shader::Shader(
    const shaderc::Compiler &compiler,
    std::string_view glsl,
    vk::ShaderStageFlagBits stage,
    const char *entryPoint,
    const char *identifier
) : stage { stage },
    entryPoint { entryPoint } {
    const auto result = compiler.CompileGlslToSpv(
        glsl.data(), glsl.size(),
        getShaderKind(stage),
        entryPoint, identifier, {});
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error { std::format("Failed to compile shader: {}", result.GetErrorMessage()) };
    }

    code = { std::from_range, result };
}