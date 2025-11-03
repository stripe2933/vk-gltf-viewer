#version 460
#extension GL_ARB_shader_viewport_layer_array : require

const uint indices[] = {
    2, 6, 7, 2, 3, 7, 0, 4, 5, 0, 1, 5, 0, 2, 6, 0, 4, 6,
    1, 3, 7, 1, 5, 7, 0, 2, 3, 0, 1, 3, 4, 6, 7, 4, 5, 7,
};

const vec3 positions[] = {
    { -1.0, -1.0, -1.0 },
    { -1.0, -1.0,  1.0 },
    { -1.0,  1.0, -1.0 },
    { -1.0,  1.0,  1.0 },
    {  1.0, -1.0, -1.0 },
    {  1.0, -1.0,  1.0 },
    {  1.0,  1.0, -1.0 },
    {  1.0,  1.0,  1.0 }
};

layout (location = 0) out vec3 outPosition;

layout (set = 0, binding = 0) uniform CameraBuffer {
    layout (offset = 256) mat4 translationlessProjectionViews[4];
} camera;

void main() {
    outPosition = positions[indices[gl_VertexIndex]];
    gl_Position = (camera.translationlessProjectionViews[gl_InstanceIndex] * vec4(outPosition, 1.0));
    gl_Position.z = 0.0; // Use reverse Z.
    gl_ViewportIndex = gl_InstanceIndex;
}