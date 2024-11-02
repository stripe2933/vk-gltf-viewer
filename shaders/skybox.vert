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

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

void main() {
    outPosition = positions[gl_VertexIndex];
    gl_Position = (pc.projectionView * vec4(outPosition, 1.0));
    gl_Position.z = 0.0; // Use reverse Z.
}