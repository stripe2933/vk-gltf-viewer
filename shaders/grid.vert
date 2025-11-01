#version 460
#extension GL_ARB_shader_viewport_layer_array : require

const vec2 uvs[] = {
    { -0.5, -0.5 },
    {  0.5, -0.5 },
    {  0.5,  0.5 },
    { -0.5, -0.5 },
    {  0.5,  0.5 },
    { -0.5,  0.5 },
};

layout (location = 0) out vec2 outUV;
layout (location = 1) flat out float outLogLodFract;

layout (set = 0, binding = 0) uniform CameraBuffer {
    mat4 projectionViews[4];
    layout (offset = 512) vec3 positions[4];
} camera;

layout (push_constant) uniform PushConstant {
    layout (offset = 16) float size;
} pc;

void main() {
    outUV = pc.size * uvs[gl_VertexIndex];
    gl_Position = camera.projectionViews[gl_InstanceIndex] * vec4(outUV, 0.0, 1.0).xzyw;
    gl_ViewportIndex = gl_InstanceIndex;

    float logLod = log(camera.positions[gl_InstanceIndex].y) / log(10);
    outLogLodFract = fract(-logLod);
    outUV *= pow(10.0, floor(-logLod));
}