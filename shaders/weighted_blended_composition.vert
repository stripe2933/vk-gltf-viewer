#version 460

const vec2 positions[] = {
    { -1.0, 3.0 },
    { -1.0, -1.0 },
    { 3.0, -1.0 }
};

void main(){
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}