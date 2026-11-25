#include "Menu3D.hpp"
#include <SDL_image.h>
#include <SDL_opengl.h>
#include "../Core/PathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <future>

#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif

namespace {

constexpr int kShadowSize = 2048;
constexpr int kSpotShadowSize = 512;
constexpr float kSpotNear = 0.25f, kSpotFar = 21.0f;

void CapMenuSize(int& width, int& height) {
    const double scale = std::min({1.0, 1920.0 / width, 1080.0 / height});
    width = std::max(1, static_cast<int>(std::lround(width * scale)));
    height = std::max(1, static_cast<int>(std::lround(height * scale)));
}

template<class Function>
Function GlProc(const char* name) {
    return reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
}

constexpr char kSurfaceVertex[] = R"GLSL(
#version 120
uniform mat4 shadowMatrix;
varying vec3 eyePosition;
varying vec3 worldPos;
varying vec3 worldNormal;
varying vec4 tangent;
varying vec4 baseMR;
varying vec4 occlusionNormal;
varying vec4 shadowPosition;

void main() {
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    eyePosition = vec3(gl_ModelViewMatrix * gl_Vertex);
    worldPos = gl_Vertex.xyz;
    worldNormal = gl_Normal;
    tangent = gl_MultiTexCoord4;
    baseMR = vec4(gl_MultiTexCoord0.xy, gl_MultiTexCoord1.xy);
    occlusionNormal = vec4(gl_MultiTexCoord2.xy, gl_MultiTexCoord3.xy);
    shadowPosition = shadowMatrix * gl_Vertex;
    gl_FrontColor = gl_BackColor = gl_Color;
}
)GLSL";

// Used in both direct lighting and the participating medium; world units throughout.
constexpr char kSpotFunctions[] = R"GLSL(
uniform vec4 spotPositionPower;
uniform vec4 spotDirectionSlope;
uniform mat4 spotMatrix;
uniform sampler2D spotDepth;
uniform bool hasSpotShadow;

float beamProfile(vec3 offset, float axial) {
    vec3 radial = offset - axial * spotDirectionSlope.xyz;
    float radius = max(0.001, axial * spotDirectionSlope.w);
    return (1.0 - smoothstep(0.18, 1.0, dot(radial, radial) / (radius * radius)))
         * smoothstep(0.0, 0.25, axial) * (1.0 - smoothstep(17.0, 21.0, axial));
}
float spotTransmission(vec4 clip, float bias, vec2 offset) {
    vec3 uvz = 0.5 * clip.xyz / clip.w + 0.5;
    return step(uvz.z - bias, texture2D(spotDepth, uvz.xy + offset).r);
}
vec3 spotSurface(vec3 worldPos, vec3 n, vec3 viewDirWorld, vec3 diffuse, vec3 f0, float shine) {
    if (spotPositionPower.w > 0.0) {
        vec3 offset = worldPos - spotPositionPower.xyz;
        float axial = dot(offset, spotDirectionSlope.xyz);
        float profile = beamProfile(offset, axial);
        if (profile > 0.0) {
            vec3 lightDir = -normalize(offset);
            float ndl = max(0.0, dot(n, lightDir));
            float visibility = 1.0;
            if (hasSpotShadow) {
                vec4 clip = spotMatrix * vec4(worldPos, 1.0);
                float bias = 0.00020 + 0.00045 * (1.0 - ndl);
                vec2 tap = vec2(0.65/512.0);
                visibility = 0.25 * (spotTransmission(clip, bias, tap) + spotTransmission(clip, bias, -tap)
                           + spotTransmission(clip, bias, vec2(tap.x,-tap.y)) + spotTransmission(clip, bias, vec2(-tap.x,tap.y)));
            }
            vec3 halfDir = normalize(lightDir - viewDirWorld);
            float gloss = pow(max(0.0, dot(n, halfDir)), shine) * (shine + 2.0) * 0.125;
            float energy = spotPositionPower.w * profile * visibility / (1.0 + 0.024 * dot(offset,offset));
            return vec3(0.90, 0.95, 1.0) * energy * ndl * (diffuse + f0 * gloss);
        }
    }

    return vec3(0.0);
}
)GLSL";

constexpr char kSurfaceFragment[] = R"GLSL(
#version 120
/*SPOT*/
uniform sampler2D baseMap;
uniform sampler2D metallicRoughnessMap;
uniform sampler2D occlusionMap;
uniform sampler2D normalMap;
uniform sampler2D shadowMap;
uniform bool hasShadow;
uniform vec4 maps;
uniform vec4 factors;
uniform vec4 sceneWeights; // x=ruins, y=desert, z=mirage, w=lightningFlash
uniform vec4 envParams;    // x=timeSec, y=topDown, z=fade, w=0
uniform vec3 cameraPos;
uniform int alphaBlend;
varying vec3 eyePosition;
varying vec3 worldPos;
varying vec3 worldNormal;
varying vec4 tangent;
varying vec4 baseMR;
varying vec4 occlusionNormal;
varying vec4 shadowPosition;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float fbm2(vec2 p) {
    float v = noise2(p) * 0.65;
    v += noise2(p * 2.13 + vec2(17.3, 9.1)) * 0.35;
    return v;
}

vec2 rainRippleGrad(vec2 uv, float t) {
    vec2 g = vec2(0.0);
    for (int k = 0; k < 2; ++k) {
        float scale = k == 0 ? 1.55 : 2.45;
        vec2 p = uv * scale + vec2(float(k) * 11.3, float(k) * 5.7);
        vec2 cell = floor(p);
        vec2 f = fract(p);
        float seed = hash12(cell + float(k) * 37.0);
        float phase = fract(t * (1.25 + 0.45 * seed) + seed);
        vec2 center = vec2(0.25 + 0.5 * hash12(cell + 1.7), 0.25 + 0.5 * hash12(cell + 9.2));
        vec2 diff = f - center;
        float dist = length(diff);
        float radius = phase * 0.46;
        float ring = exp(-abs(dist - radius) * 28.0) * (1.0 - phase) * sin((dist - radius) * 52.0);
        g += (diff / max(dist, 0.001)) * ring * 0.22;
    }
    return g;
}

float shadowVisibility(float cosLight) {
    if (!hasShadow) return 1.0;
    vec3 p = shadowPosition.xyz / shadowPosition.w * 0.5 + 0.5;
    if (any(lessThanEqual(p, vec3(0.002))) || any(greaterThanEqual(p, vec3(0.998)))) return 1.0;
    float depth = p.z - max(0.00020, 0.00085 * (1.0 - cosLight));
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += step(depth, texture2D(shadowMap, p.xy + vec2(x,y) * (1.2/2048.0)).r);
    float edge = min(min(p.x, 1.0-p.x), min(p.y, 1.0-p.y));
    return mix(1.0, lit/9.0, smoothstep(0.002, 0.06, edge));
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51*x + 0.03)) / (x * (2.43*x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 agxDesert(vec3 x) {
    vec3 v = max(x, vec3(0.0));
    v = v / (v + vec3(0.58, 0.62, 0.70));
    return clamp(pow(v, vec3(1.06)) * 1.14, 0.0, 1.0);
}

vec3 agxHot(vec3 x) {
    vec3 v = max(x, vec3(0.0));
    v = v / (v + vec3(0.52, 0.58, 0.70));
    return clamp(pow(v, vec3(1.02)) * 1.18, 0.0, 1.0);
}

void main() {
    vec4 base = gl_Color;
    if (maps.x > 0.5) base *= texture2D(baseMap, baseMR.xy);

    float ruins = clamp(sceneWeights.x, 0.0, 1.0);
    float desert = clamp(sceneWeights.y, 0.0, 1.0);
    float mirage = clamp(sceneWeights.z, 0.0, 1.0);
    float lightning = clamp(sceneWeights.w, 0.0, 1.0);
    float t = envParams.x;
    float topDown = envParams.y;

    vec3 sunDirWorld = normalize(mix(vec3(-1.12, 0.71, -0.83), vec3(-1.12, 0.71, 0.55), desert));
    vec3 ray = worldPos - cameraPos;
    float worldDist = length(ray);
    vec3 viewDirWorld = ray / max(worldDist, 0.0001);

    if (alphaBlend > 0) {
        // Translucent airborne dust / mist wisps: skip shadow masking and hard dark-side falloff.
        float forwardScatter = pow(max(0.0, dot(viewDirWorld, sunDirWorld)), 2.5);
        vec3 dustTint = mix(vec3(0.76, 0.56, 0.34), vec3(1.18, 0.94, 0.64), 0.55 + 0.45 * forwardScatter);
        if (mirage > 0.01) {
            dustTint = mix(dustTint, vec3(1.15, 0.96, 0.68), mirage);
        }
        float distFade = exp(-max(0.0, worldDist - 4.0) * 0.038);
        float topDownFade = mix(1.0, 0.25, topDown);
        float alphaScale = mix(0.52, 0.34, mirage);
        gl_FragColor = vec4(agxDesert(base.rgb * dustTint), base.a * alphaScale * distFade * topDownFade);
        return;
    }

    vec2 mr = factors.xy;
    if (maps.y > 0.5) mr *= texture2D(metallicRoughnessMap, baseMR.zw).gb;
    float roughness = clamp(mr.x, 0.07, 1.0);
    float metallic = clamp(mr.y, 0.0, 1.0);
    float ao = 1.0;
    if (maps.z > 0.5) ao = mix(1.0, texture2D(occlusionMap, occlusionNormal.xy).r, factors.z);
    vec3 n = normalize(worldNormal);
    if (maps.w > 0.5) {
        vec3 tanVec = normalize(tangent.xyz - n * dot(tangent.xyz, n));
        vec3 bitan = cross(n, tanVec) * (tangent.w < 0.0 ? -1.0 : 1.0);
        vec3 detail = texture2D(normalMap, occlusionNormal.zw).rgb * 2.0 - 1.0;
        n = normalize(mat3(tanVec, bitan, n) * vec3(detail.xy * factors.w, detail.z));
    }

    // Rain Observatory: porous rain-wet darkening, puddles and animated raindrop ripples.
    float puddle = 0.0;
    float upMask = smoothstep(0.66, 0.92, n.y);
    if (ruins > 0.001) {
        float puddleField = fbm2(worldPos.xz * 0.23 + vec2(2.7, 6.1));
        puddle = smoothstep(0.45, 0.70, puddleField) * upMask * ruins;
        float wetRough = mix(0.32, 0.07, puddle);
        roughness = mix(roughness, wetRough, ruins * (0.55 * upMask + 0.45 * puddle));
        if (puddle > 0.03 && worldDist < 24.0) {
            vec2 rip = rainRippleGrad(worldPos.xz, t) * puddle * (1.0 - smoothstep(14.0, 24.0, worldDist));
            n = normalize(n + vec3(rip.x, 0.0, rip.y));
        }
    }

    vec3 N = normalize(gl_NormalMatrix * n);
    vec3 L = normalize(gl_LightSource[0].position.xyz);
    vec3 V = normalize(-eyePosition);
    vec3 H = normalize(L + V);
    float nl = max(0.0, dot(N, L));
    float fill = max(0.0, dot(N, normalize(gl_LightSource[2].position.xyz)));
    float shine = mix(6.0, 128.0, (1.0 - roughness) * (1.0 - roughness));
    vec3 f0 = mix(vec3(0.04), base.rgb, metallic);
    f0 = mix(f0, max(f0, vec3(0.065)), puddle);
    vec3 specular = f0 * pow(max(0.0, dot(N, H)), shine) * (1.6 - roughness);

    vec3 sky = gl_LightSource[0].ambient.rgb;
    vec3 groundTint = mix(vec3(0.68, 0.51, 0.36), vec3(1.22, 0.92, 0.62), desert);
    groundTint = mix(groundTint, vec3(1.36, 1.02, 0.64), mirage);
    groundTint = mix(groundTint, vec3(0.42, 0.50, 0.48), ruins);
    vec3 ground = sky * groundTint;
    vec3 ambient = mix(ground, sky, n.y * 0.5 + 0.5);

    vec3 diffuse = base.rgb * (1.0 - 0.82 * metallic);
    // Wet masonry/stone darkens & gains saturation in rain
    diffuse *= mix(1.0, mix(0.74, 0.46, puddle), ruins * (0.55 + 0.45 * upMask));

    float shadow = shadowVisibility(nl);
    float aoFactor = mix(ao, mix(0.32, 1.0, ao), max(desert, mirage) * 0.55);
    float sunScale = mix(1.0, 0.42, ruins) * mix(1.0, 1.28, mirage);
    vec3 linear = (diffuse + f0 * metallic * 0.45) * ambient * aoFactor * mix(1.0, 1.28, max(desert, mirage))
        + diffuse * gl_LightSource[2].diffuse.rgb * fill * mix(1.0, 1.15, max(desert, mirage))
        + (diffuse + specular) * gl_LightSource[0].diffuse.rgb * nl * shadow * sunScale;

    linear += spotSurface(worldPos, n, viewDirWorld, diffuse, f0, shine);

    if (ruins > 0.001) {
        // Sky reflection on wet puddles & rain-slick surfaces + thunder lightning illumination
        vec3 reflDir = reflect(viewDirWorld, n);
        float fresnel = pow(clamp(1.0 - max(0.0, dot(-viewDirWorld, n)), 0.0, 1.0), 4.0);
        vec3 wetSkyCol = mix(vec3(0.24, 0.33, 0.36), vec3(0.82, 0.92, 1.18), lightning);
        linear += wetSkyCol * fresnel * (0.14 * upMask + 0.85 * puddle) * ruins;
        float stormTop = 0.32 + 0.68 * max(0.0, n.y);
        vec3 flashSpec = vec3(0.68, 0.82, 1.12) * pow(max(0.0, reflDir.y), 3.0) * (0.12 + 0.58 * puddle);
        linear += (diffuse * vec3(0.42, 0.52, 0.74) * stormTop + flashSpec) * lightning * 0.78 * ruins;
    }

    float fog = clamp((gl_Fog.end + eyePosition.z) * gl_Fog.scale, 0.0, 1.0);
    vec3 filmic = aces(linear * 0.92);
    filmic = mix(filmic, agxDesert(linear * 1.05), desert);
    filmic = mix(filmic, agxHot(linear * 1.12), mirage);
    vec3 finalRgb = mix(gl_Fog.color.rgb, filmic, fog);

    if (ruins > 0.001) {
        // Humid rainforest rain-mist fog + lightning scatter
        float hMist = exp(-max(0.0, worldPos.y) * 0.14);
        float mistNoise = 0.75 + 0.5 * fbm2(vec2(worldPos.x * 0.12 - t * 0.22, worldPos.z * 0.12));
        float mistFactor = clamp((1.0 - exp(-max(0.0, worldDist - 2.4) * 0.022 * hMist * mistNoise)) * mix(1.0, 0.35, topDown), 0.0, 0.62);
        vec3 mistColor = mix(vec3(0.19, 0.25, 0.25), vec3(0.46, 0.58, 0.78), lightning * 0.65);
        finalRgb = mix(finalRgb, mistColor, mistFactor * ruins);
    }

    if (desert > 0.001) {
        // 4-step advected height-exponential volumetric sand haze (matches Blender ENV_Mood_Fog).
        float optDepth = 0.0;
        float stepWeights[4];
        stepWeights[0] = 0.14; stepWeights[1] = 0.38; stepWeights[2] = 0.66; stepWeights[3] = 0.90;
        for (int s = 0; s < 4; ++s) {
            vec3 p = mix(cameraPos, worldPos, stepWeights[s]);
            float hFalloff = exp(-max(0.0, p.y) * 0.24);
            vec2 advect = vec2(p.x * 0.11 - t * 0.60, p.z * 0.13 - t * 0.12 + p.y * 0.09);
            float billow = smoothstep(0.22, 0.84, fbm2(advect));
            float fineBand = noise2(vec2(p.x * 0.34 - t * 1.25, p.y * 0.48 + p.z * 0.18));
            float localDensity = hFalloff * (0.28 + 1.22 * billow + 0.36 * fineBand);
            optDepth += localDensity;
        }
        float effectiveDist = max(0.0, worldDist - 1.8);
        float topDownScale = mix(1.0, 0.26, topDown);
        float volFactor = (1.0 - exp(-effectiveDist * (optDepth * 0.25) * 0.036 * topDownScale));
        volFactor = clamp(volFactor, 0.0, 0.74);
        float mie = pow(max(0.0, dot(viewDirWorld, sunDirWorld)), 3.2);
        vec3 volColor = mix(vec3(0.66, 0.47, 0.28), vec3(1.00, 0.82, 0.54), 0.42 + 0.58 * mie);
        finalRgb = mix(finalRgb, volColor, volFactor * desert);
    }

    if (mirage > 0.001) {
        // Subtle scorching ground heat-shimmer haze concentrated low near sun-baked pavement.
        float groundHeat = exp(-max(0.0, worldPos.y) * 0.65);
        float shimmerWave = 0.75 + 0.5 * noise2(vec2(worldPos.x * 0.35, worldPos.z * 0.35 - t * 1.1));
        float heatDist = clamp((1.0 - exp(-max(0.0, worldDist - 7.5) * 0.015 * (0.35 + 0.85 * groundHeat) * shimmerWave)) * mix(1.0, 0.25, topDown), 0.0, 0.40);
        float sunForward = pow(max(0.0, dot(viewDirWorld, sunDirWorld)), 2.5);
        vec3 heatColor = mix(vec3(0.92, 0.76, 0.52), vec3(1.04, 0.92, 0.70), 0.35 + 0.65 * sunForward);
        finalRgb = mix(finalRgb, heatColor, heatDist * mirage);
    }
    gl_FragColor = vec4(finalRgb, base.a);
}
)GLSL";

constexpr char kDesertSkyVertex[] = R"GLSL(
#version 120
varying vec2 ndc;
void main() {
    ndc = gl_Vertex.xy;
    gl_Position = vec4(gl_Vertex.xy, 0.9999, 1.0);
}
)GLSL";

constexpr char kAtmosphereSkyFragment[] = R"GLSL(
#version 120
uniform vec3 camRight;
uniform vec3 camUp;
uniform vec3 camForward;
uniform vec4 skyParams;       // x=tanHalfFov*crop, y=aspect, z=timeSec, w=fade
uniform vec4 skySceneWeights; // x=ruins, y=desert, z=mirage, w=lightningFlash
uniform vec4 skyExtraParams;  // x=lightningXNorm, y=0, z=0, w=0
varying vec2 ndc;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float noise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm2(vec2 p) {
    return noise2(p) * 0.62 + noise2(p * 2.2 + vec2(11.7, 5.3)) * 0.38;
}

void main() {
    float tanY = skyParams.x;
    float aspect = skyParams.y;
    float t = skyParams.z;
    float fade = skyParams.w;
    float ruins = clamp(skySceneWeights.x, 0.0, 1.0);
    float desert = clamp(skySceneWeights.y, 0.0, 1.0);
    float mirage = clamp(skySceneWeights.z, 0.0, 1.0);
    float lightning = clamp(skySceneWeights.w, 0.0, 1.0);

    vec3 dir = normalize(camForward + camRight * (ndc.x * tanY * aspect) + camUp * (ndc.y * tanY));
    float upness = clamp(dir.y * 1.45 + 0.08, 0.0, 1.0);
    vec3 sky = vec3(0.03);

    if (ruins > 0.001) {
        vec3 rHorizon = vec3(0.18, 0.24, 0.24);
        vec3 rZenith  = vec3(0.055, 0.085, 0.105);
        vec3 rSky = mix(rHorizon, rZenith, pow(upness, 0.72));
        vec2 cUv1 = vec2(dir.x / max(0.24, dir.y + 0.32) * 1.4 + t * 0.055,
                         dir.z / max(0.24, dir.y + 0.32) * 1.4 + t * 0.015);
        vec2 cUv2 = cUv1 * 2.1 - vec2(t * 0.03, 0.0);
        float stormCloud = fbm2(cUv1) * 0.65 + fbm2(cUv2) * 0.35;
        rSky = mix(rSky, vec3(0.04, 0.058, 0.072), smoothstep(0.32, 0.78, stormCloud) * (0.4 + 0.6 * upness));
        // Cloud internal illumination during thunder lightning
        float boltDist = length(vec2(ndc.x - skyExtraParams.x, ndc.y - 0.55));
        float cloudGlow = exp(-boltDist * 2.6) * (0.45 + 0.85 * (1.0 - stormCloud));
        rSky += (vec3(0.45, 0.58, 0.85) * 0.45 + vec3(0.85, 0.92, 1.15) * cloudGlow) * lightning;
        sky += rSky * ruins;
    }

    if (desert > 0.001) {
        vec3 sunDir = normalize(vec3(-1.12, 0.71, 0.55));
        vec3 dHorizon = vec3(0.76, 0.52, 0.28);
        vec3 dZenith  = vec3(0.36, 0.30, 0.24);
        vec3 dSky = mix(dHorizon, dZenith, pow(upness, 0.65));
        float horizonBand = exp(-abs(dir.y) * 5.5);
        vec2 cloudUv = vec2(dir.x / max(0.22, dir.y + 0.28) * 1.3 - t * 0.08,
                            dir.z / max(0.22, dir.y + 0.28) * 1.3);
        float dustCloud = fbm2(cloudUv * 1.4);
        dSky = mix(dSky, vec3(0.84, 0.59, 0.33), horizonBand * (0.55 + 0.35 * dustCloud));
        dSky += vec3(0.14, 0.10, 0.055) * (dustCloud - 0.35) * (1.0 - upness * 0.5);
        float sunDot = max(0.0, dot(dir, sunDir));
        dSky += vec3(0.42, 0.27, 0.12) * pow(sunDot, 4.0) + vec3(0.75, 0.56, 0.28) * pow(sunDot, 22.0);
        sky += dSky * desert;
    }

    if (mirage > 0.001) {
        vec3 sunDir = normalize(vec3(-1.12, 0.71, -0.83));
        vec3 mHorizon = vec3(0.96, 0.78, 0.52);
        vec3 mZenith  = vec3(0.12, 0.28, 0.56);
        vec3 mSky = mix(mHorizon, mZenith, pow(upness, 0.55));
        float heatBand = exp(-abs(dir.y) * 6.5);
        float heatRipple = noise2(vec2(dir.x * 8.0, dir.y * 12.0 - t * 2.2));
        mSky = mix(mSky, vec3(1.06, 0.90, 0.66), heatBand * (0.58 + 0.22 * heatRipple));
        float sunDot = max(0.0, dot(dir, sunDir));
        mSky += vec3(0.55, 0.38, 0.18) * pow(sunDot, 5.0) + vec3(1.45, 1.12, 0.68) * pow(sunDot, 34.0);
        sky += mSky * mirage;
    }

    sky = mix(vec3(0.04, 0.045, 0.05), sky, fade);
    gl_FragColor = vec4(max(sky, vec3(0.0)), 1.0);
}
)GLSL";

constexpr char kFullscreenVertex[] = R"GLSL(
#version 120
varying vec2 vUv;
void main() {
    vUv = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)GLSL";

constexpr char kChickenVertex[] = R"GLSL(
#version 120
uniform mat4 modelToWorld;
varying vec3 worldPos;
varying vec3 worldNormal;
varying vec3 baseLight;
varying vec3 specular;
void main() {
    vec4 eye = gl_ModelViewMatrix * gl_Vertex;
    vec3 n = normalize(gl_NormalMatrix * gl_Normal);
    // Preserve the menu's ordinary per-vertex key and fill on the animated model.
    vec3 l0 = normalize(gl_LightSource[0].position.xyz - eye.xyz * gl_LightSource[0].position.w);
    vec3 l2 = normalize(gl_LightSource[2].position.xyz);
    float nl0 = max(dot(n,l0), 0.0);
    baseLight = gl_LightModel.ambient.rgb + gl_LightSource[0].ambient.rgb
              + gl_LightSource[0].diffuse.rgb * nl0 + gl_LightSource[2].diffuse.rgb * max(dot(n,l2),0.0);
    vec3 halfDir = normalize(l0 + vec3(0,0,1));
    specular = nl0 > 0.0 ? gl_FrontMaterial.specular.rgb * gl_LightSource[0].specular.rgb
             * pow(max(0.0,dot(n,halfDir)),max(1.0,gl_FrontMaterial.shininess)) : vec3(0);
    worldPos = (modelToWorld * gl_Vertex).xyz;
    worldNormal = mat3(modelToWorld) * gl_Normal;
    gl_FrontColor = gl_BackColor = gl_Color;
    gl_TexCoord[0] = gl_MultiTexCoord0;
    gl_Position = ftransform();
}
)GLSL";

constexpr char kChickenFragment[] = R"GLSL(
#version 120
/*SPOT*/
uniform sampler2D baseMap;
uniform vec4 maps;
uniform vec4 factors;
uniform vec3 cameraPos;
varying vec3 worldPos;
varying vec3 worldNormal;
varying vec3 baseLight;
varying vec3 specular;
void main() {
    vec4 texel = maps.x > 0.5 ? texture2D(baseMap, gl_TexCoord[0].xy) : vec4(1);
    vec3 base = texel.rgb * gl_Color.rgb;
    vec3 color = texel.rgb * clamp(gl_Color.rgb * baseLight + specular, 0.0, 1.0);
    vec3 direct = spotSurface(worldPos, normalize(worldNormal), normalize(worldPos-cameraPos),
        base, mix(vec3(0.04),base,factors.y), mix(6.0,128.0,pow(1.0-factors.x,2.0)));
    // Compress highlights to preserve feather and beak detail in the hotspot.
    gl_FragColor = vec4(color + (1.0-color)*direct/(1.0+direct), gl_Color.a*texel.a);
}
)GLSL";

// Interpolate the world ray per unit of camera-space depth for the current camera.
constexpr char kVolumeVertex[] = R"GLSL(
#version 120
varying vec3 ray;
varying vec2 uv;
void main() {
    vec4 eye = gl_ProjectionMatrixInverse * vec4(gl_Vertex.xy, 1.0, 1.0);
    ray = mat3(gl_ModelViewMatrixInverse) * (eye.xyz / -eye.z);
    uv = gl_Vertex.xy * 0.5 + 0.5;
    gl_Position = vec4(gl_Vertex.xy, 0.0, 1.0);
}
)GLSL";

constexpr char kVolumeFragment[] = R"GLSL(
#version 120
/*SPOT*/
uniform sampler2D sceneDepth;
uniform vec3 cameraPos;
uniform vec4 medium; // RGB scattering coefficient and animation time
varying vec3 ray;
varying vec2 uv;
void clipPlane(float distance, float velocity, inout vec2 segment) {
    if (abs(velocity) < 0.000001) {
        if (distance < 0.0) segment.y = -1.0;
    } else if (velocity > 0.0) segment.x = max(segment.x, -distance / velocity);
    else segment.y = min(segment.y, -distance / velocity);
}
void main() {
    float z = texture2D(sceneDepth, uv).r * 2.0 - 1.0;
    vec2 segment = vec2(0.1, 8.0 / (40.1 - z * 39.9));
    vec4 start = spotMatrix * vec4(cameraPos, 1.0);
    vec4 delta = spotMatrix * vec4(ray, 0.0);
    for (int axis = 0; axis < 3; ++axis) {
        clipPlane(start.w + start[axis], delta.w + delta[axis], segment);
        clipPlane(start.w - start[axis], delta.w - delta[axis], segment);
    }
    if (segment.x >= segment.y) discard;
    float stride = (segment.y - segment.x) / 6.0;
    float sum = 0.0;
    for (int i = 0; i < 6; ++i) {
        float t = segment.x + (float(i) + 0.5) * stride;
        vec3 p = cameraPos + ray * t;
        vec3 offset = p - spotPositionPower.xyz;
        float axial = dot(offset, spotDirectionSlope.xyz);
        float light = beamProfile(offset, axial) / (1.0 + 0.024 * dot(offset,offset));
        if (hasSpotShadow) light *= spotTransmission(start + delta * t, 0.00022, vec2(0.0));
        float wisps = 0.78 + 0.22 * sin(dot(p,vec3(2.7,1.2,1.7)) + medium.w * 0.6)
                                  * sin(dot(p,vec3(-1.0,1.8,2.2)) - medium.w * 0.35);
        sum += light * wisps;
    }
    gl_FragColor = vec4(medium.rgb * spotPositionPower.w * sum * stride * length(ray), 0.0);
}
)GLSL";

std::string AddSpotFunctions(std::string source) {
    constexpr char marker[] = "/*SPOT*/";
    source.replace(source.find(marker), sizeof(marker) - 1, kSpotFunctions);
    return source;
}

constexpr char kBloomExtractFragment[] = R"GLSL(
#version 120
uniform sampler2D srcTex;
uniform float threshold;
uniform vec3 tint;
varying vec2 vUv;
void main() {
    vec3 c = texture2D(srcTex, vUv).rgb;
    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));
    float w = smoothstep(threshold, threshold + 0.30, lum);
    gl_FragColor = vec4(c * w * tint, 1.0);
}
)GLSL";

constexpr char kBloomBlurFragment[] = R"GLSL(
#version 120
uniform sampler2D srcTex;
uniform vec2 texelOffset;
varying vec2 vUv;
void main() {
    vec3 sum = texture2D(srcTex, vUv).rgb * 0.227027;
    sum += (texture2D(srcTex, vUv + texelOffset * 1.384615).rgb
          + texture2D(srcTex, vUv - texelOffset * 1.384615).rgb) * 0.316216;
    sum += (texture2D(srcTex, vUv + texelOffset * 3.230769).rgb
          + texture2D(srcTex, vUv - texelOffset * 3.230769).rgb) * 0.070270;
    gl_FragColor = vec4(sum, 1.0);
}
)GLSL";

constexpr char kPostHeatFragment[] = R"GLSL(
#version 120
uniform sampler2D sceneTex;
uniform sampler2D bloomTex;
uniform vec4 postParams; // x=mirageWeight, y=timeSec, z=topDown, w=lightningFlash
uniform vec4 bloomTint;  // rgb=bloomScale, a=unused
varying vec2 vUv;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}
float noise2(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1.0, 0.0)), u.x),
               mix(hash12(i + vec2(0.0, 1.0)), hash12(i + vec2(1.0, 1.0)), u.x), u.y);
}

void main() {
    float mirage = postParams.x;
    float t = postParams.y;
    float topDown = postParams.z;

    vec2 uv = vUv;
    vec3 baseSample = texture2D(sceneTex, uv).rgb;
    // Heat refraction appears mainly just above sun-baked pavement & lower sun-heated walls,
    // not high in the cool sky or cool shaded surfaces.
    float heightBand = smoothstep(0.24, 0.38, vUv.y) * (1.0 - smoothstep(0.54, 0.72, vUv.y));
    float warmSunlit = smoothstep(0.16, 0.50, dot(baseSample, vec3(0.36, 0.34, 0.14)))
                     * smoothstep(-0.04, 0.16, baseSample.r - baseSample.b * 0.75);
    float heatMask = mirage * (1.0 - topDown * 0.85) * heightBand * (0.20 + 0.80 * warmSunlit);
    vec3 color;
    if (heatMask > 0.002) {
        // Subtle, fine convective shimmer rising off hot stone & walls
        float w1 = sin(vUv.y * 105.0 - t * 13.5 + sin(vUv.x * 46.0 - t * 4.2) * 1.4);
        float w2 = noise2(vec2(vUv.x * 52.0, vUv.y * 40.0 - t * 7.0)) * 2.0 - 1.0;
        vec2 offset = vec2((w1 * 0.55 + w2 * 0.45) * 0.00095,
                           (cos(vUv.x * 78.0 + vUv.y * 64.0 - t * 11.0) * 0.52 + w2 * 0.48) * 0.00125) * heatMask;
        vec2 disp = vec2(0.0, 0.00025 * heatMask);
        float r = texture2D(sceneTex, clamp(uv + offset + disp, vec2(0.002), vec2(0.998))).r;
        float g = texture2D(sceneTex, clamp(uv + offset, vec2(0.002), vec2(0.998))).g;
        float b = texture2D(sceneTex, clamp(uv + offset - disp, vec2(0.002), vec2(0.998))).b;
        color = vec3(r, g, b);
    } else {
        color = baseSample;
    }

    vec3 bloom = texture2D(bloomTex, vUv).rgb * bloomTint.rgb;
    color += bloom;
    gl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)GLSL";

unsigned int CompileGlslProgram(const char* vertSrc, const char* fragSrc, const char* label) {
    const auto createShader = GlProc<PFNGLCREATESHADERPROC>("glCreateShader");
    const auto shaderSource = GlProc<PFNGLSHADERSOURCEPROC>("glShaderSource");
    const auto compile = GlProc<PFNGLCOMPILESHADERPROC>("glCompileShader");
    const auto shaderStatus = GlProc<PFNGLGETSHADERIVPROC>("glGetShaderiv");
    const auto shaderLog = GlProc<PFNGLGETSHADERINFOLOGPROC>("glGetShaderInfoLog");
    const auto deleteShader = GlProc<PFNGLDELETESHADERPROC>("glDeleteShader");
    const auto createProgram = GlProc<PFNGLCREATEPROGRAMPROC>("glCreateProgram");
    const auto attach = GlProc<PFNGLATTACHSHADERPROC>("glAttachShader");
    const auto link = GlProc<PFNGLLINKPROGRAMPROC>("glLinkProgram");
    const auto programStatus = GlProc<PFNGLGETPROGRAMIVPROC>("glGetProgramiv");
    const auto programLog = GlProc<PFNGLGETPROGRAMINFOLOGPROC>("glGetProgramInfoLog");
    const auto deleteProgram = GlProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    if (!createShader || !shaderSource || !compile || !shaderStatus || !shaderLog ||
        !deleteShader || !createProgram || !attach || !link || !programStatus ||
        !programLog || !deleteProgram) {
        return 0;
    }
    const auto buildShader = [&](GLenum type, const char* source) {
        const GLuint shader = createShader(type);
        if (!shader) return GLuint{0};
        shaderSource(shader, 1, &source, nullptr);
        compile(shader);
        GLint ok = GL_FALSE;
        shaderStatus(shader, GL_COMPILE_STATUS, &ok);
        if (ok) return shader;
        char log[2048]{};
        shaderLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[Menu3D] " << label << " shader compile failed: " << log << '\n';
        deleteShader(shader);
        return GLuint{0};
    };
    const GLuint vertex = buildShader(GL_VERTEX_SHADER, vertSrc);
    if (!vertex) return 0;
    const GLuint fragment = buildShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!fragment) {
        deleteShader(vertex);
        return 0;
    }
    const GLuint program = createProgram();
    GLint linked = GL_FALSE;
    if (program) {
        attach(program, vertex);
        attach(program, fragment);
        link(program);
        programStatus(program, GL_LINK_STATUS, &linked);
    }
    deleteShader(vertex);
    deleteShader(fragment);
    if (!linked) {
        char log[2048]{};
        if (program) {
            programLog(program, sizeof(log), nullptr, log);
            deleteProgram(program);
        }
        std::cerr << "[Menu3D] " << label << " shader link failed: " << log << '\n';
        return 0;
    }
    return program;
}

constexpr float kPi = 3.14159265358979323846f;

// 主菜单相机：保持原来的高度和俯角，仅略微前移；聚焦页继续使用旧相机参数，
// 避免物体自定义界面的 3D 排版发生变化。
constexpr float kMainCamY = 1.05f;
constexpr float kMainCamZ = 8.0f;
constexpr float kTopCamY = 12.8f;
constexpr float kMainLookY = 0.95f;
constexpr float kFocusCamY = 2.00f;
constexpr float kFocusCamZ = 8.6f;
constexpr float kFocusLookY = 1.90f;
constexpr float kChickenMinZ = -2.0f;
constexpr float kChickenMaxZ = 5.5f;

// ============================================================================
// 主菜单自定义：调色板 / 姿势 / 武器 / 配件选项
// （T/CT 的调色板数组已随角色移除，2026-11-25；计数保留给 FocusOptionCount 用）
// ============================================================================

constexpr int kCharPaletteCount = 8;  // 原色 + 7 色

const char* kCharColorNames[] = { "原色", "沙色", "丛林", "雪地", "血红", "幽灵", "赛博", "粉紫" };

struct RgbColor { float r, g, b; };
// 小鸡羽色（整体覆盖，模型本身没有材质）
constexpr RgbColor kChickenColors[] = {
    { 0.94f, 0.62f, 0.12f },  // 原色（暖黄）
    { 0.93f, 0.93f, 0.90f },  // 白羽
    { 0.62f, 0.42f, 0.24f },  // 棕羽
    { 0.22f, 0.22f, 0.25f },  // 黑羽
    { 0.34f, 0.54f, 0.90f },  // 蓝羽
    { 0.40f, 0.76f, 0.38f },  // 绿羽
    { 0.88f, 0.25f, 0.22f },  // 红羽
};
constexpr int kChickenColorCount = static_cast<int>(sizeof(kChickenColors) / sizeof(kChickenColors[0]));
const char* kChickenColorNames[] = { "原色", "白羽", "棕羽", "黑羽", "蓝羽", "绿羽", "红羽" };

const char* kCharPoseNames[]  = { "待机", "下蹲", "叉腰", "抱拳" };
// kCharPoseAnims（Idle/Crouch/HandsOnHips/FistPalm）已随 T/CT 移除，2026-11-25
constexpr int kCharPoseCount = 4;

const char* kChickenPoseNames[] = { "散步", "待机", "张望", "休息" };
const char* kChickenPoseAnims[] = { "Menu_Walk", "Menu_Idle", "Menu_Look", "Menu_Rest" };
constexpr int kChickenPoseCount = 4;

const char* kWeaponOptionNames[] = { "AK-47", "冲锋枪", "狙击枪", "霰弹枪", "手枪", "匕首" };
// kWeaponOptionNodes（AK/SMG/Sniper/Shotgun/Pistol/Knife_1）已随 T/CT 移除，2026-11-25
constexpr int kWeaponOptionCount = 6;

const char* kCharAccNames[] = { "无", "头饰", "墨镜", "背包" };
constexpr int kCharAccCount = 4;
const char* kChickenAccNames[] = { "无", "小帽子", "墨镜" };
constexpr int kChickenAccCount = 3;

const char* kCharCategoryNames[] = { "武器", "颜色", "姿势", "配饰" };
const char* kChickenCategoryNames[] = { "颜色", "姿势", "配饰" };

void DrawCube(float cx, float cy, float cz,
              float sx, float sy, float sz,
              float r, float g, float b) {
    const float x0 = cx - sx * 0.5f, x1 = cx + sx * 0.5f;
    const float y0 = cy - sy * 0.5f, y1 = cy + sy * 0.5f;
    const float z0 = cz - sz * 0.5f, z1 = cz + sz * 0.5f;
    glColor3f(r, g, b);
    glBegin(GL_QUADS);
    // +X
    glNormal3f(1, 0, 0);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
    glVertex3f(x1, y1, z1); glVertex3f(x1, y1, z0);
    // -X
    glNormal3f(-1, 0, 0);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0);
    glVertex3f(x0, y1, z0); glVertex3f(x0, y1, z1);
    // +Y
    glNormal3f(0, 1, 0);
    glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
    // -Y
    glNormal3f(0, -1, 0);
    glVertex3f(x0, y0, z1); glVertex3f(x1, y0, z1);
    glVertex3f(x1, y0, z0); glVertex3f(x0, y0, z0);
    // +Z
    glNormal3f(0, 0, 1);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
    glVertex3f(x1, y1, z1); glVertex3f(x1, y0, z1);
    // -Z
    glNormal3f(0, 0, -1);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x0, y1, z0); glVertex3f(x0, y0, z0);
    glEnd();
}

void DrawSphere(float cx, float cy, float cz, float radius,
                float r, float g, float b, int slices = 12, int stacks = 8) {
    glColor3f(r, g, b);
    for (int i = 0; i < stacks; ++i) {
        const float lat0 = -kPi * 0.5f + kPi * static_cast<float>(i) / stacks;
        const float lat1 = -kPi * 0.5f + kPi * static_cast<float>(i + 1) / stacks;
        const float y0 = std::sin(lat0) * radius;
        const float y1 = std::sin(lat1) * radius;
        const float rr0 = std::cos(lat0) * radius;
        const float rr1 = std::cos(lat1) * radius;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; ++j) {
            const float lon = 2.0f * kPi * static_cast<float>(j) / slices;
            const float x = std::cos(lon), z = std::sin(lon);
            glNormal3f(x * std::cos(lat0), std::sin(lat0), z * std::cos(lat0));
            glVertex3f(cx + x * rr0, cy + y0, cz + z * rr0);
            glNormal3f(x * std::cos(lat1), std::sin(lat1), z * std::cos(lat1));
            glVertex3f(cx + x * rr1, cy + y1, cz + z * rr1);
        }
        glEnd();
    }
}


void DrawChicken(float x, float z, float phase) {
    const float bob = std::sin(phase) * 0.035f;
    const float y = 0.28f + bob;
    DrawSphere(x, y, z, 0.24f, 0.92f, 0.78f, 0.36f, 10, 6);
    DrawSphere(x, y + 0.21f, z - 0.02f, 0.14f, 0.98f, 0.86f, 0.46f, 10, 6);
    DrawCube(x, y + 0.21f, z - 0.16f, 0.09f, 0.07f, 0.10f, 0.95f, 0.48f, 0.15f);
    // comb / tail
    DrawCube(x, y + 0.36f, z, 0.06f, 0.10f, 0.10f, 0.85f, 0.20f, 0.16f);
    DrawCube(x, y + 0.02f, z + 0.22f, 0.10f, 0.18f, 0.08f, 0.75f, 0.58f, 0.22f);
}

void DrawShadow(float x, float z, float rx, float rz, float alpha) {
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);   // 影子不写深度，避免把脚底裁掉
    glColor4f(0.0f, 0.0f, 0.0f, alpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, 0.010f, z);
    constexpr int kSegments = 24;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        glVertex3f(x + std::cos(a) * rx, 0.010f, z + std::sin(a) * rz);
    }
    glEnd();
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void ApplyLookAt(const float eye[3], const float target[3], const float up[3]) {
    float fx = target[0] - eye[0];
    float fy = target[1] - eye[1];
    float fz = target[2] - eye[2];
    const float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (flen < 1e-6f) return;
    fx /= flen; fy /= flen; fz /= flen;

    float sx = fy * up[2] - fz * up[1];
    float sy = fz * up[0] - fx * up[2];
    float sz = fx * up[1] - fy * up[0];
    const float slen = std::sqrt(sx * sx + sy * sy + sz * sz);
    if (slen < 1e-6f) return;
    sx /= slen; sy /= slen; sz /= slen;

    const float ux = sy * fz - sz * fy;
    const float uy = sz * fx - sx * fz;
    const float uz = sx * fy - sy * fx;

    const float m[16] = {
        sx, ux, -fx, 0.0f,
        sy, uy, -fy, 0.0f,
        sz, uz, -fz, 0.0f,
        -(sx * eye[0] + sy * eye[1] + sz * eye[2]),
        -(ux * eye[0] + uy * eye[1] + uz * eye[2]),
         (fx * eye[0] + fy * eye[1] + fz * eye[2]),
        1.0f
    };
    glMultMatrixf(m);
}

void ApplyMainCamera(float timeSec, float outEye[3] = nullptr, float outTarget[3] = nullptr) {
    // 轻微的手持晃动：模拟人站在地上时的呼吸与重心起伏。幅度很小（≤0.05 单位），
    // 多个慢速正弦叠加（0.17–0.37 Hz），避免机械感；只影响主菜单视角，
    // 聚焦/俯视相机与场景 crossfade 都不经过这里。
    const float swayX = 0.030f * std::sin(1.445f * timeSec)
                      + 0.012f * std::sin(2.324f * timeSec + 1.3f);
    const float swayY = 0.022f * std::sin(1.194f * timeSec + 0.7f);
    const float swayZ = 0.018f * std::sin(1.822f * timeSec + 2.1f);
    const float eye[3] = { swayX, kMainCamY + swayY, kMainCamZ + swayZ };
    const float target[3] = {
        0.020f * std::sin(1.319f * timeSec + 1.9f),
        kMainLookY + 0.014f * std::sin(1.068f * timeSec + 0.4f),
        0.0f
    };
    if (outEye) std::memcpy(outEye, eye, sizeof(eye));
    if (outTarget) std::memcpy(outTarget, target, sizeof(target));
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    ApplyLookAt(eye, target, up);
}

void ApplyFocusCamera(float progress, float outEye[3] = nullptr, float outTarget[3] = nullptr) {
    const float p = std::clamp(progress, 0.0f, 1.0f);
    const float eye[3] = {
        0.0f,
        kMainCamY + (kFocusCamY - kMainCamY) * p,
        kMainCamZ + (kFocusCamZ - kMainCamZ) * p
    };
    const float target[3] = {
        0.0f,
        kMainLookY + (kFocusLookY - kMainLookY) * p,
        0.0f
    };
    if (outEye) std::memcpy(outEye, eye, sizeof(eye));
    if (outTarget) std::memcpy(outTarget, target, sizeof(target));
    const float up[3] = { 0.0f, 1.0f, 0.0f };
    ApplyLookAt(eye, target, up);
}

} // namespace

bool Menu3D::Initialize(SDL_Renderer* mainRenderer, int width, int height) {
    Shutdown();
    followOutputSize_ = width == 0 && height == 0;
    if (mainRenderer && followOutputSize_) {
        if (SDL_GetRendererOutputSize(mainRenderer, &width, &height) != 0) {
            lastError_ = std::string("SDL_GetRendererOutputSize failed: ") + SDL_GetError();
            return false;
        }
        if (width > 0 && height > 0) CapMenuSize(width, height);
    }
    if (!mainRenderer || width <= 0 || height <= 0) {
        lastError_ = "invalid renderer/size";
        return false;
    }
    mainRenderer_ = mainRenderer;
    width_ = width;
    height_ = height;

    if (!InitGL()) {
        Shutdown();
        return false;
    }

    LoadModels();

    texture_ = CreateBackdropTexture(width_, height_);
    if (!texture_) {
        lastError_ = std::string("SDL_CreateTexture failed: ") + SDL_GetError();
        Shutdown();
        return false;
    }
    pixels_.assign(static_cast<size_t>(width_) * height_ * 4, 0);
    flipped_.assign(pixels_.size(), 0);
    animationTime_ = 0.0f;
    pendingAnimationTime_ = 0.0f;
    lastRenderTicks_ = 0;
    CaptureScenePreviews();
    ready_ = true;

    std::cout << "[Menu3D] 实时 3D 背景就绪 (" << width_ << "x" << height_
              << ", GL " << reinterpret_cast<const char*>(glGetString(GL_VERSION))
              << ")" << std::endl;
    return true;
}

SDL_Texture* Menu3D::CreateBackdropTexture(int width, int height) const {
    auto* texture = SDL_CreateTexture(mainRenderer_, SDL_PIXELFORMAT_RGBA32,
                                     SDL_TEXTUREACCESS_STREAMING, width, height);
    if (texture) {
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear);
    }
    return texture;
}

void Menu3D::FollowOutputSize() {
    if (!followOutputSize_) return;
    int width = 0, height = 0;
    if (SDL_GetRenderTarget(mainRenderer_)) {
        // SDL_GetRendererOutputSize reports that target's logical size in this case.
        auto* window = SDL_RenderGetWindow(mainRenderer_);
        if (window) SDL_GetWindowSizeInPixels(window, &width, &height);
    } else if (SDL_GetRendererOutputSize(mainRenderer_, &width, &height) != 0) {
        return;
    }
    if (width <= 0 || height <= 0) return;
    CapMenuSize(width, height);
    if (width == width_ && height == height_) return;
    auto* next = CreateBackdropTexture(width, height);
    if (!next) return;

    SDL_SetWindowSize(glWindow_, width, height);
    SDL_GL_SwapWindow(glWindow_);
    SDL_DestroyTexture(texture_);
    texture_ = next;
    width_ = width;
    height_ = height;
    pixels_.resize(static_cast<size_t>(width_) * height_ * 4);
    flipped_.resize(pixels_.size());
    ResizeBloomTargets();
    ResizeSpotlightTargets();
    CaptureScenePreviews();
}

bool Menu3D::InitGL() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 1);

    const auto createWindow = [this] {
        return SDL_CreateWindow("Menu3D",
                                 SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                 width_, height_,
                                 SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL);
    };
    glWindow_ = createWindow();
    if (!glWindow_) {
        SDL_GL_SetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, 0);
        glWindow_ = createWindow();
    }
    if (!glWindow_) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        glWindow_ = createWindow();
    }
    if (!glWindow_) {
        lastError_ = std::string("SDL_CreateWindow(OpenGL) failed: ") + SDL_GetError();
        return false;
    }

    glContext_ = SDL_GL_CreateContext(glWindow_);
    if (!glContext_) {
        lastError_ = std::string("SDL_GL_CreateContext failed: ") + SDL_GetError();
        return false;
    }
    if (SDL_GL_MakeCurrent(glWindow_, glContext_) != 0) {
        lastError_ = std::string("SDL_GL_MakeCurrent failed: ") + SDL_GetError();
        return false;
    }
    SDL_GL_SetSwapInterval(0);

    glViewport(0, 0, width_, height_);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glEnable(GL_MULTISAMPLE);
    glShadeModel(GL_SMOOTH);

    const GLfloat ambient[] = { .25f, .25f, .28f, 1.0f };
    const GLfloat diffuse[] = { 0.88f, 0.84f, 0.76f, 1.0f };
    const GLfloat position[] = { -4.0f, 8.0f, 6.0f, 1.0f };
    glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    const GLfloat specular[] = {.72f,.70f,.65f,1};
    const GLfloat fill[] = {.19f,.24f,.30f,1};
    const GLfloat fillPosition[] = {4,3,-6,0};
    glLightfv(GL_LIGHT0, GL_SPECULAR, specular);
    glLightfv(GL_LIGHT2, GL_DIFFUSE, fill);
    glLightfv(GL_LIGHT2, GL_POSITION, fillPosition);
    glEnable(GL_LIGHT2);
    InitSurfaceShader();
    return true;
}

void Menu3D::InitSurfaceShader() {
    const auto deleteProgram = GlProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    const auto useProgram = GlProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
    const auto location = GlProc<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    const auto uniform1i = GlProc<PFNGLUNIFORM1IPROC>("glUniform1i");
    GLint srgb = 0;
    GLint textureCoords = 0;
    glGetIntegerv(GL_MAX_TEXTURE_COORDS, &textureCoords);
    if (SDL_GL_GetAttribute(SDL_GL_FRAMEBUFFER_SRGB_CAPABLE, &srgb) != 0 ||
        !srgb || textureCoords < 5 || !deleteProgram || !useProgram || !location || !uniform1i ||
        !GlProc<PFNGLACTIVETEXTUREPROC>("glActiveTexture") ||
        !GlProc<PFNGLCLIENTACTIVETEXTUREARBPROC>("glClientActiveTexture") ||
        !GlProc<PFNGLUNIFORM3FPROC>("glUniform3f") ||
        !GlProc<PFNGLUNIFORM4FPROC>("glUniform4f") ||
        !GlProc<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv")) {
        std::cerr << "[Menu3D] GLSL/sRGB unavailable; fixed-function lighting\n";
        return;
    }
    // The capture harness can exercise real driver compilation failure.
    const char* forceFailure = SDL_getenv("PIXELENGINE_MENU_INVALID_SHADER");
    const GLuint program = CompileGlslProgram(
        kSurfaceVertex,
        forceFailure && std::strcmp(forceFailure, "1") == 0 ? "#version 120\ninvalid shader" : AddSpotFunctions(kSurfaceFragment).c_str(),
        "Surface");
    if (!program) {
        std::cerr << "[Menu3D] Shader unavailable; fixed-function lighting\n";
        return;
    }
    glEnable(GL_FRAMEBUFFER_SRGB);
    if (!glIsEnabled(GL_FRAMEBUFFER_SRGB)) {
        deleteProgram(program);
        std::cerr << "[Menu3D] sRGB conversion unavailable; fixed-function lighting\n";
        return;
    }
    surfaceProgram_ = program;
    useProgram(program);
    const char* samplers[] = {"baseMap", "metallicRoughnessMap", "occlusionMap", "normalMap", "shadowMap"};
    for (int i = 0; i < 5; ++i) uniform1i(location(program, samplers[i]), i);
    surfaceUniforms_ = {location(program, "factors"), location(program, "maps"), location(program, "alphaBlend")};
    surfaceShadowMatrix_ = location(program, "shadowMatrix");
    surfaceHasShadow_ = location(program, "hasShadow");
    surfaceSceneWeights_ = location(program, "sceneWeights");
    surfaceEnvParams_ = location(program, "envParams");
    surfaceCameraPos_ = location(program, "cameraPos");
    useProgram(0);
    InitShadowMap(shadowFramebuffer_, shadowDepth_, kShadowSize);
    InitShadowMap(spotFramebuffer_, spotDepth_, kSpotShadowSize);
    chickenProgram_ = CompileGlslProgram(kChickenVertex, AddSpotFunctions(kChickenFragment).c_str(), "Chicken lighting");
    if (chickenProgram_) {
        chickenUniforms_ = {location(chickenProgram_, "factors"), location(chickenProgram_, "maps"), -1};
        chickenTransform_ = location(chickenProgram_, "modelToWorld");
        chickenCamera_ = location(chickenProgram_, "cameraPos");
    }
    volumeProgram_ = CompileGlslProgram(kVolumeVertex, AddSpotFunctions(kVolumeFragment).c_str(), "Spotlight volume");
    auto initSpot = [&](GLuint shader, SpotUniforms& uniforms) {
        if (!shader) return;
        useProgram(shader);
        uniform1i(location(shader, "spotDepth"), 5);
        uniform1i(location(shader, "hasSpotShadow"), spotDepth_ != 0);
        uniforms = {location(shader, "spotPositionPower"), location(shader, "spotDirectionSlope"),
                    location(shader, "spotMatrix")};
        useProgram(0);
    };
    initSpot(surfaceProgram_, surfaceSpot_);
    initSpot(chickenProgram_, chickenSpot_);
    initSpot(volumeProgram_, volumeSpot_);
    if (volumeProgram_ && spotFramebuffer_) {
        volumeCamera_ = location(volumeProgram_, "cameraPos");
        volumeMedium_ = location(volumeProgram_, "medium");
        GlProc<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers")(1, &volumeFramebuffer_);
        glGenTextures(1, &volumeTexture_);
        glGenTextures(1, &sceneDepthCopy_);
        ResizeSpotlightTargets();
    }
    InitSandstormAssets();
    InitAtmosphereShadersAndPost();
    std::cout << "[Menu3D] GLSL 1.20 with framebuffer sRGB"
              << (shadowDepth_ ? " and 2048x2048 shadows" : "") << '\n';
}

void Menu3D::InitSandstormAssets() {
    auto hash2 = [](int x, int y, unsigned int seed) -> float {
        unsigned int n = static_cast<unsigned int>(x) * 374761393u +
                         static_cast<unsigned int>(y) * 668265263u + seed * 1442695041u;
        n = (n ^ (n >> 13)) * 1274126177u;
        return static_cast<float>((n ^ (n >> 16)) & 0xFFFFu) / 65535.0f;
    };
    auto valueNoise = [&](float x, float y, unsigned int seed) -> float {
        const int ix = static_cast<int>(std::floor(x));
        const int iy = static_cast<int>(std::floor(y));
        const float fx = x - static_cast<float>(ix);
        const float fy = y - static_cast<float>(iy);
        const float ux = fx * fx * (3.0f - 2.0f * fx);
        const float uy = fy * fy * (3.0f - 2.0f * fy);
        const float a = hash2(ix, iy, seed);
        const float b = hash2(ix + 1, iy, seed);
        const float c = hash2(ix, iy + 1, seed);
        const float d = hash2(ix + 1, iy + 1, seed);
        return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
    };

    // 1. Soft FBM wisp texture (128x128): strictly zero alpha on all borders.
    constexpr int kW = 128, kH = 128;
    std::vector<uint8_t> wispPixels(static_cast<size_t>(kW) * kH * 4, 255);
    for (int y = 0; y < kH; ++y) {
        const float ny = (static_cast<float>(y) + 0.5f) / kH * 2.0f - 1.0f;
        for (int x = 0; x < kW; ++x) {
            const float nx = (static_cast<float>(x) + 0.5f) / kW * 2.0f - 1.0f;
            const float r2 = nx * nx * 1.12f + ny * ny * 1.25f;
            const float radial = std::max(0.0f, 1.0f - r2);
            const float envelope = radial * radial * radial;
            const float sx = static_cast<float>(x) * 0.042f;
            const float sy = static_cast<float>(y) * 0.068f;
            const float n1 = valueNoise(sx, sy, 11u);
            const float n2 = valueNoise(sx * 2.15f + 7.3f, sy * 2.15f + 3.1f, 29u);
            const float n3 = valueNoise(sx * 4.40f + 1.9f, sy * 4.40f + 8.7f, 53u);
            const float cloud = 0.55f * n1 + 0.30f * n2 + 0.15f * n3;
            const float a = std::clamp(envelope * (0.35f + 0.95f * cloud), 0.0f, 1.0f);
            const size_t idx = (static_cast<size_t>(y) * kW + x) * 4;
            wispPixels[idx + 0] = 255;
            wispPixels[idx + 1] = 242;
            wispPixels[idx + 2] = 216;
            wispPixels[idx + 3] = static_cast<uint8_t>(std::lround(a * 255.0f));
        }
    }
    glGenTextures(1, &sandWispTex_);
    glBindTexture(GL_TEXTURE_2D, sandWispTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, wispPixels.data());

    // 2. Elongated soft grain / streak texture (64x32): zero alpha on all borders.
    constexpr int kSW = 64, kSH = 32;
    std::vector<uint8_t> streakPixels(static_cast<size_t>(kSW) * kSH * 4, 255);
    for (int y = 0; y < kSH; ++y) {
        const float ny = (static_cast<float>(y) + 0.5f) / kSH * 2.0f - 1.0f;
        for (int x = 0; x < kSW; ++x) {
            const float nx = (static_cast<float>(x) + 0.5f) / kSW * 2.0f - 1.0f;
            const float r2 = nx * nx * 1.15f + ny * ny * 1.15f;
            const float edge = std::max(0.0f, 1.0f - r2);
            const float core = std::exp(-3.2f * (nx * nx * 0.7f + ny * ny * 1.8f));
            const float a = std::clamp(edge * edge * core, 0.0f, 1.0f);
            const size_t idx = (static_cast<size_t>(y) * kSW + x) * 4;
            streakPixels[idx + 0] = 255;
            streakPixels[idx + 1] = 244;
            streakPixels[idx + 2] = 220;
            streakPixels[idx + 3] = static_cast<uint8_t>(std::lround(a * 255.0f));
        }
    }
    glGenTextures(1, &sandStreakTex_);
    glBindTexture(GL_TEXTURE_2D, sandStreakTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSW, kSH, 0, GL_RGBA, GL_UNSIGNED_BYTE, streakPixels.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Menu3D::InitAtmosphereShadersAndPost() {
    const auto location = GlProc<PFNGLGETUNIFORMLOCATIONPROC>("glGetUniformLocation");
    const auto genFbo = GlProc<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers");
    const auto bindFbo = GlProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    if (!location) return;

    skyProgram_ = CompileGlslProgram(kDesertSkyVertex, kAtmosphereSkyFragment, "AtmosphereSky");
    if (skyProgram_) {
        skyCamRight_ = location(skyProgram_, "camRight");
        skyCamUp_ = location(skyProgram_, "camUp");
        skyCamForward_ = location(skyProgram_, "camForward");
        skyParams_ = location(skyProgram_, "skyParams");
        skySceneWeights_ = location(skyProgram_, "skySceneWeights");
        skyExtraParams_ = location(skyProgram_, "skyExtraParams");
    }

    if (!genFbo || !bindFbo) return;
    bloomExtractProgram_ = CompileGlslProgram(kFullscreenVertex, kBloomExtractFragment, "BloomExtract");
    bloomBlurProgram_ = CompileGlslProgram(kFullscreenVertex, kBloomBlurFragment, "BloomBlur");
    postHeatProgram_ = CompileGlslProgram(kFullscreenVertex, kPostHeatFragment, "PostHeat");
    if (bloomExtractProgram_ && bloomBlurProgram_) {
        bloomExtractTex_ = location(bloomExtractProgram_, "srcTex");
        bloomExtractThreshold_ = location(bloomExtractProgram_, "threshold");
        bloomExtractTint_ = location(bloomExtractProgram_, "tint");
        bloomBlurTex_ = location(bloomBlurProgram_, "srcTex");
        bloomBlurOffset_ = location(bloomBlurProgram_, "texelOffset");
        if (postHeatProgram_) {
            postHeatSceneTex_ = location(postHeatProgram_, "sceneTex");
            postHeatBloomTex_ = location(postHeatProgram_, "bloomTex");
            postHeatParams_ = location(postHeatProgram_, "postParams");
            postHeatBloomTint_ = location(postHeatProgram_, "bloomTint");
        }
        genFbo(1, &bloomFramebuffer_);
        glGenTextures(1, &sceneCopyTex_);
        glGenTextures(2, bloomTex_.data());
        ResizeBloomTargets();
    }
}

void Menu3D::ResizeBloomTargets() {
    if (!bloomFramebuffer_ || !sceneCopyTex_ || !bloomTex_[0] || !bloomTex_[1]) return;
    sceneCopyW_ = std::max(1, width_);
    sceneCopyH_ = std::max(1, height_);
    bloomW_ = std::max(1, width_ / 4);
    bloomH_ = std::max(1, height_ / 4);

    constexpr GLint srgb8Alpha8 = 0x8C43;
    glBindTexture(GL_TEXTURE_2D, sceneCopyTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, srgb8Alpha8, sceneCopyW_, sceneCopyH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (unsigned int tex : bloomTex_) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bloomW_, bloomH_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Menu3D::ReleaseAtmosphereResources() {
    const auto delProg = GlProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram");
    const auto delFbo = GlProc<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers");
    if (delProg) {
        if (skyProgram_) delProg(skyProgram_);
        if (bloomExtractProgram_) delProg(bloomExtractProgram_);
        if (bloomBlurProgram_) delProg(bloomBlurProgram_);
        if (postHeatProgram_) delProg(postHeatProgram_);
    }
    if (delFbo && bloomFramebuffer_) delFbo(1, &bloomFramebuffer_);
    if (sandWispTex_) glDeleteTextures(1, &sandWispTex_);
    if (sandStreakTex_) glDeleteTextures(1, &sandStreakTex_);
    if (sceneCopyTex_) glDeleteTextures(1, &sceneCopyTex_);
    if (bloomTex_[0]) glDeleteTextures(2, bloomTex_.data());
    skyProgram_ = bloomExtractProgram_ = bloomBlurProgram_ = postHeatProgram_ = 0;
    bloomFramebuffer_ = sandWispTex_ = sandStreakTex_ = sceneCopyTex_ = 0;
    bloomTex_ = {};
}

void Menu3D::InitShadowMap(unsigned int& framebuffer, unsigned int& depth, int size) {
    const auto gen = GlProc<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers");
    const auto bind = GlProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    const auto attach = GlProc<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D");
    const auto status = GlProc<PFNGLCHECKFRAMEBUFFERSTATUSPROC>("glCheckFramebufferStatus");
    const auto remove = GlProc<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers");
    if ((!SDL_GL_ExtensionSupported("GL_ARB_framebuffer_object") &&
         !SDL_GL_ExtensionSupported("GL_EXT_framebuffer_object")) ||
        !gen || !bind || !attach || !status || !remove) {
        std::cerr << "[Menu3D] Shadow framebuffer unavailable\n";
        return;
    }
    glGenTextures(1, &depth);
    glBindTexture(GL_TEXTURE_2D, depth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size,
                 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    gen(1, &framebuffer);
    if (framebuffer) {
        bind(GL_FRAMEBUFFER, framebuffer);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        attach(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depth, 0);
        const GLenum framebufferStatus = status(GL_FRAMEBUFFER);
        bind(GL_FRAMEBUFFER, 0);
        if (framebufferStatus == GL_FRAMEBUFFER_COMPLETE) return;
        remove(1, &framebuffer);
        framebuffer = 0;
    }
    glDeleteTextures(1, &depth);
    depth = 0;
    std::cerr << "[Menu3D] Shadow framebuffer incomplete\n";
}

void Menu3D::Shutdown() {
    ready_ = false;
    if (glContext_) SDL_GL_MakeCurrent(glWindow_, glContext_);
    focusWeapons_.clear();
    chickens_.clear();
    for (auto& environment : environments_) environment.reset();
    for (auto& preview : scenePreviews_) {
        if (preview) SDL_DestroyTexture(preview);
        preview = nullptr;
    }
    if (texture_) { SDL_DestroyTexture(texture_); texture_ = nullptr; }
    ReleaseAtmosphereResources();
    if (chickenProgram_) GlProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram")(chickenProgram_);
    chickenProgram_ = 0;
    if (volumeProgram_) GlProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram")(volumeProgram_);
    if (spotFramebuffer_) GlProc<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers")(1, &spotFramebuffer_);
    if (volumeFramebuffer_) GlProc<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers")(1, &volumeFramebuffer_);
    const GLuint textures[] = {spotDepth_, volumeTexture_, sceneDepthCopy_};
    if (glContext_) glDeleteTextures(3, textures);
    volumeProgram_ = volumeFramebuffer_ = spotFramebuffer_ = spotDepth_ = volumeTexture_ = sceneDepthCopy_ = 0;
    if (surfaceProgram_) GlProc<PFNGLDELETEPROGRAMPROC>("glDeleteProgram")(surfaceProgram_);
    if (shadowFramebuffer_) GlProc<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers")(1, &shadowFramebuffer_);
    if (shadowDepth_) glDeleteTextures(1, &shadowDepth_);
    surfaceProgram_ = shadowFramebuffer_ = shadowDepth_ = 0;
    if (glContext_) { SDL_GL_DeleteContext(glContext_); glContext_ = nullptr; }
    if (glWindow_) { SDL_DestroyWindow(glWindow_); glWindow_ = nullptr; }
    mainRenderer_ = nullptr;
    pixels_.clear();
    flipped_.clear();
}

bool Menu3D::LoadModels() {
    const char* scenes[] = { "ancient_ruins", "dust_2", "mirage" };
    // Parsing and skinning the three independent scenes can overlap. All GL work
    // stays on this thread and takes place when the previews draw.
    using SceneLoad = std::pair<std::unique_ptr<Menu3DModel>, bool>;
    std::array<std::future<SceneLoad>, 3> loads;
    for (int i = 0; i < 3; ++i) {
        loads[i] = std::async([name = scenes[i]] {
            auto model = std::make_unique<Menu3DModel>();
            const bool loaded = model->Load(std::string("assets/models/menu/scenes/") + name + ".glb");
            return SceneLoad{std::move(model), loaded};
        });
    }
    for (int i = 0; i < 3; ++i) {
        auto [model, loaded] = loads[i].get();
        if (loaded) {
            environments_[i] = std::move(model);
        } else {
            std::cerr << "[Menu3D] Environment unavailable: " << scenes[i] << " (" << model->LastError() << ")\n";
        }
    }
    // T/CT 角色已移除（2026-11-25）：主菜单不再加载 t.glb / ct.glb，
    // 两个模型文件留在磁盘上，需要时可随时恢复加载。
    chickens_.clear();
    for (int i = 0; i < 3; ++i) {
        auto chicken = std::make_unique<Menu3DModel>();
        if (!chicken->Load("assets/models/menu/chicken.glb", "Menu_Walk")) {
            std::cerr << "[Menu3D] 小鸡模型加载失败: " << chicken->LastError() << std::endl;
            chickens_.clear();
            break;
        }
        // Every selectable persistent clip must exist in the exported model.
        for (int p = 0; p < kChickenPoseCount; ++p) {
            if (!chicken->PlayAnimation(kChickenPoseAnims[p])) {
                std::cerr << "[Menu3D] 小鸡动画缺失: '" << kChickenPoseAnims[p]
                          << "'" << std::endl;
            }
        }
        if (!chicken->PlayAnimation("Menu_Walk")) {
            std::cerr << "[Menu3D] 小鸡基础动画不可用，当前动画="
                      << chicken->AnimationName() << std::endl;
        }
        // 错开三只鸡的动画相位
        chicken->Update(0.37f * static_cast<float>(i));
        chickens_.push_back(std::move(chicken));
    }
    if (!chickens_.empty()) {
        std::cout << "[Menu3D] 小鸡已就绪: " << chickens_.size()
                  << " 只, 动画=" << chickens_[0]->AnimationName()
                  << ", 模型高=" << kChickenModelHeight << " 单位" << std::endl;
    }

    focusWeapons_.clear();
    const char* weaponPaths[3] = {
        "assets/models/quaternius/Guns/AK.gltf",
        "assets/models/quaternius/Guns/Pistol.gltf",
        "assets/models/quaternius/Guns/Knife_1.gltf"
    };
    for (int i = 0; i < 3; ++i) {
        auto weapon = std::make_unique<Menu3DModel>();
        if (!weapon->Load(weaponPaths[i])) {
            std::cerr << "[Menu3D] 武器模型加载失败: " << weaponPaths[i]
                      << " " << weapon->LastError() << std::endl;
            weapon.reset();
        }
        focusWeapons_.push_back(std::move(weapon));
    }

    ApplyAllCustomization();

    std::cout << "[Menu3D] glTF 角色已加载: 小鸡=" << chickens_.size() << std::endl;
    InitChickenState();
    return !chickens_.empty();
}

void Menu3D::InitChickenState() {
    const float xs[3] = { -2.35f, 0.0f, 2.25f };
    const float zs[3] = { 3.2f, 5.4f, 3.0f };
    for (int i = 0; i < 3; ++i) {
        ChickenState& s = chickenState_[i];
        s.x = xs[i];
        s.z = zs[i];
        s.yaw = 0.0f;
        s.targetYaw = 0.0f;
        // 透视补偿：世界速度按距离缩放，屏幕上三只鸡的行走速度接近；
        // 动画播放速率由 ChickenAnimTimeScale 按同一速度换算，避免滑步。
        const float dist = 8.6f - s.z;
        const float variance = 0.85f + static_cast<float>(chickenRng_() % 100) / 100.0f * 0.30f;
        s.speed = kChickenBaseSpeed * (dist / 6.0f) * variance;
        s.movementSpeed = s.speed;
    }
    for (int i = 0; i < 3; ++i) PickChickenTarget(i);
}

float Menu3D::ChickenWorldScale(const ChickenState& s) const {
    // 透视补偿：距离越近，世界缩放越小，屏幕上三只鸡大小接近
    const float dist = 8.6f - s.z;
    return std::clamp(0.30f * dist / 6.0f, 0.055f, 0.34f);
}

float Menu3D::ChickenAnimTimeScale(const ChickenState& s) const {
    if (focusedObject_ >= 0) return kWalkClipSeconds * 1.25f;
    // 期望循环时长 = 每循环步幅 / 移动速度；步幅 = 模型身高 × 世界缩放 × 步幅系数
    const float stride = kChickenModelHeight * ChickenWorldScale(s) * kChickenStrideBodyHeights;
    if (stride <= 0.0001f) return 1.0f;
    return std::clamp(kWalkClipSeconds * s.movementSpeed / stride, 0.0f, 16.0f);
}

float Menu3D::ChickenRadius(int index, float z) const {
    const float scale = std::clamp(.30f * (8.6f - z) / 6.0f, .055f, .34f);
    const float radius = index >= 0 && index < static_cast<int>(chickens_.size())
        ? chickens_[index]->HorizontalRadius() : 1.05f;
    return radius * scale + .06f;
}

std::vector<Menu3D::GroundObstacle> Menu3D::ChickenObstacles(int index) const {
    std::vector<GroundObstacle> obstacles;
    // Keep room for every menu loadout as well as the currently visible meshes.
    // T/CT 的站位障碍已随角色移除（2026-11-25），小鸡可跑满整个舞台。
    for (int other = 0; other < static_cast<int>(chickens_.size()) && other < 3; ++other) {
        if (other == index) continue;
        const auto& state = chickenState_[other];
        obstacles.push_back({state.x, state.z, ChickenRadius(other, state.z), other});
    }
    return obstacles;
}

bool Menu3D::ChickenPathClear(int index, float fromX, float fromZ, float toX, float toZ) const {
    const float radius = std::max(ChickenRadius(index, fromZ), ChickenRadius(index, toZ));
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float halfWidth = std::min(5.5f, std::tan(28.0f * kPi / 180.0f) * aspect * (kMainCamZ - toZ));
    if (toZ < kChickenMinZ || toZ > kChickenMaxZ || std::abs(toX) > halfWidth - radius - .12f) return false;
    const float dx = toX - fromX, dz = toZ - fromZ;
    const float squared = dx * dx + dz * dz;
    for (const auto& obstacle : ChickenObstacles(index)) {
        const float fraction = squared > .000001f ? std::clamp(
            ((obstacle.x - fromX) * dx + (obstacle.z - fromZ) * dz) / squared, 0.0f, 1.0f) : 0.0f;
        const float x = fromX + fraction * dx - obstacle.x;
        const float z = fromZ + fraction * dz - obstacle.z;
        const float clearance = radius + obstacle.radius;
        if (x * x + z * z < clearance * clearance) return false;
    }
    return true;
}

void Menu3D::SeparateChickens() {
    for (int pass = 0; pass < 8; ++pass) {
        for (int index = 0; index < static_cast<int>(chickens_.size()) && index < 3; ++index) {
            auto& state = chickenState_[index];
            for (const auto& obstacle : ChickenObstacles(index)) {
                if (custom_.pose[index + 2] != 0 && obstacle.chickenIndex >= 0 &&
                    custom_.pose[obstacle.chickenIndex + 2] == 0) continue;
                float dx = state.x - obstacle.x, dz = state.z - obstacle.z;
                float distance = std::sqrt(dx * dx + dz * dz);
                const float clearance = ChickenRadius(index, state.z) + obstacle.radius + .002f;
                if (distance >= clearance) continue;
                if (distance < .0001f) {
                    dx = obstacle.x > 0 ? -1.0f : 1.0f;
                    dz = 0;
                    distance = 1.0f;
                }
                state.x = obstacle.x + dx / distance * clearance;
                state.z = obstacle.z + dz / distance * clearance;
            }
        }
    }
}

void Menu3D::PickChickenTarget(int index) {
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    ChickenState& s = chickenState_[index];

    // 全场自由漫游：不再按左/中/右分区。
    // z 先随机，再按该 z 的可见宽度限制 x，避免小鸡跑出画面。
    s.targetX = s.x;
    s.targetZ = s.z;
    for (int attempt = 0; attempt < 32; ++attempt) {
        const float z = kChickenMinZ + (kChickenMaxZ - kChickenMinZ) * dist01(chickenRng_);
        const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
        const float width = std::max(0.0f, std::min(5.5f,
            std::tan(28.0f * kPi / 180.0f) * aspect * (kMainCamZ - z)) - ChickenRadius(index, z) - .12f);
        const float x = -width + 2.0f * width * dist01(chickenRng_);
        if (!ChickenPathClear(index, x, z, x, z)) continue;
        s.targetX = x;
        s.targetZ = z;
        break;
    }
    s.repathTimer = 1.2f + 2.8f * dist01(chickenRng_);
}

void Menu3D::UpdateChickens(float dt) {
    if (dt <= 0) return;
    SeparateChickens();
    for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
        ChickenState& s = chickenState_[i];
        s.movementSpeed = 0;
        if (custom_.pose[i + 2] != 0) continue;
        s.repathTimer -= dt;
        if (s.repathTimer <= 0.0f) PickChickenTarget(i);

        float dx = s.targetX - s.x;
        float dz = s.targetZ - s.z;
        float dist = std::sqrt(dx * dx + dz * dz);
        if (dist <= 0.06f) {
            // 到点后立刻换下一个方向，保证主界面里一直有鸡在走
            PickChickenTarget(i);
            dx = s.targetX - s.x;
            dz = s.targetZ - s.z;
            dist = std::sqrt(dx * dx + dz * dz);
        }

        if (dist > 0.001f) {
            // 模型正面为 +Z：yaw 跟随行走方向，并做平滑转身
            const float desired = std::atan2(dx, dz);
            const float current = s.yaw * kPi / 180.0f;
            const float lookAhead = std::min(dist, std::max(.55f, s.speed * .8f));
            float bestScore = -10.0f;
            s.targetYaw = s.yaw;
            for (int offset = -6; offset <= 6; ++offset) {
                const float angle = desired + offset * kPi / 6.0f;
                const float x = s.x + std::sin(angle) * lookAhead;
                const float z = s.z + std::cos(angle) * lookAhead;
                if (!ChickenPathClear(i, s.x, s.z, x, z)) continue;
                const float score = std::cos(angle - desired) - .15f * (1.0f - std::cos(angle - current));
                if (score > bestScore) {
                    bestScore = score;
                    s.targetYaw = angle * 180.0f / kPi;
                }
            }
            float delta = std::fmod(s.targetYaw - s.yaw + 540.0f, 360.0f) - 180.0f;
            const float maxTurn = 220.0f * dt;
            s.yaw += std::clamp(delta, -maxTurn, maxTurn);
            s.yaw = std::fmod(s.yaw + 360.0f, 360.0f);

            const float angle = s.yaw * kPi / 180.0f;
            const float alignment = std::max(0.0f, std::cos((s.targetYaw - s.yaw) * kPi / 180.0f));
            const float step = std::min(dist, s.speed * alignment * dt);
            const float x = s.x + std::sin(angle) * step;
            const float z = s.z + std::cos(angle) * step;
            if (ChickenPathClear(i, s.x, s.z, x, z)) {
                s.x = x;
                s.z = z;
                s.movementSpeed = step / dt;
            } else {
                s.repathTimer = std::min(s.repathTimer, .25f);
            }
        }
    }
}

float Menu3D::ActorStageX(int actor) const {
    const float offset = displayedScene_ == 1 ? 4.92f : 4.4f;
    return actor == 0 ? -offset : offset;
}

void Menu3D::DrawChickens(bool depthOnly) {
    const bool focus = focusedObject_ >= 2;
    const float t = focus ? focusProgress_ * focusProgress_ * (3.f - 2.f * focusProgress_) : 0.f;
    for (int i = 0; i < static_cast<int>(chickens_.size()); ++i) {
        const auto& state = chickenState_[i];
        const bool selected = focus && focusedObject_ == i + 2;
        const float opacity = selected ? 1.f : 1.f - t;
        // Only opaque silhouettes cast solid shadows during the focus crossfade.
        if (opacity <= 0.01f || (depthOnly && opacity < 0.999f)) continue;
        float x = state.x, z = state.z, yaw = state.yaw;
        float scale = ChickenWorldScale(state);
        if (selected) {
            FocusCurrentPosition(x, z);
            scale += (2.3f - scale) * t;
            yaw = 0;
        }
        const auto& model = *chickens_[i];
        if (!depthOnly && (selected || (!focus && hoveredObject_ == i + 2))) {
            glPushMatrix();
            glTranslatef(x, 0, z);
            model.DrawHighlight(scale, 0, yaw);
            glPopMatrix();
        }
        const float c = scale * std::cos(yaw*kPi/180.f);
        const float sn = scale * std::sin(yaw*kPi/180.f);
        const float transform[] = {c,0,-sn,0, 0,scale,0,0, sn,0,c,0, x,-model.GroundOffset()*scale,z,1};
        glPushMatrix();
        glMultMatrixf(transform);
        if (depthOnly) {
            model.DrawDepth();
        } else if (chickenProgram_) {
            const auto use = GlProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
            use(chickenProgram_);
            SetSpotlightUniforms(chickenSpot_);
            GlProc<PFNGLUNIFORM3FVPROC>("glUniform3fv")(chickenCamera_,1,cameraPos_.data());
            GlProc<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv")(chickenTransform_,1,GL_FALSE,transform);
            model.DrawRaw(opacity, &chickenUniforms_);
            use(0);
        } else {
            model.DrawRaw(opacity);
        }
        glPopMatrix();
    }
}

void Menu3D::ObjectWorldPosition(int index, float& x, float& y, float& z) const {
    x = 0.0f; y = 1.2f; z = 0.0f;
    if (index == 0) { x = ActorStageX(0); }
    else if (index == 1) { x = ActorStageX(1); }
    else if (index >= 2 && index < 5 && index - 2 < static_cast<int>(chickenState_.size())) {
        const ChickenState& s = chickenState_[index - 2];
        x = s.x; y = 0.7f; z = s.z;
    }
}

void Menu3D::FocusCurrentPosition(float& x, float& z) const {
    const float raw = std::clamp(focusProgress_, 0.0f, 1.0f);
    const float t = raw * raw * (3.0f - 2.0f * raw);
    if (focusedObject_ == 0) {
        x = ActorStageX(0) + (focusTargetX_ - ActorStageX(0)) * t;
        z = 0.0f;
    } else if (focusedObject_ == 1) {
        x = ActorStageX(1) + (focusTargetX_ - ActorStageX(1)) * t;
        z = 0.0f;
    } else if (focusedObject_ >= 2 && focusedObject_ - 2 < static_cast<int>(chickenState_.size())) {
        const ChickenState& s = chickenState_[focusedObject_ - 2];
        x = s.x + (focusTargetX_ - s.x) * t;
        z = s.z + (focusTargetZ_ - s.z) * t;
    } else {
        x = focusTargetX_;
        z = focusTargetZ_;
    }
}

void Menu3D::SetOverlayMouse(float u, float v, bool active) {
    overlaySpotlightActive_ = active;
    if (!active) return;
    // Match the overhead camera and the presentation crop used by the background.
    const float aspect = static_cast<float>(width_) / height_;
    const float cropScale = std::max(aspect / presentationAspect_, presentationAspect_ / aspect);
    const float halfH = std::tan(28.0f * kPi / 180.0f) * kTopCamY * cropScale;
    const float halfW = halfH * aspect;
    overlayTargetX_ = (std::clamp(u, 0.0f, 1.0f) - 0.5f) * 2.0f * halfW;
    overlayTargetZ_ = (std::clamp(v, 0.0f, 1.0f) - 0.5f) * 2.0f * halfH;
}

void Menu3D::SetFocusedObject(int index) {
    if (index < -1 || index >= kObjectCount) index = -1;
    if (index >= 0 && index < 2) index = -1;  // T/CT 已移除，不可聚焦
    if (focusedObject_ == index) return;
    focusedObject_ = index;
    focusProgress_ = 0.0f;
    focusTargetX_ = (index < 2) ? -2.4f : -1.8f;
    focusTargetZ_ = 0.0f;
    spotScanTimer_ = 0.0f;
}

void Menu3D::SetObjectColor(int index, int option) {
    if (index < 0 || index >= kObjectCount) return;
    custom_.color[index] = option;
    ApplyCustomizationColor(index);
}

void Menu3D::SetObjectPose(int index, int option) {
    if (index < 0 || index >= kObjectCount) return;
    custom_.pose[index] = option;
    ApplyCustomizationPose(index);
}

void Menu3D::SetCustomization(const Customization& c) {
    custom_ = c;
    ApplyAllCustomization();
}

void Menu3D::ApplyAllCustomization() {
    for (int obj = 0; obj < kObjectCount; ++obj) {
        ApplyCustomizationColor(obj);
        ApplyCustomizationAccessory(obj);
        if (obj >= 2) ApplyCustomizationPose(obj);
    }
}

void Menu3D::ApplyCustomizationColor(int obj) {
    if (obj < 0 || obj >= kObjectCount) return;
    if (obj < 2) return;  // T/CT 已移除（2026-11-25），只剩小鸡可定制

    const int ci = obj - 2;
    if (ci < 0 || ci >= static_cast<int>(chickens_.size()) || !chickens_[ci]) return;
    const int opt = std::clamp(custom_.color[obj], 0, kChickenColorCount - 1);
    const RgbColor& c = kChickenColors[opt];
    chickens_[ci]->ClearSlotColors();
    chickens_[ci]->SetSolidColor(c.r, c.g, c.b);
}

void Menu3D::ApplyCustomizationPose(int obj) {
    if (obj < 0 || obj >= kObjectCount) return;
    if (obj < 2) return;  // T/CT 已移除（2026-11-25），只剩小鸡可定制

    const int ci = obj - 2;
    if (ci < 0 || ci >= static_cast<int>(chickens_.size()) || !chickens_[ci]) return;
    const int opt = std::clamp(custom_.pose[obj], 0, kChickenPoseCount - 1);
    const char* wanted = kChickenPoseAnims[opt];
    if (!chickens_[ci]->PlayAnimation(wanted)) {
        std::cerr << "[Menu3D] 小鸡动画缺失 " << wanted << std::endl;
        chickens_[ci]->PlayAnimation("Menu_Walk");
    }
}



void Menu3D::ApplyCustomizationAccessory(int obj) {
    if (obj < 0 || obj >= kObjectCount) return;
    if (obj < 2) return;  // T/CT 已移除（2026-11-25），只剩小鸡可定制
    const int ci = obj - 2;
    if (ci >= static_cast<int>(chickens_.size())) return;
    Menu3DModel* model = chickens_[ci].get();
    if (!model) return;
    const char* nodes[] = { "accessory_headwear", "accessory_eyewear", "accessory_back" };
    for (int i = 0; i < 3; ++i) {
        model->SetNodeVisible(nodes[i], custom_.accessory[obj] == i + 1);
        model->SetNodeColorExcluded(nodes[i], true);
    }
}

int Menu3D::FocusCategoryCount(int objectIndex) const {
    return (objectIndex >= 0 && objectIndex < 2) ? 4 : 3;
}

const char* Menu3D::FocusCategoryName(int objectIndex, int cat) const {
    if (objectIndex >= 0 && objectIndex < 2) {
        if (cat >= 0 && cat < 4) return kCharCategoryNames[cat];
    } else if (cat >= 0 && cat < 3) {
        return kChickenCategoryNames[cat];
    }
    return "";
}

int Menu3D::FocusOptionCount(int objectIndex, int cat) const {
    if (objectIndex >= 0 && objectIndex < 2) {
        switch (cat) {
            case 0: return kWeaponOptionCount;
            case 1: return kCharPaletteCount;
            case 2: return kCharPoseCount;
            case 3: return kCharAccCount;
            default: return 0;
        }
    }
    switch (cat) {
        case 0: return kChickenColorCount;
        case 1: return kChickenPoseCount;
        case 2: return kChickenAccCount;
        default: return 0;
    }
}

const char* Menu3D::FocusOptionName(int objectIndex, int cat, int opt) const {
    if (opt < 0 || opt >= FocusOptionCount(objectIndex, cat)) return "";
    if (objectIndex >= 0 && objectIndex < 2) {
        switch (cat) {
            case 0: return kWeaponOptionNames[opt];
            case 1: return kCharColorNames[opt];
            case 2: return kCharPoseNames[opt];
            case 3: return kCharAccNames[opt];
            default: return "";
        }
    }
    switch (cat) {
        case 0: return kChickenColorNames[opt];
        case 1: return kChickenPoseNames[opt];
        case 2: return kChickenAccNames[opt];
        default: return "";
    }
}

int Menu3D::FocusSelected(int objectIndex, int cat) const {
    if (objectIndex < 0 || objectIndex >= kObjectCount) return 0;
    if (objectIndex < 2) return 0;  // T/CT 已移除（2026-11-25）
    switch (cat) {
        case 0: return custom_.color[objectIndex];
        case 1: return custom_.pose[objectIndex];
        case 2: return custom_.accessory[objectIndex];
        default: return 0;
    }
}

void Menu3D::FocusSelect(int objectIndex, int cat, int opt) {
    if (objectIndex < 0 || objectIndex >= kObjectCount) return;
    if (objectIndex < 2) return;  // T/CT 已移除（2026-11-25）
    const int count = FocusOptionCount(objectIndex, cat);
    if (count <= 0) return;
    opt = std::clamp(opt, 0, count - 1);

    switch (cat) {
        case 0: custom_.color[objectIndex] = opt;     ApplyCustomizationColor(objectIndex); break;
        case 1: custom_.pose[objectIndex] = opt;      ApplyCustomizationPose(objectIndex); break;
        case 2: custom_.accessory[objectIndex] = opt; ApplyCustomizationAccessory(objectIndex); break;
        default: break;
    }
}

void Menu3D::UpdateObjectScreenPositions() {
    double mv[16], pr[16];
    int vp[4];
    glGetDoublev(GL_MODELVIEW_MATRIX, mv);
    glGetDoublev(GL_PROJECTION_MATRIX, pr);
    glGetIntegerv(GL_VIEWPORT, vp);

    auto project = [&](float x, float y, float z, float& u, float& v) -> bool {
        const double in[4] = { x, y, z, 1.0 };
        double a[4] = { 0, 0, 0, 0 };
        double clip[4] = { 0, 0, 0, 0 };
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) a[row] += mv[k * 4 + row] * in[k];
        }
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) clip[row] += pr[k * 4 + row] * a[k];
        }
        if (std::fabs(clip[3]) < 1e-6) return false;
        const double nx = clip[0] / clip[3];
        const double ny = clip[1] / clip[3];
        u = static_cast<float>(nx * 0.5 + 0.5);
        v = static_cast<float>(1.0 - (ny * 0.5 + 0.5));
        return true;
    };

    for (int i = 0; i < kObjectCount; ++i) {
        if (i < 2) continue;  // T/CT 已移除（2026-11-25），0/1 号槽位不再投影
        float x, y, z;
        ObjectWorldPosition(i, x, y, z);
        const float bottomY = 0.0f;
        const float topY = 1.55f;
        const float width = 1.05f;
        const float midY = (bottomY + topY) * 0.5f;

        float ub = 0, vb = 0, ut = 0, vt = 0;
        float ul = 0, vl = 0, ur = 0, vr = 0;
        if (!project(x, bottomY, z, ub, vb)) continue;
        if (!project(x, topY, z, ut, vt)) continue;
        if (!project(x - width * 0.5f, midY, z, ul, vl)) continue;
        if (!project(x + width * 0.5f, midY, z, ur, vr)) continue;

        // 使用覆盖脚底到头顶的屏幕包围盒，鼠标落在身体任意位置都能命中。
        screenU_[i] = (ul + ur) * 0.5f;
        screenV_[i] = (vb + vt) * 0.5f;
        screenRU_[i] = std::max(0.018f, std::fabs(ur - ul) * 0.5f * 1.30f);
        screenRV_[i] = std::max(0.024f, std::fabs(vt - vb) * 0.5f * 1.20f);
    }
}

int Menu3D::HitTestNormalized(float u, float v) const {
    // T/CT 已移除（2026-11-25），只有 2/3/4 号小鸡可命中。
    int bestChicken = -1;
    float bestChickenD = 1e9f;
    for (int i = 2; i < kObjectCount; ++i) {
        const float rx = std::max(screenRU_[i], 0.005f);
        const float ry = std::max(screenRV_[i], 0.005f);
        const float dx = (u - screenU_[i]) / rx;
        const float dy = (v - screenV_[i]) / ry;
        if (std::fabs(dx) <= 1.0f && std::fabs(dy) <= 1.0f) {
            const float d = dx * dx + dy * dy;
            if (d < bestChickenD) {
                bestChickenD = d;
                bestChicken = i;
            }
        }
    }
    return bestChicken;
}

void Menu3D::UpdateSpotlight(float dt) {
    float tx = 0.0f, tz = 0.0f;
    if (overlaySpotlightActive_) {
        tx = overlayTargetX_;
        tz = overlayTargetZ_;
    } else if (focusedObject_ >= 0) {
        FocusCurrentPosition(tx, tz);
    } else if (hoveredObject_ >= 0) {
        float x, y, z;
        ObjectWorldPosition(hoveredObject_, x, y, z);
        tx = x;
        tz = z;
    } else {
        spotScanTimer_ -= dt;
        if (spotScanTimer_ <= 0.0f) {
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
            spotTargetX_ = -4.0f + 8.0f * dist01(chickenRng_);
            spotTargetZ_ = 0.5f + 5.5f * dist01(chickenRng_);
            spotScanTimer_ = 1.8f + 2.8f * dist01(chickenRng_);
        }
        tx = spotTargetX_;
        tz = spotTargetZ_;
    }

    const float speed = overlaySpotlightActive_ ? 16.0f
                      : (focusedObject_ >= 0) ? 14.0f
                      : (hoveredObject_ >= 0) ? 10.0f : 2.2f;
    const float a = 1.0f - std::exp(-speed * dt);
    spotX_ += (tx - spotX_) * a;
    spotZ_ += (tz - spotZ_) * a;

    const int targetIndex = (focusedObject_ >= 0) ? focusedObject_ : hoveredObject_;
    const float targetRadius = overlaySpotlightActive_ ? 0.95f
                             : (targetIndex < 0) ? 0.72f
                             : (targetIndex < 2) ? 1.05f : 0.34f;
    float targetY = 0.05f;
    if (!overlaySpotlightActive_ && targetIndex >= 2 && targetIndex < kObjectCount) {
        // Aim through the body at its displayed scale, including the focus enlargement.
        // A fixed world height sends the beam over the smaller, nearby chickens.
        float scale = ChickenWorldScale(chickenState_[targetIndex - 2]);
        if (focusedObject_ == targetIndex) {
            const float t = focusProgress_ * focusProgress_ * (3.f - 2.f * focusProgress_);
            scale += (2.3f - scale) * t;
        }
        targetY = 0.55f * kChickenModelHeight * scale;
    }
    // 光柱落点半径逐帧插值，恢复 T/CT 大圈与小鸡小圈之间的平滑过渡。
    spotRadius_ += (targetRadius - spotRadius_) * (1.0f - std::exp(-10.0f * dt));
    spotY_ += (targetY - spotY_) * (1.0f - std::exp(-12.0f * dt));
}

void Menu3D::DrawObjectRing(float x, float z, float radius, float alpha) {
    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glColor4f(0.68f, 0.88f, 1.0f, alpha);
    glLineWidth(2.0f);
    glBegin(GL_LINE_LOOP);
    constexpr int kSegments = 40;
    for (int i = 0; i < kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        glVertex3f(x + std::cos(a) * radius, 0.018f, z + std::sin(a) * radius);
    }
    glEnd();
    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);
}

void Menu3D::PrepareSpotlight() {
    light_.power = previewCapture_ || spotlightSuppressed_ ? 0.f : spotlightIntensity_ * 2.6f;
    if (light_.power <= 0.001f || !surfaceProgram_) {
        glDisable(GL_LIGHT1);
        return;
    }
    light_.position = {0.f, 6.4f, spotSourceZ_};
    const float dy = spotY_ - light_.position[1], dz = spotZ_ - spotSourceZ_;
    const float distance = std::sqrt(spotX_*spotX_ + dy*dy + dz*dz);
    light_.direction = {spotX_/distance, dy/distance, dz/distance};
    light_.slope = spotRadius_ / distance;
    // The axis always points downward, so +Z is a stable reference for the basis.
    const auto& d = light_.direction;
    const float xy = std::hypot(d[0], d[1]);
    light_.right = {d[1]/xy, -d[0]/xy, 0};
    const auto& r = light_.right;
    light_.up = {r[1]*d[2], -r[0]*d[2], r[0]*d[1]-r[1]*d[0]};

    const float position[] = {0, 6.4f, spotSourceZ_, 1};
    const float color[] = {0.90f*light_.power, 0.95f*light_.power, light_.power, 1};
    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT1, GL_POSITION, position);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, color);
    glLightfv(GL_LIGHT1, GL_SPECULAR, color);
    glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, d.data());
    glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, std::atan(light_.slope) * 180.f/kPi);
    glLightf(GL_LIGHT1, GL_QUADRATIC_ATTENUATION, 0.024f);

    if (!spotFramebuffer_) return;
    const auto bind = GlProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    bind(GL_FRAMEBUFFER, spotFramebuffer_);
    glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT | GL_VIEWPORT_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, kSpotShadowSize, kSpotShadowSize);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.25f, 2.f);
    glClear(GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    const float edge = kSpotNear * light_.slope;
    glFrustum(-edge, edge, -edge, edge, kSpotNear, kSpotFar);
    const float target[] = {spotX_, spotY_, spotZ_};
    ApplyLookAt(light_.position.data(), target, light_.up.data());
    glGetFloatv(GL_PROJECTION_MATRIX, spotMatrix_.data());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    if (environments_[displayedScene_]) environments_[displayedScene_]->DrawDepth();
    DrawChickens(true);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
    bind(GL_FRAMEBUFFER, 0);
}

void Menu3D::SetSpotlightUniforms(const SpotUniforms& uniforms) const {
    const auto set4 = GlProc<PFNGLUNIFORM4FPROC>("glUniform4f");
    const auto active = GlProc<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    const auto& p = light_.position;
    const auto& d = light_.direction;
    set4(uniforms.position, p[0], p[1], p[2], light_.power);
    set4(uniforms.direction, d[0], d[1], d[2], light_.slope);
    GlProc<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv")(uniforms.matrix, 1, GL_FALSE, spotMatrix_.data());
    active(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, spotDepth_);
    active(GL_TEXTURE0);
}

void Menu3D::DrawSpotlightLens() {
    if (!surfaceProgram_ || light_.power <= 0.001f) return;
    const auto& p = light_.position;
    const auto& d = light_.direction;
    float facing = 0.f, distance2 = 0.f;
    for (int i = 0; i < 3; ++i) {
        float delta = cameraPos_[i] - p[i];
        facing += delta * d[i];
        distance2 += delta * delta;
    }
    facing /= std::sqrt(std::max(0.0001f, distance2));
    if (facing <= 0) return;
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
    glDisable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    // Radially graded disc in the plane of the lens. Geometry depth-tests the halo too.
    const float radii[] = {0.f, 0.056f, 0.075f, 0.15f};
    const float levels[] = {1.f, 0.90f, 0.16f, 0.f};
    for (int ring = 0; ring < 3; ++ring) {
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= 32; ++i) {
            const float angle = i * 2.f * kPi / 32;
            for (int band = ring; band <= ring+1; ++band) {
                glColor4f(.84f,.94f,1.f,levels[band]*facing*spotlightIntensity_);
                float v[3];
                for (int k = 0; k < 3; ++k)
                    v[k] = p[k] + radii[band]*(light_.right[k]*std::cos(angle) + light_.up[k]*std::sin(angle));
                glVertex3fv(v);
            }
        }
        glEnd();
    }
    glPopAttrib();
}

void Menu3D::ResizeSpotlightTargets() {
    if (!volumeFramebuffer_) return;
    const auto bind = GlProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    const auto attach = GlProc<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D");
    const auto status = GlProc<PFNGLCHECKFRAMEBUFFERSTATUSPROC>("glCheckFramebufferStatus");
    volumeWidth_ = (width_ + 1)/2;
    volumeHeight_ = (height_ + 1)/2;
    for (GLuint texture : {sceneDepthCopy_, volumeTexture_}) {
        glBindTexture(GL_TEXTURE_2D, texture);
        const bool depth = texture == sceneDepthCopy_;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, depth ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, depth ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, depth ? GL_DEPTH_COMPONENT24 : GL_RGBA8,
                     depth ? width_ : volumeWidth_, depth ? height_ : volumeHeight_, 0,
                     depth ? GL_DEPTH_COMPONENT : GL_RGBA, depth ? GL_UNSIGNED_INT : GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    bind(GL_FRAMEBUFFER, volumeFramebuffer_);
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, volumeTexture_, 0);
    if (status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        volumeWidth_ = 0;
        std::cerr << "[Menu3D] Spotlight scattering framebuffer unavailable\n";
    }
    bind(GL_FRAMEBUFFER, 0);
}

void Menu3D::DrawSpotlightVolume() {
    if (!volumeProgram_ || volumeWidth_ == 0 || light_.power <= 0.001f) return;
    // Project the pyramid clipped against the camera near plane. Limit both depth
    // resolve and integration to the footprint, including the bilinear filter border.
    float view[16], projection[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, view);
    glGetFloatv(GL_PROJECTION_MATRIX, projection);
    std::array<std::array<float,3>,5> eye{};
    for (int i = 0; i < 5; ++i) {
        auto point = light_.position;
        if (i > 0) {
            const float sx = (i == 1 || i == 4) ? -1.f : 1.f;
            const float sy = i <= 2 ? -1.f : 1.f;
            for (int k = 0; k < 3; ++k)
                point[k] += kSpotFar*(light_.direction[k] + light_.slope*(sx*light_.right[k]+sy*light_.up[k]));
        }
        for (int k = 0; k < 3; ++k)
            eye[i][k] = view[k]*point[0] + view[4+k]*point[1] + view[8+k]*point[2] + view[12+k];
    }
    float minX = 1.f, minY = 1.f, maxX = -1.f, maxY = -1.f;
    auto include = [&](float x, float y, float z) {
        x = std::clamp(-projection[0]*x/z, -1.f, 1.f);
        y = std::clamp(-projection[5]*y/z, -1.f, 1.f);
        minX = std::min(minX,x); maxX = std::max(maxX,x);
        minY = std::min(minY,y); maxY = std::max(maxY,y);
    };
    for (const auto& v : eye) if (v[2] <= -0.1f) include(v[0], v[1], v[2]);
    for (int i = 1; i <= 4; ++i) {
        for (int j : {0, i%4+1}) {
            const auto& a = eye[i];
            const auto& b = eye[j];
            if ((a[2] < -0.1f) == (b[2] < -0.1f)) continue;
            const float t = (-0.1f - a[2])/(b[2]-a[2]);
            include(a[0]+t*(b[0]-a[0]), a[1]+t*(b[1]-a[1]), -0.1f);
        }
    }
    if (minX >= maxX || minY >= maxY) return;
    auto bounds = [&](int w, int h, int pad) {
        int x = std::max(0, int((minX*.5f+.5f)*w)-pad);
        int y = std::max(0, int((minY*.5f+.5f)*h)-pad);
        int endX = std::min(w, int(std::ceil((maxX*.5f+.5f)*w))+pad);
        int endY = std::min(h, int(std::ceil((maxY*.5f+.5f)*h))+pad);
        return std::array<int,4>{x,y,endX-x,endY-y};
    };
    auto full = bounds(width_,height_,2);
    auto half = bounds(volumeWidth_,volumeHeight_,3);
    const auto bind = GlProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    const auto use = GlProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
    glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_SCISSOR_BIT | GL_DEPTH_BUFFER_BIT |
                 GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_CURRENT_BIT);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_LIGHTING);
    glDisable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glBindTexture(GL_TEXTURE_2D, sceneDepthCopy_);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, full[0], full[1], full[0], full[1], full[2], full[3]);
    bind(GL_FRAMEBUFFER, volumeFramebuffer_);
    glViewport(0,0,volumeWidth_,volumeHeight_);
    glScissor(half[0],half[1],half[2],half[3]);
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);
    use(volumeProgram_);
    SetSpotlightUniforms(volumeSpot_);
    GlProc<PFNGLUNIFORM3FVPROC>("glUniform3fv")(volumeCamera_,1,cameraPos_.data());
    // Humid mist, pale suspended sand, and a clearer sunlit courtyard.
    constexpr float medium[][3] = {{.074f,.090f,.11f},{.080f,.067f,.046f},{.036f,.035f,.031f}};
    const auto& m = medium[displayedScene_];
    GlProc<PFNGLUNIFORM4FPROC>("glUniform4f")(volumeMedium_,m[0],m[1],m[2],animationTime_);
    glRectf(-1,-1,1,1);
    use(0);
    bind(GL_FRAMEBUFFER,0);
    glViewport(0,0,width_,height_);
    glScissor(full[0],full[1],full[2],full[3]);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,volumeTexture_);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix(); glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix(); glLoadIdentity();
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); glVertex2f(-1,-1);
    glTexCoord2f(1,0); glVertex2f( 1,-1);
    glTexCoord2f(1,1); glVertex2f( 1, 1);
    glTexCoord2f(0,1); glVertex2f(-1, 1);
    glEnd();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION); glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
}

void Menu3D::DrawLegacySpotlight() {
    if (spotlightSuppressed_ || spotlightIntensity_ <= 0.001f) {
        glDisable(GL_LIGHT1);
        return;
    }
    const float si = std::clamp(spotlightIntensity_, 0.0f, 1.0f);
    const int targetIndex = (focusedObject_ >= 0) ? focusedObject_ : hoveredObject_;

    // 发射源在 3D 空间中保持高于地面，选图过场时沿屏幕竖直方向移动
    const GLfloat pos[] = { 0.0f, 6.4f, spotSourceZ_, 1.0f };
    float dirX = spotX_;
    float dirY = spotY_ - 6.4f;
    float dirZ = spotZ_ - spotSourceZ_;
    const float dirLen = std::sqrt(dirX * dirX + dirY * dirY + dirZ * dirZ);
    if (dirLen > 1e-5f) { dirX /= dirLen; dirY /= dirLen; dirZ /= dirLen; }
    const GLfloat dir[] = { dirX, dirY, dirZ };
    const GLfloat diff[] = { 0.86f * si, 0.93f * si, 1.0f * si, 1.0f };
    const float cutoff = (targetIndex >= 2) ? 20.0f : 31.0f;
    glEnable(GL_LIGHT1);
    glLightfv(GL_LIGHT1, GL_POSITION, pos);
    glLightfv(GL_LIGHT1, GL_DIFFUSE, diff);
    glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, dir);
    glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, cutoff);
    glLightf(GL_LIGHT1, GL_SPOT_EXPONENT, 7.0f);

    glDisable(GL_LIGHTING);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    const GLfloat beamAlpha = si * (targetIndex < 0 ? 0.06f : 0.20f);
    const bool calmIdleBeam = (targetIndex < 0);
    // 可见光锥：顶端跟随灯源，底端跟随目标
    const float br = !calmIdleBeam ? 0.72f : (displayedScene_ == 0 ? 0.58f : (displayedScene_ == 1 ? 0.96f : 0.98f));
    const float bg = !calmIdleBeam ? 0.86f : (displayedScene_ == 0 ? 0.74f : (displayedScene_ == 1 ? 0.80f : 0.88f));
    const float bb = !calmIdleBeam ? 1.00f : (displayedScene_ == 0 ? 0.86f : (displayedScene_ == 1 ? 0.56f : 0.66f));
    glColor4f(br, bg, bb, 0.24f * beamAlpha);
    glBegin(GL_QUAD_STRIP);
    constexpr int kSegments = 24;
    for (int i = 0; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        const float topR = 0.045f;
        glVertex3f(std::cos(a) * topR, 6.2f, spotSourceZ_ + std::sin(a) * topR);
        glVertex3f(spotX_ + std::cos(a) * spotRadius_, 0.02f,
                   spotZ_ + std::sin(a) * spotRadius_);
    }
    glEnd();

    // 地面光斑
    glColor4f(0.78f, 0.90f, 1.0f, 0.27f * beamAlpha);
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(spotX_, 0.008f, spotZ_);
    for (int i = 0; i <= kSegments; ++i) {
        const float a = 2.0f * kPi * static_cast<float>(i) / kSegments;
        glVertex3f(spotX_ + std::cos(a) * spotRadius_, 0.008f,
                   spotZ_ + std::sin(a) * spotRadius_);
    }
    glEnd();

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glEnable(GL_LIGHTING);

    // 随机探照时也画一个小光圈，明确灯在扫地面
    if (hoveredObject_ < 0 && focusedObject_ < 0) {
        DrawObjectRing(spotX_, spotZ_, 0.36f, 0.24f);
    }

    if (hoveredObject_ >= 0 || focusedObject_ >= 0) {
        const int idx = focusedObject_ >= 0 ? focusedObject_ : hoveredObject_;
        float x, y, z;
        if (focusedObject_ >= 0) { FocusCurrentPosition(x, z); }
        else ObjectWorldPosition(idx, x, y, z);
        const float r = (idx < 2) ? 0.82f : 0.30f;
        const float pulse = 0.65f + 0.35f * std::sin(animationTime_ * 5.0f);
        DrawObjectRing(x, z, r, 0.95f * pulse * si);
        DrawObjectRing(x, z, r * 0.72f, 0.55f * pulse * si);
    }
}

void Menu3D::DrawWeaponProp(float x, float y, float z) {
    if (focusWeapon_ >= 0 && focusWeapon_ < static_cast<int>(focusWeapons_.size()) &&
        focusWeapons_[focusWeapon_] && focusWeapons_[focusWeapon_]->IsReady()) {
        glPushMatrix();
        glTranslatef(x, y, z);
        glRotatef(focusWeapon_ == 2 ? 35.0f : -20.0f, 0.0f, 1.0f, 0.0f);
        focusWeapons_[focusWeapon_]->Draw(
            focusWeapon_ == 0 ? 0.28f : (focusWeapon_ == 1 ? 0.38f : 0.50f),
            0.0f, 0.0f);
        glPopMatrix();
        return;
    }

    glDisable(GL_LIGHTING);
    if (focusWeapon_ == 0) {
        // 步枪
        DrawCube(x, y, z, 0.95f, 0.10f, 0.10f, 0.10f, 0.11f, 0.13f);
        DrawCube(x - 0.35f, y - 0.02f, z, 0.36f, 0.22f, 0.18f, 0.22f, 0.18f, 0.13f);
        DrawCube(x + 0.25f, y, z - 0.06f, 0.30f, 0.06f, 0.06f, 0.38f, 0.32f, 0.22f);
    } else if (focusWeapon_ == 1) {
        // 手枪
        DrawCube(x, y, z, 0.38f, 0.12f, 0.10f, 0.10f, 0.11f, 0.13f);
        DrawCube(x - 0.06f, y - 0.20f, z, 0.12f, 0.32f, 0.10f, 0.18f, 0.16f, 0.14f);
    } else if (focusWeapon_ == 2) {
        // 匕首
        DrawCube(x, y, z, 0.52f, 0.06f, 0.04f, 0.72f, 0.74f, 0.78f);
        DrawCube(x - 0.30f, y, z, 0.16f, 0.10f, 0.08f, 0.20f, 0.16f, 0.10f);
    }
    glEnable(GL_LIGHTING);
}

void Menu3D::SetScene(int index, bool immediate) {
    index = std::clamp(index, 0, 2);
    if (immediate) {
        sceneIndex_ = displayedScene_ = index;
        sceneTransition_ = .5f;
        return;
    }
    if (index == sceneIndex_) return;
    sceneIndex_ = index;
    if (sceneTransition_ >= .25f) sceneTransition_ = .5f - sceneTransition_;
}

void Menu3D::DrawEnvironmentShadow(const Menu3DModel& model, const float sun[4]) {
    const auto bind = GlProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    bind(GL_FRAMEBUFFER, shadowFramebuffer_);
    glPushAttrib(GL_ENABLE_BIT | GL_POLYGON_BIT | GL_VIEWPORT_BIT | GL_DEPTH_BUFFER_BIT);
    glViewport(0, 0, kShadowSize, kShadowSize);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.5f, 2.0f);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Stable volume around the menu camera's courtyard and visible architecture.
    const float center[] = {0, 2, -7};
    const float eye[] = {center[0]+sun[0]*22, center[1]+sun[1]*22, center[2]+sun[2]*22};
    const float up[] = {0,1,0};
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(-19,19,-15,15,1,58);
    ApplyLookAt(eye, center, up);
    glGetFloatv(GL_PROJECTION_MATRIX, shadowMatrix_.data());
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
    model.DrawDepth();
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();
    bind(GL_FRAMEBUFFER, 0);
}

void Menu3D::UpdateLightning(float dt) {
    if (displayedScene_ != 0) {
        lightningFlash_ = 0.0f;
        lightningAge_ = -1.0f;
        lightningTimer_ = std::min(lightningTimer_, 2.2f);
        return;
    }
    if (lightningAge_ >= 0.0f) {
        lightningAge_ += dt;
        const float a = lightningAge_;
        if (a < 0.14f) {
            lightningFlash_ = std::clamp(std::exp(-a * 14.0f) * (0.75f + 0.25f * std::sin(a * 90.0f)), 0.0f, 1.0f);
        } else if (a < 0.42f) {
            const float b = a - 0.15f;
            lightningFlash_ = b > 0.0f ? std::clamp(0.76f * std::exp(-b * 13.0f), 0.0f, 1.0f) : 0.0f;
        } else {
            lightningAge_ = -1.0f;
            lightningFlash_ = 0.0f;
        }
        return;
    }
    lightningFlash_ = 0.0f;
    lightningTimer_ -= dt;
    if (lightningTimer_ <= 0.0f) {
        std::uniform_real_distribution<float> distNext(5.5f, 10.5f);
        std::uniform_real_distribution<float> distX(-7.5f, 7.5f);
        lightningTimer_ = distNext(chickenRng_);
        lightningX_ = distX(chickenRng_);
        lightningSeed_ = static_cast<unsigned int>(chickenRng_());
        lightningAge_ = 0.0f;
        lightningFlash_ = 1.0f;
    }
}

void Menu3D::ComputeSceneWeights(float& ruinsW, float& desertW, float& mirageW, float& fade) const {
    fade = std::clamp(std::fabs(sceneTransition_ - .25f) * 4.0f, 0.0f, 1.0f);
    ruinsW  = (displayedScene_ == 0) ? fade : 0.0f;
    desertW = (displayedScene_ == 1) ? fade : 0.0f;
    mirageW = (displayedScene_ == 2) ? fade : 0.0f;
}

void Menu3D::DrawAtmosphereSky(float ruinsW, float desertW, float mirageW, float fade) {
    if (!skyProgram_) return;
    const auto use = GlProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
    const auto set3f = GlProc<PFNGLUNIFORM3FPROC>("glUniform3f");
    const auto set4f = GlProc<PFNGLUNIFORM4FPROC>("glUniform4f");
    if (!use || !set3f || !set4f) return;

    float fx = cameraTarget_[0] - cameraPos_[0];
    float fy = cameraTarget_[1] - cameraPos_[1];
    float fz = cameraTarget_[2] - cameraPos_[2];
    const float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
    if (flen > 1e-6f) { fx /= flen; fy /= flen; fz /= flen; }
    float rx = fy * cameraUpZ_ - fz * cameraUpY_;
    float ry = -fx * cameraUpZ_;
    float rz = fx * cameraUpY_;
    const float rlen = std::sqrt(rx * rx + ry * ry + rz * rz);
    if (rlen > 1e-6f) { rx /= rlen; ry /= rlen; rz /= rlen; }
    const float ux = ry * fz - rz * fy;
    const float uy = rz * fx - rx * fz;
    const float uz = rx * fy - ry * fx;

    const float aspect = static_cast<float>(width_) / std::max(1, height_);
    const float cropScale = std::max(aspect / presentationAspect_, presentationAspect_ / aspect);
    const float tanY = std::tan(28.0f * kPi / 180.0f) * cropScale;
    const float flash = previewCapture_ ? 0.0f : lightningFlash_;

    use(skyProgram_);
    set3f(skyCamRight_, rx, ry, rz);
    set3f(skyCamUp_, ux, uy, uz);
    set3f(skyCamForward_, fx, fy, fz);
    set4f(skyParams_, tanY, aspect, previewCapture_ ? 0.0f : animationTime_, fade);
    set4f(skySceneWeights_, ruinsW, desertW, mirageW, flash);
    set4f(skyExtraParams_, std::clamp(lightningX_ / 12.0f, -0.8f, 0.8f), 0.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
    glVertex2f(-1.0f, -1.0f);
    glVertex2f( 1.0f, -1.0f);
    glVertex2f( 1.0f,  1.0f);
    glVertex2f(-1.0f,  1.0f);
    glEnd();
    use(0);
}

void Menu3D::DrawLightningBolt(float flash) {
    if (flash <= 0.06f) return;
    auto boltHash = [](unsigned int s, int idx) -> float {
        unsigned int n = s ^ (static_cast<unsigned int>(idx) * 2654435761u);
        n = (n ^ (n >> 13)) * 1274126177u;
        return static_cast<float>((n ^ (n >> 16)) & 0xFFFFu) / 65535.0f;
    };
    auto drawSegment = [](float x0, float y0, float x1, float y1, float z, float halfW,
                          float r, float g, float b, float a) {
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-5f) return;
        const float nx = (-dy / len) * halfW;
        const float ny = ( dx / len) * halfW;
        glColor4f(r, g, b, a);
        glVertex3f(x0 - nx, y0 - ny, z);
        glVertex3f(x0 + nx, y0 + ny, z);
        glVertex3f(x1 + nx, y1 + ny, z);
        glVertex3f(x1 - nx, y1 - ny, z);
    };

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_FOG);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    constexpr int kSteps = 14;
    constexpr float kZ = -21.0f;
    std::array<float, kSteps + 1> bx{}, by{};
    bx[0] = lightningX_;
    by[0] = 14.5f;
    for (int i = 1; i <= kSteps; ++i) {
        const float frac = static_cast<float>(i) / kSteps;
        const float jitter = (boltHash(lightningSeed_, i) - 0.5f) * 1.65f * (1.0f - frac * 0.25f);
        bx[i] = bx[i - 1] + jitter;
        by[i] = 14.5f - frac * 10.6f;
    }

    glBegin(GL_QUADS);
    // Outer violet-blue ionized glow
    for (int i = 0; i < kSteps; ++i) {
        drawSegment(bx[i], by[i], bx[i + 1], by[i + 1], kZ, 0.22f, 0.52f, 0.72f, 1.00f, 0.42f * flash);
    }
    // Bright white-blue lightning core
    for (int i = 0; i < kSteps; ++i) {
        drawSegment(bx[i], by[i], bx[i + 1], by[i + 1], kZ, 0.055f, 0.96f, 0.98f, 1.00f, 0.95f * flash);
    }
    // 2 jagged forks branching off main channel
    for (int b = 0; b < 2; ++b) {
        const int root = 3 + b * 4;
        float fx = bx[root], fy = by[root];
        const float dir = (b == 0 ? -1.0f : 1.0f) * (0.65f + 0.4f * boltHash(lightningSeed_, 50 + b));
        for (int s = 0; s < 5; ++s) {
            const float nx = fx + dir * (0.45f + 0.45f * boltHash(lightningSeed_, 60 + b * 10 + s));
            const float ny = fy - (0.65f + 0.35f * boltHash(lightningSeed_, 80 + b * 10 + s));
            drawSegment(fx, fy, nx, ny, kZ, 0.11f, 0.52f, 0.72f, 1.00f, 0.28f * flash);
            drawSegment(fx, fy, nx, ny, kZ, 0.028f, 0.92f, 0.96f, 1.00f, 0.72f * flash);
            fx = nx; fy = ny;
        }
    }
    glEnd();
    glPopAttrib();
}

void Menu3D::DrawAtmosphereParticles(bool foreground, float ruinsW, float desertW, float mirageW) {
    if (!sandWispTex_ || !sandStreakTex_) return;
    const float topDownFade = 1.0f - std::clamp(cameraTopDown_ * 1.25f, 0.0f, 0.88f);
    const float t = previewCapture_ ? 0.0f : animationTime_;
    const float flash = previewCapture_ ? 0.0f : lightningFlash_;

    float mv[16]{};
    glGetFloatv(GL_MODELVIEW_MATRIX, mv);
    const float rightX = mv[0], rightY = mv[4], rightZ = mv[8];
    const float upX = mv[1], upY = mv[5], upZ = mv[9];

    auto hash01 = [](int idx, int salt) -> float {
        unsigned int n = static_cast<unsigned int>(idx) * 73856093u ^ static_cast<unsigned int>(salt) * 19349663u;
        n = (n ^ (n >> 13)) * 1274126177u;
        return static_cast<float>((n ^ (n >> 16)) & 0xFFFFu) / 65535.0f;
    };

    auto drawQuad = [&](float cx, float cy, float cz, float hw, float hh, float tiltRad,
                        float r, float g, float b, float a) {
        const float cs = std::cos(tiltRad);
        const float sn = std::sin(tiltRad);
        const float ax = (rightX * cs + upX * sn) * hw;
        const float ay = (rightY * cs + upY * sn) * hw;
        const float az = (rightZ * cs + upZ * sn) * hw;
        const float bx = (-rightX * sn + upX * cs) * hh;
        const float by = (-rightY * sn + upY * cs) * hh;
        const float bz = (-rightZ * sn + upZ * cs) * hh;
        glColor4f(r, g, b, a);
        glTexCoord2f(0.0f, 0.0f); glVertex3f(cx - ax - bx, cy - ay - by, cz - az - bz);
        glTexCoord2f(1.0f, 0.0f); glVertex3f(cx + ax - bx, cy + ay - by, cz + az - bz);
        glTexCoord2f(1.0f, 1.0f); glVertex3f(cx + ax + bx, cy + ay + by, cz + az + bz);
        glTexCoord2f(0.0f, 1.0f); glVertex3f(cx - ax + bx, cy - ay + by, cz - az + bz);
    };

    // Distant jagged lightning bolt behind ruins skyline
    if (!foreground && ruinsW > 0.01f && flash > 0.06f) {
        DrawLightningBolt(flash * ruinsW);
    }

    glPushAttrib(GL_ENABLE_BIT | GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_FOG);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    // ---- 1. Ancient Ruins: Storm Rain, Mist & Ground Splashes ----
    const float rWeight = ruinsW * topDownFade;
    if (rWeight > 0.01f) {
        if (!foreground) {
            // Humid rainforest mist drifting across the ruins
            glBindTexture(GL_TEXTURE_2D, sandWispTex_);
            glBegin(GL_QUADS);
            for (int i = 0; i < 24; ++i) {
                const float u0 = hash01(i, 201), u1 = hash01(i, 202), u2 = hash01(i, 203), u3 = hash01(i, 204);
                const float phase = std::fmod((t * (0.45f + u0 * 0.4f) / 24.0f) + u1, 1.0f);
                const float life = std::sin(phase * kPi);
                const float x = -12.0f + phase * 24.0f;
                const float z = -10.5f + u2 * 10.5f;
                const float y = 0.7f + u3 * 3.4f + std::sin(t * 0.4f + i) * 0.14f;
                const float hw = 2.6f + u0 * 2.2f;
                const float hh = hw * 0.42f;
                const float mr = 0.42f + 0.52f * flash;
                const float mg = 0.52f + 0.46f * flash;
                const float mb = 0.54f + 0.46f * flash;
                drawQuad(x, y, z, hw, hh, (u3 - 0.5f) * 0.16f, mr, mg, mb, (0.048f + 0.03f * flash) * life * rWeight);
            }
            glEnd();
        }
        // Rain streaks (long vertical-tilted streaks falling downward with subtle wind slant)
        glBindTexture(GL_TEXTURE_2D, sandStreakTex_);
        glBegin(GL_QUADS);
        const int rainCount = foreground ? 240 : 420;
        const float tilt = 1.43f; // ~82 deg -> hw becomes vertical drop length, hh becomes thin drop width
        const float litR = 0.72f + 0.28f * flash;
        const float litG = 0.82f + 0.18f * flash;
        const float litB = 0.90f + 0.10f * flash;
        for (int i = 0; i < rainCount; ++i) {
            const float u0 = hash01(i, foreground ? 311 : 301);
            const float u1 = hash01(i, foreground ? 312 : 302);
            const float u2 = hash01(i, foreground ? 313 : 303);
            const float u3 = hash01(i, foreground ? 314 : 304);
            const float dropSpan = foreground ? 6.2f : 9.5f;
            const float speed = (foreground ? 14.5f : 11.0f) + u0 * 4.5f;
            const float phase = std::fmod((t * speed / dropSpan) + u1, 1.0f);
            const float y = (foreground ? 5.2f : 8.5f) - phase * dropSpan;
            const float x = ((u2 - 0.5f) * (foreground ? 11.0f : 22.0f)) - phase * 1.25f;
            const float z = foreground ? (3.0f + u3 * 4.2f) : (-10.0f + u3 * 12.5f);
            const float len = foreground ? (0.24f + u0 * 0.18f) : (0.30f + u0 * 0.22f);
            const float thick = foreground ? (0.006f + u3 * 0.003f) : (0.0075f + u3 * 0.004f);
            const float edgeFade = std::sin(std::clamp(phase, 0.02f, 0.98f) * kPi);
            const float alpha = (foreground ? 0.32f : 0.27f) * (0.65f + 0.35f * u0) * (1.0f + 0.75f * flash) * edgeFade * rWeight;
            drawQuad(x, y, z, len, thick, tilt, litR, litG, litB, alpha);
        }
        // Ground rain splashes on the stone courtyard
        if (!foreground) {
            for (int i = 0; i < 150; ++i) {
                const float u0 = hash01(i, 351), u1 = hash01(i, 352), u2 = hash01(i, 353), u3 = hash01(i, 354);
                const float cycle = std::fmod(t * (2.6f + u0 * 2.2f) + u1 * 10.0f, 1.0f);
                const float burst = std::sin(cycle * kPi);
                const float x = (u2 - 0.5f) * 14.0f;
                const float z = -5.5f + u3 * 11.5f;
                const float y = 0.018f + cycle * 0.032f;
                const float rSplash = 0.028f + cycle * 0.055f;
                const float alpha = 0.30f * burst * (1.0f + 0.7f * flash) * rWeight;
                drawQuad(x, y, z, rSplash, 0.009f, 0.0f, litR, litG, litB, alpha);
            }
        }
        glEnd();
    }

    // ---- 2. Dust_2: Subtle Sandstorm (Wisps + Ground Sweep + Motes + Lens Streaks) ----
    const float dWeight = desertW * topDownFade;
    if (dWeight > 0.01f) {
        glBindTexture(GL_TEXTURE_2D, sandWispTex_);
        glBegin(GL_QUADS);
        const int wispCount = foreground ? 16 : 38;
        for (int i = 0; i < wispCount; ++i) {
            const float u0 = hash01(i, foreground ? 101 : 1);
            const float u1 = hash01(i, foreground ? 102 : 2);
            const float u2 = hash01(i, foreground ? 103 : 3);
            const float u3 = hash01(i, foreground ? 104 : 4);
            const float u4 = hash01(i, foreground ? 105 : 5);
            const float span = foreground ? 13.0f : 28.0f;
            const float speed = (foreground ? 2.2f : 1.25f) + u0 * (foreground ? 1.5f : 1.15f);
            const float phase = std::fmod((t * speed / span) + u1, 1.0f);
            const float life = std::sin(phase * kPi);
            const float x = -span * 0.5f + phase * span;
            const float z = foreground ? (3.2f + u2 * 3.6f) : (-8.6f + u2 * 10.6f);
            const float yBase = foreground ? (0.40f + u3 * 2.20f) : (0.65f + u3 * 3.90f);
            const float y = yBase + std::sin(t * (0.45f + u4 * 0.35f) + i) * 0.16f - phase * 0.22f;
            const float hw = foreground ? (1.7f + u4 * 1.4f) : (2.4f + u4 * 2.5f);
            const float hh = hw * (0.40f + 0.16f * u0);
            const float tilt = -0.10f + (u3 - 0.5f) * 0.22f;
            const float alpha = (foreground ? 0.068f : 0.118f) * (0.65f + 0.35f * u0) * life * dWeight;
            const float sunTint = 0.88f + 0.14f * u2;
            drawQuad(x, y, z, hw, hh, tilt, 0.94f * sunTint, 0.70f * sunTint, 0.41f * sunTint, alpha);
        }
        glEnd();

        glBindTexture(GL_TEXTURE_2D, sandStreakTex_);
        glBegin(GL_QUADS);
        if (!foreground) {
            for (int i = 0; i < 160; ++i) {
                const float u0 = hash01(i, 21), u1 = hash01(i, 22), u2 = hash01(i, 23), u3 = hash01(i, 24);
                const float span = 22.0f;
                const float speed = 3.9f + u0 * 3.4f;
                const float phase = std::fmod((t * speed / span) + u1, 1.0f);
                const float life = std::sin(phase * kPi);
                const float x = -span * 0.5f + phase * span;
                const float z = -6.5f + u2 * 12.0f;
                const float y = 0.04f + u3 * 0.58f + std::sin(t * 2.5f + i) * 0.04f;
                drawQuad(x, y, z, 0.25f + u0 * 0.38f, 0.016f + u3 * 0.024f, -0.05f, 0.96f, 0.76f, 0.48f, (0.14f + 0.17f * u2) * life * dWeight);
            }
            for (int i = 0; i < 280; ++i) {
                const float u0 = hash01(i, 41), u1 = hash01(i, 42), u2 = hash01(i, 43), u3 = hash01(i, 44);
                const float span = 24.0f;
                const float speed = 1.4f + u0 * 2.4f;
                const float phase = std::fmod((t * speed / span) + u1, 1.0f);
                const float life = std::sin(phase * kPi);
                const float x = -span * 0.5f + phase * span;
                const float z = -7.8f + u2 * 13.5f;
                const float y = 0.18f + u3 * 5.0f + std::sin(t * (1.2f + u0) + i * 0.7f) * 0.18f;
                const float twinkle = 0.65f + 0.35f * std::sin(t * (3.5f + u2 * 4.0f) + i);
                const float size = 0.022f + u0 * 0.028f;
                drawQuad(x, y, z, size * 1.45f, size * 0.72f, -0.08f, 1.00f, 0.86f, 0.60f, (0.22f + 0.32f * u3) * life * twinkle * dWeight);
            }
        } else {
            for (int i = 0; i < 155; ++i) {
                const float u0 = hash01(i, 71), u1 = hash01(i, 72), u2 = hash01(i, 73), u3 = hash01(i, 74);
                const float span = 12.0f;
                const float speed = 5.4f + u0 * 4.4f;
                const float phase = std::fmod((t * speed / span) + u1, 1.0f);
                const float life = std::sin(phase * kPi);
                const float x = -span * 0.5f + phase * span;
                const float z = 3.2f + u2 * 4.0f;
                const float y = 0.08f + u3 * 2.85f - phase * 0.25f;
                drawQuad(x, y, z, 0.12f + u0 * 0.19f, 0.006f + u3 * 0.009f, -0.09f - u0 * 0.05f, 1.00f, 0.85f, 0.58f, (0.18f + 0.24f * u2) * life * dWeight);
            }
        }
        glEnd();
    }

    // ---- 3. Mirage: Rising Convective Heat Plumes & Shimmering Hot Motes ----
    const float mWeight = mirageW * topDownFade;
    if (mWeight > 0.01f) {
        glBindTexture(GL_TEXTURE_2D, sandWispTex_);
        glBegin(GL_QUADS);
        const int plumeCount = foreground ? 10 : 22;
        for (int i = 0; i < plumeCount; ++i) {
            const float u0 = hash01(i, foreground ? 511 : 501);
            const float u1 = hash01(i, foreground ? 512 : 502);
            const float u2 = hash01(i, foreground ? 513 : 503);
            const float u3 = hash01(i, foreground ? 514 : 504);
            const float ySpan = foreground ? 2.8f : 4.2f;
            const float phase = std::fmod((t * (0.45f + u0 * 0.35f) / ySpan) + u1, 1.0f);
            const float life = std::sin(phase * kPi);
            const float x = (u2 - 0.5f) * (foreground ? 10.0f : 18.0f) + std::sin(t * 1.4f + i) * 0.25f;
            const float z = foreground ? (3.0f + u3 * 3.8f) : (-8.0f + u3 * 11.0f);
            const float y = 0.15f + phase * ySpan;
            const float hw = foreground ? (1.4f + u0 * 1.0f) : (2.2f + u0 * 1.8f);
            const float hh = hw * 0.55f;
            drawQuad(x, y, z, hw, hh, 1.57f + std::sin(t + i) * 0.15f, 1.00f, 0.88f, 0.64f, (foreground ? 0.032f : 0.048f) * life * mWeight);
        }
        glEnd();

        glBindTexture(GL_TEXTURE_2D, sandStreakTex_);
        glBegin(GL_QUADS);
        const int heatMoteCount = foreground ? 80 : 210;
        for (int i = 0; i < heatMoteCount; ++i) {
            const float u0 = hash01(i, foreground ? 561 : 551);
            const float u1 = hash01(i, foreground ? 562 : 552);
            const float u2 = hash01(i, foreground ? 563 : 553);
            const float u3 = hash01(i, foreground ? 564 : 554);
            const float ySpan = foreground ? 3.2f : 5.2f;
            const float phase = std::fmod((t * (0.35f + u0 * 0.55f) / ySpan) + u1, 1.0f);
            const float life = std::sin(phase * kPi);
            const float x = (u2 - 0.5f) * (foreground ? 9.5f : 19.0f) + std::sin(t * (1.8f + u0) + i) * 0.18f;
            const float z = foreground ? (3.2f + u3 * 3.8f) : (-7.5f + u3 * 12.5f);
            const float y = 0.08f + phase * ySpan;
            const float twinkle = 0.55f + 0.45f * std::sin(t * (4.2f + u2 * 4.0f) + i);
            const float sz = 0.016f + u0 * 0.022f;
            drawQuad(x, y, z, sz * 0.85f, sz * 1.35f, std::sin(t * 2.0f + i) * 0.25f, 1.00f, 0.92f, 0.68f, (0.20f + 0.26f * u3) * life * twinkle * mWeight);
        }
        glEnd();
    }

    glPopAttrib();
}

void Menu3D::ApplyAtmospherePost(float ruinsW, float desertW, float mirageW) {
    if (!bloomFramebuffer_ || !bloomExtractProgram_ || !bloomBlurProgram_) return;
    const float flash = previewCapture_ ? 0.0f : lightningFlash_;
    const float bloomStrength = ruinsW * (0.07f + 0.44f * flash) + desertW * 0.24f + mirageW * 0.22f;
    if (bloomStrength <= 0.01f && mirageW <= 0.01f) return;

    const auto bindFbo = GlProc<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    const auto attach = GlProc<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D");
    const auto use = GlProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
    const auto active = GlProc<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
    const auto set1i = GlProc<PFNGLUNIFORM1IPROC>("glUniform1i");
    const auto set1f = GlProc<PFNGLUNIFORM1FPROC>("glUniform1f");
    const auto set2f = GlProc<PFNGLUNIFORM2FPROC>("glUniform2f");
    const auto set3f = GlProc<PFNGLUNIFORM3FPROC>("glUniform3f");
    const auto set4f = GlProc<PFNGLUNIFORM4FPROC>("glUniform4f");
    if (!bindFbo || !attach || !use || !active || !set1i || !set1f || !set2f || !set3f || !set4f) return;

    glPushAttrib(GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT | GL_TEXTURE_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_FOG);
    glDepthMask(GL_FALSE);
    active(GL_TEXTURE0);

    // Capture the backbuffer (sRGB encoded in GL_SRGB8_ALPHA8 texture)
    glBindTexture(GL_TEXTURE_2D, sceneCopyTex_);
    glReadBuffer(GL_BACK);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, sceneCopyW_, sceneCopyH_);

    glDisable(GL_FRAMEBUFFER_SRGB);
    bindFbo(GL_FRAMEBUFFER, bloomFramebuffer_);
    glViewport(0, 0, bloomW_, bloomH_);
    glDisable(GL_BLEND);

    auto drawFullscreenQuad = []() {
        glBegin(GL_QUADS);
        glVertex2f(-1.0f, -1.0f);
        glVertex2f( 1.0f, -1.0f);
        glVertex2f( 1.0f,  1.0f);
        glVertex2f(-1.0f,  1.0f);
        glEnd();
    };

    const float threshold = ruinsW * 0.58f + desertW * 0.56f + mirageW * 0.52f;
    const float tr = ruinsW * (0.86f + 0.20f * flash) + desertW * 1.12f + mirageW * 1.18f;
    const float tg = ruinsW * (0.94f + 0.15f * flash) + desertW * 0.96f + mirageW * 1.02f;
    const float tb = ruinsW * (1.12f + 0.25f * flash) + desertW * 0.72f + mirageW * 0.76f;

    // Pass 1: Extract bright highlights into bloomTex_[0]
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[0], 0);
    use(bloomExtractProgram_);
    set1i(bloomExtractTex_, 0);
    set1f(bloomExtractThreshold_, threshold);
    if (bloomExtractTint_ >= 0) set3f(bloomExtractTint_, tr, tg, tb);
    glBindTexture(GL_TEXTURE_2D, sceneCopyTex_);
    drawFullscreenQuad();

    // Pass 2 & 3: Separable Gaussian blur
    use(bloomBlurProgram_);
    set1i(bloomBlurTex_, 0);
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[1], 0);
    set2f(bloomBlurOffset_, 1.8f / static_cast<float>(bloomW_), 0.0f);
    glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
    drawFullscreenQuad();

    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloomTex_[0], 0);
    set2f(bloomBlurOffset_, 0.0f, 1.8f / static_cast<float>(bloomH_));
    glBindTexture(GL_TEXTURE_2D, bloomTex_[1]);
    drawFullscreenQuad();

    // Composite back onto default sRGB backbuffer
    bindFbo(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    if (surfaceProgram_) glEnable(GL_FRAMEBUFFER_SRGB);

    if (postHeatProgram_) {
        use(postHeatProgram_);
        set1i(postHeatSceneTex_, 0);
        set1i(postHeatBloomTex_, 1);
        set4f(postHeatParams_, mirageW, previewCapture_ ? 0.0f : animationTime_, cameraTopDown_, flash * ruinsW);
        set4f(postHeatBloomTint_, bloomStrength, bloomStrength * 0.94f, bloomStrength * 0.84f, 1.0f);
        active(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
        active(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, sceneCopyTex_);
        drawFullscreenQuad();
        active(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        active(GL_TEXTURE0);
        use(0);
    } else {
        use(0);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, bloomTex_[0]);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor4f(bloomStrength, bloomStrength * 0.92f, bloomStrength * 0.78f, 1.0f);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f( 1.0f, -1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f( 1.0f,  1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f,  1.0f);
        glEnd();
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
    glPopAttrib();
}

void Menu3D::DrawEnvironment() {
    const bool ruins = displayedScene_ == 0;
    const bool desert = displayedScene_ == 1;
    const bool mirage = displayedScene_ == 2;
    float ruinsW = 0.0f, desertW = 0.0f, mirageW = 0.0f, fade = 1.0f;
    ComputeSceneWeights(ruinsW, desertW, mirageW, fade);
    const float flash = previewCapture_ ? 0.0f : lightningFlash_;
    const auto atmosphere = [fade](float value, float neutral) {
        return neutral + (value - neutral) * fade;
    };
    glDisable(GL_LIGHTING);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    if (skyProgram_) {
        DrawAtmosphereSky(ruinsW, desertW, mirageW, fade);
    } else {
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glBegin(GL_QUADS);
        const GLfloat horizon[][3] = {{.60f,.72f,.64f},{.86f,.74f,.57f},{.91f,.76f,.63f}};
        const GLfloat zenith[][3] = {{.23f,.43f,.46f},{.28f,.44f,.53f},{.35f,.53f,.67f}};
        const auto& low = horizon[displayedScene_];
        const auto& high = zenith[displayedScene_];
        glColor4f(atmosphere(low[0],.24f),atmosphere(low[1],.27f),atmosphere(low[2],.27f),1);
        glVertex2f(-1,-1); glVertex2f(1,-1);
        glColor4f(atmosphere(high[0],.12f),atmosphere(high[1],.20f),atmosphere(high[2],.24f),1);
        glVertex2f(1,1); glVertex2f(-1,1);
        glEnd();
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    // The permanent underfloor keeps a continuous stage throughout the transition.
    DrawCube(0,-.05f,0,40,.05f,48,
             atmosphere(ruins ? .22f : (desert ? .56f : .62f),.34f),
             atmosphere(ruins ? .24f : (desert ? .44f : .49f),.32f),
             atmosphere(ruins ? .21f : (desert ? .28f : .32f),.27f));
    glEnable(GL_LIGHTING);
    const GLfloat horizonFog[][3] = {{.34f,.44f,.44f},{.88f,.70f,.46f},{.94f,.78f,.54f}};
    const auto& low = horizonFog[displayedScene_];
    const GLfloat haze[] = {atmosphere(low[0],.24f), atmosphere(low[1],.27f),
                            atmosphere(low[2],.27f), 1.0f};
    constexpr float fogRanges[][2] = {{17,41},{17,42},{18,40}};
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogfv(GL_FOG_COLOR, haze);
    glFogf(GL_FOG_START, fogRanges[displayedScene_][0]);
    glFogf(GL_FOG_END, fogRanges[displayedScene_][1]);
    if (environments_[displayedScene_]) {
        glPushAttrib(GL_LIGHTING_BIT);
        const GLfloat sun[] = {-1.12f,.71f,desert ? .55f : -.83f,0};
        const float flashAdd = ruins ? flash * 0.50f : 0.0f;
        const GLfloat warmth[] = {
            ruins ? (0.48f + flashAdd * 0.85f) : (mirage ? 1.06f : 0.98f),
            ruins ? (0.52f + flashAdd * 0.95f) : (mirage ? 0.90f : 0.81f),
            ruins ? (0.54f + flashAdd * 1.15f) : (mirage ? 0.64f : 0.58f), 1.0f
        };
        const GLfloat skylights[][4] = {
            {0.20f + flashAdd * 0.25f, 0.26f + flashAdd * 0.30f, 0.28f + flashAdd * 0.40f, 1},
            {.38f,.31f,.24f,1},
            {.34f,.32f,.30f,1}
        };
        const GLfloat fill[] = {
            ruins ? (0.14f + flashAdd * 0.20f) : (mirage ? 0.28f : 0.26f),
            ruins ? (0.20f + flashAdd * 0.25f) : (mirage ? 0.26f : 0.24f),
            ruins ? (0.24f + flashAdd * 0.35f) : (mirage ? 0.24f : 0.22f), 1.0f
        };
        const GLfloat fillDirection[] = {.85f,.55f,-.62f,0};
        const auto& skylight = skylights[displayedScene_];
        glLightfv(GL_LIGHT2, GL_POSITION, fillDirection);
        glLightfv(GL_LIGHT2, GL_DIFFUSE, fill);
        glLightfv(GL_LIGHT0, GL_POSITION, sun);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, warmth);
        glLightfv(GL_LIGHT0, GL_AMBIENT, skylight);
        glDisable(GL_LIGHT1);
        auto& environment = *environments_[displayedScene_];
        if (surfaceProgram_) {
            const auto use = GlProc<PFNGLUSEPROGRAMPROC>("glUseProgram");
            const auto active = GlProc<PFNGLACTIVETEXTUREPROC>("glActiveTexture");
            const auto setMatrix = GlProc<PFNGLUNIFORMMATRIX4FVPROC>("glUniformMatrix4fv");
            const auto setInt = GlProc<PFNGLUNIFORM1IPROC>("glUniform1i");
            const auto set3f = GlProc<PFNGLUNIFORM3FPROC>("glUniform3f");
            const auto set4f = GlProc<PFNGLUNIFORM4FPROC>("glUniform4f");
            if (shadowDepth_) DrawEnvironmentShadow(environment, sun);
            use(surfaceProgram_);
            SetSpotlightUniforms(surfaceSpot_);
            setInt(surfaceHasShadow_, shadowDepth_ != 0);
            if (surfaceSceneWeights_ >= 0 && set4f) {
                set4f(surfaceSceneWeights_, ruinsW, desertW, mirageW, flash);
            }
            if (surfaceEnvParams_ >= 0 && set4f) {
                set4f(surfaceEnvParams_, previewCapture_ ? 0.0f : animationTime_, cameraTopDown_, fade, 0.0f);
            }
            if (surfaceCameraPos_ >= 0 && set3f) {
                set3f(surfaceCameraPos_, cameraPos_[0], cameraPos_[1], cameraPos_[2]);
            }
            if (shadowDepth_) {
                setMatrix(surfaceShadowMatrix_, 1, GL_FALSE, shadowMatrix_.data());
                active(GL_TEXTURE4);
                glBindTexture(GL_TEXTURE_2D, shadowDepth_);
                active(GL_TEXTURE0);
            }
            environment.DrawRaw(fade, &surfaceUniforms_);
            active(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, 0);
            active(GL_TEXTURE0);
            use(0);
        } else {
            environment.DrawRaw(fade);
        }
        glPopAttrib();
    } else {
        DrawCube(0,-.05f,0,40,.1f,48,.32f,.29f,.23f);
    }
    glDisable(GL_FOG);
    DrawAtmosphereParticles(false, ruinsW, desertW, mirageW);
}

void Menu3D::DrawFocusScene() {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float nearZ = 0.1f;
    const float cropScale = std::max(aspect / presentationAspect_, presentationAspect_ / aspect);
    const float top = nearZ * std::tan(28.0f * kPi / 180.0f) * cropScale;
    const float right = top * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, nearZ, 40.0f);

    // 相机从主菜单正面视角平滑过渡到聚焦视角，和模型移动/放大同步
    const float focusRaw = std::clamp(focusProgress_, 0.0f, 1.0f);
    const float focusT = focusRaw * focusRaw * (3.0f - 2.0f * focusRaw);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    ApplyFocusCamera(focusT, cameraPos_.data(), cameraTarget_.data());
    cameraUpY_ = 1.0f;
    cameraUpZ_ = 0.0f;

    PrepareSpotlight();
    DrawEnvironment();

    if (!surfaceProgram_) DrawLegacySpotlight();
    const float t = focusT;
    DrawChickens(false);
    DrawSpotlightLens();
    DrawSpotlightVolume();
    float x, z;
    FocusCurrentPosition(x, z);
    DrawObjectRing(x, z, 0.40f, 0.8f);
    {
        float ruinsW = 0.0f, desertW = 0.0f, mirageW = 0.0f, fade = 1.0f;
        ComputeSceneWeights(ruinsW, desertW, mirageW, fade);
        const float focusReduce = 1.0f - 0.65f * t;
        DrawAtmosphereParticles(true, ruinsW * focusReduce, desertW * focusReduce, mirageW * focusReduce);
        ApplyAtmospherePost(ruinsW, desertW, mirageW);
    }
}


void Menu3D::Render(float dt) {
    if (!ready_ || !glContext_) return;

    const float frameDt = std::clamp(dt, 0.0f, 0.05f);
    animationTime_ += frameDt;
    pendingAnimationTime_ = std::min(pendingAnimationTime_ + frameDt, .10f);
    const uint64_t now = SDL_GetTicks64();
    if (lastRenderTicks_ != 0 && now - lastRenderTicks_ < 33) return;
    lastRenderTicks_ = now;

    if (SDL_GL_MakeCurrent(glWindow_, glContext_) != 0) return;
    FollowOutputSize();
    glViewport(0, 0, width_, height_);

    const float animDt = pendingAnimationTime_;
    pendingAnimationTime_ = 0.0f;
    sceneTransition_ = std::min(.5f, sceneTransition_ + animDt);
    if (sceneTransition_ >= .25f) displayedScene_ = sceneIndex_;
    if (environments_[displayedScene_]) environments_[displayedScene_]->Update(animDt);
    for (int i = 0; i < static_cast<int>(chickens_.size()) && i < 3; ++i) {
        // Match the baked walking stride to world movement.
        const std::string& anim = chickens_[i]->AnimationName();
        float rate = 1.0f;
        if (anim == "Menu_Walk") {
            rate = ChickenAnimTimeScale(chickenState_[i]);
        }
        chickens_[i]->Update(animDt * rate);
    }
    if (focusedObject_ < 0) UpdateChickens(animDt);
    UpdateSpotlight(animDt);
    UpdateLightning(animDt);

    if (focusedObject_ >= 0) {
        DrawFocusScene();
    } else {
        DrawScene(animationTime_);
        UpdateObjectScreenPositions();
    }
    ReadbackToTexture(texture_);
    SDL_GL_SwapWindow(glWindow_);
}

void Menu3D::DrawScene(float timeSec) {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const float nearZ = 0.1f;
    const float farZ = 40.0f;
    const float cropScale = std::max(aspect / presentationAspect_, presentationAspect_ / aspect);
    const float top = nearZ * std::tan(28.0f * kPi / 180.0f) * cropScale;
    const float right = top * aspect;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right, right, -top, top, nearZ, farZ);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    if (cameraTopDown_ > 0.001f) {
        // 前视 → 四分之一圆弧 → 正上方俯视
        const float raw = std::clamp(cameraTopDown_, 0.0f, 1.0f);
        const float t = raw * raw * (3.0f - 2.0f * raw);
        const float angle = t * (kPi * 0.5f);
        const float eye[3] = {
            0.0f,
            kMainCamY + (kTopCamY - kMainCamY) * std::sin(angle),
            kMainCamZ * std::cos(angle) + 5.0f * std::sin(kPi * t) + 0.001f
        };
        const float targetY = kMainLookY + (0.85f - kMainLookY) * t;
        const float target[3] = { 0.0f, targetY, 0.0f };
        // 从“屏幕上方为 +Y”平滑过渡到俯视时的“屏幕上方为 -Z”
        float up[3] = {
            0.0f,
            1.0f - t,
            -t
        };
        const float ul = std::sqrt(up[1] * up[1] + up[2] * up[2]);
        if (ul > 1e-5f) { up[1] /= ul; up[2] /= ul; }
        std::memcpy(cameraPos_.data(), eye, sizeof(eye));
        std::memcpy(cameraTarget_.data(), target, sizeof(target));
        cameraUpY_ = up[1];
        cameraUpZ_ = up[2];
        ApplyLookAt(eye, target, up);
    } else {
        ApplyMainCamera(timeSec, cameraPos_.data(), cameraTarget_.data());
        cameraUpY_ = 1.0f;
        cameraUpZ_ = 0.0f;
    }
    PrepareSpotlight();
    DrawEnvironment();

    // 预览抓拍时只画环境本体：不画聚光灯光柱、角色与小鸡、悬停光圈。
    // 否则三个预览会被漫游光柱漂白、被同一组角色/小鸡占满，看不出场景区别。
    if (!previewCapture_) {
        if (!surfaceProgram_) DrawLegacySpotlight();

        if (chickens_.size() == 3) {
            for (size_t i = 0; i < chickens_.size(); ++i) {
                const auto& state = chickenState_[i];
                const float size = std::max(0.04f, (8.6f - state.z) / 6.f);
                DrawShadow(state.x, state.z, 0.20f * size, 0.15f * size, 0.34f);
            }
            DrawChickens(false);
        } else {
            // 小鸡模型加载失败时的程序化替身；T/CT 已移除，不再画替身角色
            DrawChicken(-1.6f, 2.2f, timeSec * 2.4f);
            DrawChicken( 0.0f, 2.8f, timeSec * 2.4f + 1.7f);
            DrawChicken( 1.55f, 2.1f, timeSec * 2.4f + 3.4f);
        }

        DrawSpotlightLens();
        DrawSpotlightVolume();
        if (hoveredObject_ >= 0) {
            float x, y, z;
            ObjectWorldPosition(hoveredObject_, x, y, z);
            const float r = 0.30f;
            const float pulse = 0.65f + 0.35f * std::sin(animationTime_ * 5.0f);
            DrawObjectRing(x, z, r, 0.95f * pulse);
            DrawObjectRing(x, z, r * 0.72f, 0.55f * pulse);
        }
    }
    {
        float ruinsW = 0.0f, desertW = 0.0f, mirageW = 0.0f, fade = 1.0f;
        ComputeSceneWeights(ruinsW, desertW, mirageW, fade);
        DrawAtmosphereParticles(true, ruinsW, desertW, mirageW);
        ApplyAtmospherePost(ruinsW, desertW, mirageW);
    }
}

void Menu3D::ReadbackToTexture(SDL_Texture* target) {
    if (!target || pixels_.empty()) return;
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());

    // glTF material factors and fixed-function lighting are linear. Encode RGB
    // for the SDL display texture, retaining its original coverage alpha.
    static const std::array<uint8_t, 256> srgb = [] {
        std::array<uint8_t, 256> values{};
        for (size_t index = 0; index < values.size(); ++index) {
            const float linear = static_cast<float>(index) / 255.0f;
            const float encoded = linear <= .0031308f ? linear * 12.92f
                : 1.055f * std::pow(linear, 1.0f / 2.4f) - .055f;
            values[index] = static_cast<uint8_t>(std::lround(encoded * 255.0f));
        }
        return values;
    }();
    const size_t stride = static_cast<size_t>(width_) * 4;
    for (int y = 0; y < height_; ++y) {
        const uint8_t* src = pixels_.data() + static_cast<size_t>(height_ - 1 - y) * stride;
        uint8_t* dst = flipped_.data() + static_cast<size_t>(y) * stride;
        // A live surface program uses framebuffer sRGB for the entire composite.
        if (surfaceProgram_) {
            std::memcpy(dst, src, stride);
            continue;
        }
        for (int x = 0; x < width_; ++x) {
            dst[x * 4] = srgb[src[x * 4]];
            dst[x * 4 + 1] = srgb[src[x * 4 + 1]];
            dst[x * 4 + 2] = srgb[src[x * 4 + 2]];
            dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }
    SDL_UpdateTexture(target, nullptr, flipped_.data(), static_cast<int>(stride));
}

// 预览抓拍：逐个切到三个环境，只画环境本体并回读为预览纹理。
// 在 Initialize() 末尾做一次（与主 3D 纹理同分辨率）。
// 环境 .glb 加载失败的槽位保持空纹理，
// 面板按原有逻辑跳过绘制（GameUI.cpp 的 if (ctx.menuScenePreviews[i]) 守卫）。
void Menu3D::CaptureScenePreviews() {
    if (!glContext_ || !mainRenderer_) return;
    if (SDL_GL_MakeCurrent(glWindow_, glContext_) != 0) return;
    glViewport(0, 0, width_, height_);

    const int savedScene = displayedScene_;
    const float savedTransition = sceneTransition_;
    const float savedCamera = cameraTopDown_;
    const bool savedSuppressed = spotlightSuppressed_;
    sceneTransition_ = .5f;
    cameraTopDown_ = 0;
    spotlightSuppressed_ = true;
    previewCapture_ = true;
    for (int i = 0; i < 3; ++i) {
        if (!environments_[i]) continue;
        displayedScene_ = i;
        SDL_Texture* preview = SDL_CreateTexture(
            mainRenderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, width_, height_);
        if (!preview) {
            std::cerr << "[Menu3D] 预览纹理创建失败: " << SDL_GetError() << std::endl;
            continue;
        }
        DrawScene(0.0f);
        ReadbackToTexture(preview);
        SDL_SetTextureScaleMode(preview, SDL_ScaleModeLinear);
        if (scenePreviews_[i]) SDL_DestroyTexture(scenePreviews_[i]);
        scenePreviews_[i] = preview;
    }
    previewCapture_ = false;
    spotlightSuppressed_ = savedSuppressed;
    sceneTransition_ = savedTransition;
    cameraTopDown_ = savedCamera;
    displayedScene_ = savedScene;
}
