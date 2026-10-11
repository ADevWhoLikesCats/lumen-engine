# lumen-engine

A small deferred renderer in C++17 and OpenGL 3.3 core.

## Status

Working PBR pipeline with cascaded shadows, SSAO, SSGI, and
spherical-harmonics ambient.

## Pipeline (per frame)

    shadow pass (3 cascades, 2048x2048x3)
      -> SSAO (reads previous depth)
      -> SSGI (reads previous lit color + G-buffer)
      -> geometry pass (PBR, writes 4-attachment G-buffer)
      -> blit gbuf.color -> sceneFB.color
      -> tonemap (ACES + sRGB)
      -> screen

## Features

- Deferred G-buffer (lit color / albedo / normal+roughness / emissive)
- Physically based shading (Cook-Torrance, GGX + Smith + Schlick)
- Cascaded shadow maps (3 cascades, 5x5 PCF)
- Screen-space ambient occlusion
- Screen-space global illumination
- Spherical harmonics ambient (octahedral probe capture)
- ACES tonemapping

## Planned

- Temporal anti-aliasing
- Screen-space reflections
- Spherical Gaussian probe reflections
- Volumetric fog

## Build

    make

Requires: g++, libglfw3-dev, libglm-dev, libtinygltf-dev

## Run

    ./lumen
