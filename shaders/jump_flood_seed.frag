#version 450

layout (location = 0) out uvec2 outCoordinate;

layout (early_fragment_tests) in;

void main(){
    outCoordinate = uvec2(gl_FragCoord.xy);
}