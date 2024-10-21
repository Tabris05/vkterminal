#version 460

#define EPSILON 0.000001f
#define NUM_COLS 10u

layout(location = 0) in vec3 inNorm;
layout(location = 0) out uint fragChar;

const uint palette[NUM_COLS] = { 32u, 46u, 58u, 45u, 61u, 43u, 42u, 35u, 37u, 64u };

void main() {
    float color = max(dot(inNorm, vec3(0.7071f, -0.7071f, 0.0f)), 0.0f) + 0.1f;
    fragChar = palette[uint(min(color * NUM_COLS, NUM_COLS - 1u))];
}