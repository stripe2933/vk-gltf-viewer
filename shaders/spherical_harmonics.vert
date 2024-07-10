#version 450

const vec3[] positions = vec3[8](
vec3(-1.0, -1.0, -1.0),
vec3(-1.0, -1.0,  1.0),
vec3(-1.0,  1.0, -1.0),
vec3(-1.0,  1.0,  1.0),
vec3( 1.0, -1.0, -1.0),
vec3( 1.0, -1.0,  1.0),
vec3( 1.0,  1.0, -1.0),
vec3( 1.0,  1.0,  1.0)
);

layout (location = 0) out vec3 fragPosition;

layout (push_constant) uniform PushConstant {
    mat4 projectionView;
} pc;

void main() {
    fragPosition = positions[gl_VertexIndex];
    gl_Position = (pc.projectionView * vec4(fragPosition, 1.0));
    gl_Position.z = 0.0; // Use reverse Z.
}