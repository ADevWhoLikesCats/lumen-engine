# lumen-engine

A small deferred renderer in C++17 and OpenGL 3.3 core.

## Status

Working G-buffer pipeline with cascaded shadow maps.

## Pipeline

Per frame:

1. **Shadow pass** — 3 cascades (8 m / 24 m / 80 m), rendered into a
   `2048x2048x3` depth texture array with 5x5 PCF.
2. **Geometry pass** — writes to a 4-attachment G-buffer:
   - attachment 0: lit HDR color (RGBA16F)
   - attachment 1: albedo (RGBA16F)
   - attachment 2: normal + roughness (RGBA16F)
   - attachment 3: emissive (RGBA16F)
3. **Tonemap pass** — ACES tonemap + sRGB, full-screen triangle.

## Planned

- SSAO (Phase 2)
- PBR material system — normal maps, metalness maps (Phase 3)
- Spherical harmonics ambient (Phase 4)
- Volumetric fog (Phase 5)
- Screen-space reflections (Phase 6)
- Temporal anti-aliasing (Phase 7)

## Build

    make

Requires: g++, libglfw3-dev, libglm-dev, libtinygltf-dev

## Run

    ./lumen
