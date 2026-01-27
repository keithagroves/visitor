#pragma once

#include <cmath>

class PerlinNoise {
public:
    PerlinNoise(unsigned int seed = 0) {
        for (int i = 0; i < 256; i++) perm[i] = i;
        for (int i = 255; i > 0; i--) {
            seed = seed * 1103515245 + 12345;
            int j = (seed >> 16) % (i + 1);
            int tmp = perm[i];
            perm[i] = perm[j];
            perm[j] = tmp;
        }
        for (int i = 0; i < 256; i++) perm[256 + i] = perm[i];
    }

    float noise(float x, float y) const {
        int xi = (int)floorf(x) & 255;
        int yi = (int)floorf(y) & 255;
        float xf = x - floorf(x);
        float yf = y - floorf(y);

        float u = fade(xf);
        float v = fade(yf);

        int aa = perm[perm[xi] + yi];
        int ab = perm[perm[xi] + yi + 1];
        int ba = perm[perm[xi + 1] + yi];
        int bb = perm[perm[xi + 1] + yi + 1];

        float x1 = lerp(grad(aa, xf, yf), grad(ba, xf - 1, yf), u);
        float x2 = lerp(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u);

        return lerp(x1, x2, v);
    }

    float octaveNoise(float x, float y, int octaves, float persistence) const {
        float total = 0;
        float frequency = 1;
        float amplitude = 1;
        float maxValue = 0;

        for (int i = 0; i < octaves; i++) {
            total += noise(x * frequency, y * frequency) * amplitude;
            maxValue += amplitude;
            amplitude *= persistence;
            frequency *= 2;
        }

        return total / maxValue;
    }

private:
    int perm[512];

    static float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
    static float lerp(float a, float b, float t) { return a + t * (b - a); }
    static float grad(int hash, float x, float y) {
        int h = hash & 3;
        float u = h < 2 ? x : y;
        float v = h < 2 ? y : x;
        return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
    }
};
