# lumen-engine

A small deferred 3D renderer written in C++17 and OpenGL 3.3 core.

## Status

Work in progress. The current commit is a forward-rendered baseline
(Lambert lighting, shadow map, instanced rendering) that will be
replaced by a deferred PBR pipeline.

## Planned pipeline

- G-buffer / deferred lighting
- Physically based shading (Cook-Torrance)
- SSAO
- Spherical harmonics ambient
- Volumetric fog
- Screen-space reflections
- Temporal anti-aliasing

## Build

    make

Requires: g++, libglfw3-dev, libglm-dev, libtinygltf-dev

## Run

    ./lumen
