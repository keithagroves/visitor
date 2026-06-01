#pragma once

// All shader source code in one place

namespace Shaders {

const char* terrainVertex = R"(#version 300 es
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aHeight;

out vec3 vNormal;
out vec3 vWorldPos;
out float vHeight;

uniform mat4 uMVP;
uniform mat4 uModel;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vWorldPos = (uModel * vec4(aPosition, 1.0)).xyz;
    vHeight = aHeight;
}
)";

const char* terrainFragment = R"(#version 300 es
precision mediump float;

in vec3 vNormal;
in vec3 vWorldPos;
in float vHeight;

out vec4 fragColor;

uniform vec3 uLightDir;
uniform vec3 uCameraPos;
uniform float uTime;

void main() {
    vec3 color;
    float h = vHeight;

    if (h < 0.0) {
        color = mix(vec3(0.0, 0.1, 0.3), vec3(0.0, 0.3, 0.5), h + 1.0);
    } else if (h < 0.15) {
        color = mix(vec3(0.0, 0.3, 0.5), vec3(0.76, 0.70, 0.50), h / 0.15);
    } else if (h < 0.4) {
        color = mix(vec3(0.2, 0.5, 0.1), vec3(0.1, 0.4, 0.1), (h - 0.15) / 0.25);
    } else if (h < 0.7) {
        color = mix(vec3(0.4, 0.35, 0.3), vec3(0.5, 0.5, 0.5), (h - 0.4) / 0.3);
    } else {
        color = mix(vec3(0.5, 0.5, 0.5), vec3(1.0, 1.0, 1.0), (h - 0.7) / 0.3);
    }

    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);

    float ambient = 0.3;
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 16.0) * 0.3;

    vec3 finalColor = color * (ambient + diff * 0.7) + vec3(spec);

    float dist = length(uCameraPos - vWorldPos);
    float fogFactor = 1.0 - exp(-dist * 0.0015);  // Fog starts further away
    vec3 fogColor = vec3(0.7, 0.8, 0.95);
    finalColor = mix(finalColor, fogColor, fogFactor);

    fragColor = vec4(finalColor, 1.0);
}
)";

const char* waterVertex = R"(#version 300 es
layout(location = 0) in vec3 aPosition;

out vec3 vWorldPos;

uniform mat4 uMVP;
uniform float uTime;

void main() {
    vec3 pos = aPosition;
    // Larger, slower waves for bigger scale
    pos.y += sin(pos.x * 0.1 + uTime * 0.5) * 0.3 + cos(pos.z * 0.1 + uTime * 0.3) * 0.3;
    gl_Position = uMVP * vec4(pos, 1.0);
    vWorldPos = pos;
}
)";

const char* waterFragment = R"(#version 300 es
precision mediump float;

in vec3 vWorldPos;
out vec4 fragColor;

uniform vec3 uCameraPos;
uniform float uTime;

void main() {
    vec3 waterColor = vec3(0.0, 0.4, 0.7);

    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float fresnel = pow(1.0 - abs(viewDir.y), 2.0);

    vec3 color = mix(waterColor, vec3(0.5, 0.7, 0.9), fresnel * 0.5);

    float dist = length(uCameraPos - vWorldPos);
    float fogFactor = 1.0 - exp(-dist * 0.0015);  // Match terrain fog
    vec3 fogColor = vec3(0.7, 0.8, 0.95);
    color = mix(color, fogColor, fogFactor);

    fragColor = vec4(color, 0.8);
}
)";

const char* crosshairVertex = R"(#version 300 es
layout(location = 0) in vec2 aPosition;

void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
}
)";

const char* crosshairFragment = R"(#version 300 es
precision mediump float;
out vec4 fragColor;

void main() {
    fragColor = vec4(1.0, 1.0, 1.0, 0.8);
}
)";

// Debug line shader for 3D lines
const char* debugLineVertex = R"(#version 300 es
layout(location = 0) in vec3 aPosition;

uniform mat4 uMVP;

void main() {
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

const char* debugLineFragment = R"(#version 300 es
precision mediump float;
out vec4 fragColor;

uniform vec3 uColor;

void main() {
    fragColor = vec4(uColor, 1.0);
}
)";

// Underwater surface - view from below
const char* underwaterVertex = R"(#version 300 es
layout(location = 0) in vec3 aPosition;

out vec3 vWorldPos;

uniform mat4 uMVP;
uniform float uTime;

void main() {
    vec3 pos = aPosition;
    // Same waves as surface water
    pos.y += sin(pos.x * 0.1 + uTime * 0.5) * 0.3 + cos(pos.z * 0.1 + uTime * 0.3) * 0.3;
    gl_Position = uMVP * vec4(pos, 1.0);
    vWorldPos = pos;
}
)";

const char* underwaterFragment = R"(#version 300 es
precision mediump float;

in vec3 vWorldPos;
out vec4 fragColor;

uniform vec3 uCameraPos;
uniform float uTime;

void main() {
    // Darker, murkier color when looking up from below
    vec3 underwaterColor = vec3(0.0, 0.25, 0.45);

    vec3 viewDir = normalize(uCameraPos - vWorldPos);

    // Caustic-like light patterns
    float caustic = sin(vWorldPos.x * 0.3 + uTime) * cos(vWorldPos.z * 0.3 + uTime * 0.7) * 0.5 + 0.5;
    caustic = caustic * 0.3 + 0.7;

    vec3 color = underwaterColor * caustic;

    // Light rays coming from above
    float sunRay = pow(max(viewDir.y, 0.0), 2.0) * 0.4;
    color += vec3(0.1, 0.3, 0.4) * sunRay;

    float dist = length(uCameraPos - vWorldPos);
    float fogFactor = 1.0 - exp(-dist * 0.003);  // Denser fog underwater
    vec3 fogColor = vec3(0.0, 0.15, 0.3);
    color = mix(color, fogColor, fogFactor);

    fragColor = vec4(color, 0.85);
}
)";

// Full-screen underwater overlay effect
const char* underwaterOverlayVertex = R"(#version 300 es
layout(location = 0) in vec2 aPosition;

out vec2 vUV;

void main() {
    gl_Position = vec4(aPosition, 0.0, 1.0);
    vUV = aPosition * 0.5 + 0.5;
}
)";

const char* underwaterOverlayFragment = R"(#version 300 es
precision mediump float;

in vec2 vUV;
out vec4 fragColor;

uniform float uTime;

void main() {
    // Wavy distortion pattern
    float wave = sin(vUV.y * 20.0 + uTime * 2.0) * 0.02;
    wave += sin(vUV.x * 15.0 + uTime * 1.5) * 0.015;

    // Blue-green tint overlay
    vec3 tint = vec3(0.0, 0.2, 0.4);

    // Vignette effect - darker at edges
    float vignette = 1.0 - length(vUV - 0.5) * 0.8;

    // Light caustics on screen
    float caustic = sin((vUV.x + wave) * 30.0 + uTime * 3.0) *
                    cos((vUV.y + wave) * 25.0 + uTime * 2.5);
    caustic = caustic * 0.5 + 0.5;
    caustic = pow(caustic, 3.0) * 0.15;

    fragColor = vec4(tint + caustic, 0.35 * vignette);
}
)";

// Character shader - blobby jelly effect with vertex colors
const char* characterVertex = R"(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vColor;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uTime;
uniform float uSpeed;

void main() {
    vec3 pos = aPosition;

    // Jelly wobble - different frequencies for organic feel
    float wobble1 = sin(uTime * 4.0 + pos.y * 3.0) * 0.03;
    float wobble2 = cos(uTime * 5.0 + pos.y * 2.0 + pos.x * 2.0) * 0.02;
    float wobble3 = sin(uTime * 3.0 + pos.z * 4.0) * 0.02;

    // Apply wobble to x and z (side-to-side jiggle)
    pos.x += wobble1 + wobble2;
    pos.z += wobble2 + wobble3;

    // Squash and stretch based on height (bottom stays grounded)
    float heightFactor = pos.y / 1.8;
    float squash = sin(uTime * 6.0) * 0.08 * heightFactor;
    float stretch = cos(uTime * 6.0) * 0.05;

    pos.y += squash;
    pos.x *= 1.0 + stretch * heightFactor;
    pos.z *= 1.0 + stretch * heightFactor;

    // Bounce effect when moving
    float sinVal = sin(uTime * 8.0);
    float bounce = (sinVal < 0.0 ? -sinVal : sinVal) * 0.05 * uSpeed;
    pos.y += bounce * heightFactor;

    // Bulge in middle (breathing effect)
    float bulge = sin(uTime * 2.0) * 0.04 + 0.02;
    float yDiff = pos.y - 0.9;
    float midHeight = 1.0 - (yDiff < 0.0 ? -yDiff : yDiff) / 0.9;
    pos.x *= 1.0 + bulge * midHeight;
    pos.z *= 1.0 + bulge * midHeight;

    gl_Position = uMVP * vec4(pos, 1.0);

    // Wobbly normal for shiny jelly look
    vec3 norm = aNormal;
    norm.x += wobble1 * 2.0;
    norm.z += wobble2 * 2.0;
    vNormal = normalize(mat3(uModel) * norm);

    vWorldPos = (uModel * vec4(pos, 1.0)).xyz;
    vColor = aColor;
}
)";

const char* characterFragment = R"(#version 300 es
precision mediump float;

in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vColor;

out vec4 fragColor;

uniform vec3 uLightDir;
uniform vec3 uCameraPos;
uniform float uTime;

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);

    // Brighter ambient for jelly look
    float ambient = 0.5;
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    vec3 reflectDir = reflect(-lightDir, normal);

    // Strong specular for shiny jelly surface
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32.0) * 0.6;

    // Fresnel rim lighting for translucent jelly edge glow
    float fresnel = pow(1.0 - max(dot(viewDir, normal), 0.0), 3.0);
    vec3 rimColor = vColor * 1.5 + vec3(0.2, 0.3, 0.4);

    // Subsurface scattering approximation - light from behind
    float sss = max(dot(-viewDir, lightDir), 0.0) * 0.3;
    vec3 sssColor = vColor * 1.2;

    vec3 finalColor = vColor * (ambient + diff * 0.5);
    finalColor += vec3(spec);  // Shiny highlights
    finalColor += rimColor * fresnel * 0.4;  // Edge glow
    finalColor += sssColor * sss;  // Backlight glow

    // Slight color shift over time for extra jelly feel
    finalColor += vec3(0.02) * sin(uTime * 2.0);

    // Distance fog
    float dist = length(uCameraPos - vWorldPos);
    float fogFactor = 1.0 - exp(-dist * 0.0015);
    vec3 fogColor = vec3(0.7, 0.8, 0.95);
    finalColor = mix(finalColor, fogColor, fogFactor);

    fragColor = vec4(finalColor, 1.0);
}
)";

// Skybox shader - procedural gradient sky with sun and clouds
const char* skyboxVertex = R"(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPosition;

out vec3 vDirection;

uniform mat4 uMVP;

void main() {
    vDirection = aPosition;
    vec4 pos = uMVP * vec4(aPosition, 1.0);
    gl_Position = pos.xyww;  // Depth = 1.0 (far plane)
}
)";

const char* skyboxFragment = R"(#version 300 es
precision mediump float;

in vec3 vDirection;
out vec4 fragColor;

uniform float uTime;
uniform vec3 uSunDirection;

// Simple noise for clouds
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);

    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));

    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm(vec2 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 4; i++) {
        sum += noise(p) * amp;
        p *= 2.0;
        amp *= 0.5;
    }
    return sum;
}

void main() {
    vec3 dir = normalize(vDirection);

    // Sky gradient
    float y = dir.y * 0.5 + 0.5;
    vec3 horizonColor = vec3(0.8, 0.85, 0.95);
    vec3 zenithColor = vec3(0.4, 0.6, 0.9);
    vec3 skyColor = mix(horizonColor, zenithColor, pow(y, 0.7));

    // Sun
    vec3 sunDir = normalize(uSunDirection);
    float sunDot = max(dot(dir, sunDir), 0.0);
    float sunDisc = smoothstep(0.995, 0.999, sunDot);
    vec3 sunColor = vec3(1.0, 0.95, 0.8);
    float sunGlow = pow(sunDot, 8.0) * 0.5;

    // Clouds
    if (dir.y > 0.0) {
        vec2 cloudUV = dir.xz / (dir.y + 0.1) * 2.0;
        cloudUV += uTime * 0.02;  // Slow cloud movement

        float clouds = fbm(cloudUV * 1.5);
        clouds = smoothstep(0.3, 0.7, clouds);

        // Cloud color - white with slight blue tint in shadows
        vec3 cloudColor = mix(vec3(0.9, 0.92, 0.95), vec3(1.0), clouds);
        float cloudShadow = 1.0 - clouds * 0.3;

        skyColor = mix(skyColor, cloudColor, clouds * 0.6 * smoothstep(0.0, 0.3, dir.y));
    }

    // Combine
    skyColor += sunColor * sunDisc;
    skyColor += sunColor * sunGlow;

    // Slight fog near horizon
    float horizonFog = 1.0 - smoothstep(0.0, 0.15, dir.y);
    skyColor = mix(skyColor, horizonColor, horizonFog * 0.5);

    fragColor = vec4(skyColor, 1.0);
}
)";

// Tree shader - low poly trees with wind sway
const char* treeVertex = R"(#version 300 es
precision mediump float;

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

out vec3 vNormal;
out vec3 vWorldPos;
out vec3 vColor;
out vec3 vLocalPos;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform float uTime;

void main() {
    vec3 pos = aPosition;

    // Wind sway - stronger at top of tree
    float windStrength = pos.y * 0.03;
    float windX = sin(uTime * 1.5 + pos.x * 0.5) * windStrength;
    float windZ = cos(uTime * 1.2 + pos.z * 0.5) * windStrength * 0.7;

    pos.x += windX;
    pos.z += windZ;

    gl_Position = uMVP * vec4(pos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vWorldPos = (uModel * vec4(pos, 1.0)).xyz;
    vLocalPos = aPosition;  // Original local position for texturing
    vColor = aColor;
}
)";

const char* treeFragment = R"(#version 300 es
precision mediump float;

in vec3 vNormal;
in vec3 vWorldPos;
in vec3 vColor;
in vec3 vLocalPos;

out vec4 fragColor;

uniform vec3 uLightDir;
uniform vec3 uCameraPos;
uniform float uTime;
uniform float uHighlight;

// Pseudo-random hash functions
float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float hash(vec3 p) {
    return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

// Value noise
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);  // Smoothstep
    
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float noise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    
    float n = mix(
        mix(mix(hash(i), hash(i + vec3(1,0,0)), f.x),
            mix(hash(i + vec3(0,1,0)), hash(i + vec3(1,1,0)), f.x), f.y),
        mix(mix(hash(i + vec3(0,0,1)), hash(i + vec3(1,0,1)), f.x),
            mix(hash(i + vec3(0,1,1)), hash(i + vec3(1,1,1)), f.x), f.y),
        f.z);
    return n;
}

// Fractal Brownian Motion
float fbm(vec2 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

float fbm(vec3 p, int octaves) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < octaves; i++) {
        value += amplitude * noise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

// Bark texture - vertical grooves with noise
vec3 barkTexture(vec3 pos) {
    // Base bark colors
    vec3 darkBark = vec3(0.25, 0.15, 0.08);
    vec3 lightBark = vec3(0.45, 0.30, 0.18);
    vec3 midBark = vec3(0.35, 0.22, 0.12);
    
    // Vertical groove pattern
    float angle = atan(pos.z, pos.x);
    float grooves = sin(angle * 12.0 + pos.y * 3.0) * 0.5 + 0.5;
    grooves = pow(grooves, 0.7);
    
    // Add noise for natural variation
    float n1 = fbm(pos * 8.0, 3);
    float n2 = fbm(pos * 20.0 + 100.0, 2);
    
    // Horizontal rings (subtle)
    float rings = sin(pos.y * 15.0 + n1 * 2.0) * 0.5 + 0.5;
    rings = smoothstep(0.3, 0.7, rings) * 0.3;
    
    // Combine patterns
    float pattern = grooves * 0.6 + n1 * 0.3 + rings;
    pattern += n2 * 0.15;  // Fine detail
    
    // Mix colors based on pattern
    vec3 color = mix(darkBark, lightBark, pattern);
    color = mix(color, midBark, n1 * 0.5);
    
    return color;
}

// Leaf/foliage texture - clustered look with variation
vec3 foliageTexture(vec3 pos, vec3 baseColor) {
    // Determine if this is green (foliage) by checking the base color
    float greenness = baseColor.g - max(baseColor.r, baseColor.b);
    
    // Multi-scale noise for leaf clusters
    float n1 = fbm(pos * 5.0, 3);
    float n2 = noise(pos * 15.0);
    float n3 = noise(pos * 30.0);
    
    // Create leaf cluster pattern
    float clusters = smoothstep(0.3, 0.7, n1);
    
    // Color variations for leaves
    vec3 darkGreen = vec3(0.08, 0.28, 0.06);
    vec3 midGreen = vec3(0.12, 0.38, 0.10);
    vec3 lightGreen = vec3(0.18, 0.48, 0.14);
    vec3 yellowGreen = vec3(0.25, 0.45, 0.12);
    
    // Mix greens based on noise
    vec3 leafColor = mix(darkGreen, midGreen, n1);
    leafColor = mix(leafColor, lightGreen, n2 * 0.5);
    leafColor = mix(leafColor, yellowGreen, smoothstep(0.6, 0.9, n3) * 0.3);
    
    // Add subtle highlights on outer edges (fake subsurface)
    float edge = 1.0 - abs(dot(normalize(pos), vec3(0, 1, 0)));
    leafColor += vec3(0.05, 0.08, 0.02) * edge * n2;
    
    // Small dark spots (depth/shadow between leaves)
    float spots = smoothstep(0.4, 0.45, n3);
    leafColor = mix(leafColor, darkGreen * 0.7, spots * 0.4);
    
    return leafColor;
}

void main() {
    vec3 normal = normalize(vNormal);
    vec3 lightDir = normalize(uLightDir);
    
    // Detect if trunk (brown) or foliage (green) based on vertex color
    float isTrunk = step(vColor.g, vColor.r + 0.1);  // Brown has more red than green
    
    // Apply procedural textures
    vec3 texColor;
    if (isTrunk > 0.5) {
        texColor = barkTexture(vLocalPos);
    } else {
        texColor = foliageTexture(vLocalPos + vWorldPos * 0.1, vColor);
    }

    // Lighting
    float ambient = 0.35;
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Softer shadows for foliage (fake subsurface scattering)
    if (isTrunk < 0.5) {
        diff = diff * 0.7 + 0.3;  // Lift shadows on leaves
    }

    vec3 finalColor = texColor * (ambient + diff * 0.65);
    
    // Subtle rim lighting for depth
    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float rim = 1.0 - max(dot(viewDir, normal), 0.0);
    rim = pow(rim, 3.0) * 0.15;
    finalColor += rim * vec3(0.6, 0.7, 0.5);

    // Distance fog
    float dist = length(uCameraPos - vWorldPos);
    float fogFactor = 1.0 - exp(-dist * 0.0015);
    vec3 fogColor = vec3(0.7, 0.8, 0.95);
    finalColor = mix(finalColor, fogColor, fogFactor);

    // Highlight effect when targeted
    if (uHighlight > 0.5) {
        // Pulsing glow effect
        float pulse = sin(uTime * 4.0) * 0.5 + 0.5;

        // Bright outline/glow
        vec3 highlightColor = vec3(1.0, 0.9, 0.5);  // Golden yellow

        // Add glow to the whole tree
        finalColor = mix(finalColor, highlightColor, 0.3 + pulse * 0.2);

        // Extra bright rim
        float highlightRim = pow(rim, 1.5) * 0.8;
        finalColor += highlightColor * highlightRim * (0.8 + pulse * 0.4);
    }

    fragColor = vec4(finalColor, 1.0);
}
)";

} // namespace Shaders
