#version 460
#extension GL_EXT_multiview : require

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

layout (set = 0, binding = 0) uniform Camera {
    mat4 translationlessProjectionViews[4];
};

void main() {
    outPosition = positions[gl_VertexIndex];
    gl_Position = (translationlessProjectionViews[gl_ViewIndex] * vec4(outPosition, 1.0));
    gl_Position.z = 0.0; // Use reverse Z.
}