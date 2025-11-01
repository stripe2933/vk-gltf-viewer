#version 460

const vec3[] positions = {
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
    outPosition = positions[gl_VertexIndex];
    gl_Position = (camera.translationlessProjectionViews[gl_InstanceIndex] * vec4(outPosition, 1.0));
    gl_Position.z = 0.0; // Use reverse Z.
}