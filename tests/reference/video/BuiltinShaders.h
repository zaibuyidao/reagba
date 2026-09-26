#pragma once
#include <string>

namespace reagba::shaders {
// Frozen pre-migration render reference. Used by tests only; never linked into the core extension.
// LCD3x: Gigaherz's public-domain sine mask, default scanline/LCD brightness 16/4.
// https://github.com/libretro/glsl-shaders/blob/master/handheld/shaders/lcd3x.glsl
// LCD grid v2: implementation of cgwg's integrated subpixel profile model, at
// default RGB primaries, input/output gamma 3/2.2, gain 1, black level .05.
// https://github.com/libretro/glsl-shaders/blob/master/handheld/shaders/lcd-cgwg/lcd-grid-v2.glsl
// Profile integrals below are evaluated in Horner form; no RetroArch frontend code
// or external shader loader is included. Opaque output and clamped edge fetches
// are intentional for ReaGBA's native framebuffer.
inline constexpr char Math[] = R"SHADER(
vec3 lcd3x(vec2 uv) {
    const float pi = 3.141592654;
    vec2 phase = uv * sourceSize * (2.0 * pi);
    vec3 mask = (4.0 + sin(phase.x + pi * vec3(0.5, -1.0/6.0, -5.0/6.0))) / 5.0;
    return sampleFrame(uv) * mask * ((16.0 + sin(phase.y)) / 17.0);
}

// Antiderivatives of the squared horizontal and vertical subpixel profiles:
// (1-z^2-z^4+z^6)^2 and (1-2z^4+z^6)^2, supported on [-1,1].
vec3 profileIntegral(vec3 z, bool horizontal) {
    vec3 q = z*z;
    if (horizontal)
        return z*(1.0+q*(-2.0/3.0+q*(-1.0/5.0+q*(4.0/7.0+q*(-1.0/9.0+q*(-2.0/11.0+q/13.0))))));
    return z*(1.0+q*q*(-4.0/5.0+q*(2.0/7.0+q*(4.0/9.0+q*(-4.0/11.0+q/13.0)))));
}
vec3 coverage(vec3 distance, float footprint, float radius, bool horizontal) {
    vec3 lo = clamp((distance - footprint*0.5) / radius, -1.0, 1.0);
    vec3 hi = clamp((distance + footprint*0.5) / radius, -1.0, 1.0);
    return max((profileIntegral(hi, horizontal) - profileIntegral(lo, horizontal)) * (radius/footprint),
               vec3(0.0, 0.0, 0.0));
}
vec3 lcdLight(ivec2 texel) {
    vec3 value = fetchFrame(texel) + 0.05;
    return value * value * value;
}
vec3 lcdGrid(vec2 uv) {
    vec2 position = uv*sourceSize - 0.4999;
    ivec2 origin = ivec2(floor(position));
    vec2 fraction = position - vec2(origin);
    // Physical output pixels, not the WebView's CSS size (important on HiDPI).
    vec2 footprint = sourceSize / max(outputSize, vec2(1.0, 1.0));
    vec3 leftMask = coverage(fraction.x*3.0 + vec3(1.0, 0.0, -1.0), footprint.x*3.0, 1.5, true);
    vec3 rightMask = coverage(fraction.x*3.0 + vec3(-2.0, -3.0, -4.0), footprint.x*3.0, 1.5, true);
    vec3 rows = coverage(vec3(fraction.y, fraction.y-1.0, 0.0), footprint.y, 0.63, false);
    vec3 upper = lcdLight(origin)*leftMask + lcdLight(origin+ivec2(1,0))*rightMask;
    vec3 lower = lcdLight(origin+ivec2(0,1))*leftMask + lcdLight(origin+ivec2(1,1))*rightMask;
    return pow(max(upper*rows.x + lower*rows.y, vec3(0.0,0.0,0.0)), vec3(1.0/2.2,1.0/2.2,1.0/2.2));
}
vec3 shadeFrame(vec2 uv) {
    if (shaderPreset == 1) return lcd3x(uv);
    if (shaderPreset == 2) return lcdGrid(uv);
    return sampleFrame(uv);
}
)SHADER";

// D3D constant buffers are multiples of 16 bytes; keep this layout in sync below.
struct Uniforms {
    float sourceWidth, sourceHeight, outputWidth, outputHeight;
    int preset;
    float padding[3]{};
};
static_assert(sizeof(Uniforms) == 32);

inline constexpr char HLSLVertex[] = R"SHADER(
struct Vertex {float4 position:SV_POSITION; float2 uv:TEXCOORD;};
Vertex vs(uint id:SV_VertexID) {
    Vertex v;
    v.uv = float2((id<<1)&2, id&2);
    v.position = float4(v.uv*float2(2,-2)+float2(-1,1),0,1);
    return v;
}
)SHADER";
inline std::string HLSLFragment() {
    return std::string(R"SHADER(
#define vec2 float2
#define vec3 float3
#define ivec2 int2
cbuffer Video : register(b0) {float2 sourceSize; float2 outputSize; int shaderPreset; float3 padding;};
Texture2D frame : register(t0);
SamplerState sampling : register(s0);
float3 sampleFrame(float2 uv) {return frame.Sample(sampling, uv).rgb;}
float3 fetchFrame(int2 texel) {return frame.Load(int3(clamp(texel, int2(0,0), int2(sourceSize)-1), 0)).rgb;}
)SHADER") + Math + R"SHADER(
float4 ps(float4 position:SV_POSITION, float2 uv:TEXCOORD):SV_TARGET {
    return float4(shadeFrame(uv), 1.0);
}
)SHADER";
}
inline constexpr char GLSLVertex[] = R"SHADER(#version 150
out vec2 uv;
void main() {
    uv = vec2((gl_VertexID<<1)&2, gl_VertexID&2);
    gl_Position = vec4(uv*vec2(2,-2)+vec2(-1,1),0,1);
}
)SHADER";
inline std::string GLSLFragment() {
    return std::string(R"SHADER(#version 150
in vec2 uv;
uniform sampler2D frame;
uniform vec2 sourceSize;
uniform vec2 outputSize;
uniform int shaderPreset;
out vec4 color;
vec3 sampleFrame(vec2 coordinate) {return texture(frame, coordinate).rgb;}
vec3 fetchFrame(ivec2 texel) {return texelFetch(frame, clamp(texel, ivec2(0), ivec2(sourceSize)-1), 0).rgb;}
)SHADER") + Math + R"SHADER(
void main() {color = vec4(shadeFrame(uv), 1.0);}
)SHADER";
}
}
