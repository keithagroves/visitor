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

} // namespace Shaders
