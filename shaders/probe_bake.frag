#version 330 core
precision highp float;
precision highp int;
precision highp usampler2D;

#define PROBE_COUNT_X 8
#define PROBE_COUNT_Y 4
#define PROBE_COUNT_Z 8
#define PROBE_SG_COUNT 32

#define GRID_X 32
#define GRID_Y 16
#define GRID_Z 32

uniform vec3  uGridMin;
uniform vec3  uGridMax;
uniform vec3  uCarGridMin;
uniform vec3  uCarGridMax;
uniform sampler2D uTriTex;
uniform sampler2D uCellTex;
uniform usampler2D uIdxTex;
uniform int       uTriPerRow;
uniform float     uIdxTexW;

uniform float uGroundY;
uniform vec3  uGroundAlbedo;
uniform vec3  uSkyHorizon;
uniform vec3  uSkyZenith;
uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform float uLightIntensity;

out vec4 fragColor;

const float PI = 3.14159265359;

vec3 fib_dir(int i, int N) {
    float golden = PI * (1.0 + sqrt(5.0));
    float phi   = acos(1.0 - 2.0 * (float(i) + 0.5) / float(N));
    float theta = golden * float(i);
    return vec3(sin(phi) * cos(theta), sin(phi) * sin(theta), cos(phi));
}

bool ray_tri(vec3 ro, vec3 rd, vec3 v0, vec3 v1, vec3 v2, out float t) {
    vec3 e1 = v1 - v0;
    vec3 e2 = v2 - v0;
    vec3 pv = cross(rd, e2);
    float det = dot(e1, pv);
    if (abs(det) < 1e-9) return false;
    float inv = 1.0 / det;
    vec3 tv = ro - v0;
    float u = dot(tv, pv) * inv;
    if (u < 0.0 || u > 1.0) return false;
    vec3 qv = cross(tv, e1);
    float v = dot(rd, qv) * inv;
    if (v < 0.0 || u + v > 1.0) return false;
    t = dot(e2, qv) * inv;
    return t > 1e-4;
}

void fetch_tri(int triIdx, out vec3 a, out vec3 b, out vec3 c, out vec3 alb) {
    int row = triIdx / uTriPerRow;
    int col = (triIdx % uTriPerRow) * 3;
    vec4 t0 = texelFetch(uTriTex, ivec2(col + 0, row), 0);
    vec4 t1 = texelFetch(uTriTex, ivec2(col + 1, row), 0);
    vec4 t2 = texelFetch(uTriTex, ivec2(col + 2, row), 0);
    a = t0.xyz;
    b = t1.xyz;
    c = t2.xyz;
    alb = vec3(t0.w, t1.w, t2.w);
}

ivec2 cell_tex_coord(int cx, int cy, int cz) {
    int col = cy * GRID_X + cx;
    return ivec2(col, cz);
}

int fetch_idx(int flat_index) {
    int iw = int(uIdxTexW);
    ivec2 c = ivec2(flat_index % iw, flat_index / iw);
    return int(texelFetch(uIdxTex, c, 0).r);
}

float trace_grid(vec3 ro, vec3 rd, out vec3 outNormal, out vec3 outAlbedo) {
    vec3 gridSize = uCarGridMax - uCarGridMin;
    if (gridSize.x < 1e-6 || gridSize.y < 1e-6 || gridSize.z < 1e-6) {
        outNormal = vec3(0,1,0); outAlbedo = vec3(0);
        return 1e30;
    }

    vec3 invCell = vec3(float(GRID_X)/gridSize.x, float(GRID_Y)/gridSize.y, float(GRID_Z)/gridSize.z);
    vec3 ro_g = (ro - uCarGridMin) * invCell;
    vec3 rd_g = rd * invCell;

    ivec3 istep = ivec3(sign(rd_g));
    ivec3 cur = ivec3(floor(ro_g));
    cur = clamp(cur, ivec3(0), ivec3(GRID_X-1, GRID_Y-1, GRID_Z-1));

    vec3 tDelta = abs(1.0 / max(abs(rd_g), vec3(1e-12)));
    vec3 tMax;
    if (istep.x > 0) tMax.x = (float(cur.x + 1) - ro_g.x) / rd_g.x;
    else if (istep.x < 0) tMax.x = (float(cur.x) - ro_g.x) / rd_g.x;
    else tMax.x = 1e30;
    if (istep.y > 0) tMax.y = (float(cur.y + 1) - ro_g.y) / rd_g.y;
    else if (istep.y < 0) tMax.y = (float(cur.y) - ro_g.y) / rd_g.y;
    else tMax.y = 1e30;
    if (istep.z > 0) tMax.z = (float(cur.z + 1) - ro_g.z) / rd_g.z;
    else if (istep.z < 0) tMax.z = (float(cur.z) - ro_g.z) / rd_g.z;
    else tMax.z = 1e30;

    float tBest = 1e30;
    vec3 nBest = vec3(0,1,0);
    vec3 aBest = vec3(0);

    for (int step = 0; step < 96; ++step) {
        if (cur.x < 0 || cur.x >= GRID_X || cur.y < 0 || cur.y >= GRID_Y ||
            cur.z < 0 || cur.z >= GRID_Z) break;

        vec4 sc = texelFetch(uCellTex, cell_tex_coord(cur.x, cur.y, cur.z), 0);
        int start = int(sc.x);
        int cnt   = int(sc.y);

        for (int k = 0; k < cnt; ++k) {
            int triIdx = fetch_idx(start + k);
            vec3 a, b, c, alb;
            fetch_tri(triIdx, a, b, c, alb);
            float t;
            if (ray_tri(ro, rd, a, b, c, t) && t < tBest) {
                tBest = t;
                nBest = normalize(cross(b - a, c - a));
                aBest = alb;
            }
        }

        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            if (tMax.x > tBest) break;
            cur.x += istep.x; tMax.x += tDelta.x;
        } else if (tMax.y < tMax.z) {
            if (tMax.y > tBest) break;
            cur.y += istep.y; tMax.y += tDelta.y;
        } else {
            if (tMax.z > tBest) break;
            cur.z += istep.z; tMax.z += tDelta.z;
        }
    }

    outNormal = nBest;
    outAlbedo = aBest;
    return tBest;
}

vec3 shade_hit(vec3 normal, vec3 albedo) {
    vec3 L = normalize(-uLightDir);
    float NdotL = max(dot(normal, L), 0.0);
    return albedo / PI * uLightColor * uLightIntensity * NdotL + albedo * 0.15;
}

vec3 trace_scene(vec3 ro, vec3 rd) {
    vec3 car_n, car_alb;
    float t_car = trace_grid(ro, rd, car_n, car_alb);

    float t_ground = 1e30;
    if (abs(rd.y) > 1e-6) {
        float t = (uGroundY - ro.y) / rd.y;
        if (t > 1e-4) t_ground = t;
    }

    if (t_car < t_ground && t_car < 1e29) return shade_hit(car_n, car_alb);
    if (t_ground < 1e29) return shade_hit(vec3(0,1,0), uGroundAlbedo);

    float y = rd.y * 0.5 + 0.5;
    return mix(uSkyHorizon, uSkyZenith, y);
}

vec3 fit_sg_color(vec3 ro, vec3 axis, float sharpness) {
    const int N = 48;
    vec3 accum = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < N; ++i) {
        vec3 d = fib_dir(i, N);
        float k = exp(sharpness * (dot(d, axis) - 1.0));
        vec3 rad = trace_scene(ro, d);
        accum += rad * k;
        weight += k;
    }
    return (weight > 1e-6) ? (accum / weight) : vec3(0.0);
}

void main() {
    int col = int(gl_FragCoord.x);
    int row = int(gl_FragCoord.y);
    int sg  = col >> 1;
    int parity = col & 1;

    int pz = row / (PROBE_COUNT_X * PROBE_COUNT_Y);
    int rem = row % (PROBE_COUNT_X * PROBE_COUNT_Y);
    int py = rem / PROBE_COUNT_X;
    int px = rem % PROBE_COUNT_X;

    vec3 t = vec3(float(px) + 0.5, float(py) + 0.5, float(pz) + 0.5)
           / vec3(float(PROBE_COUNT_X), float(PROBE_COUNT_Y), float(PROBE_COUNT_Z));
    vec3 probe_pos = mix(uGridMin, uGridMax, t);

    vec3 axis = fib_dir(sg, PROBE_SG_COUNT);
    float sharpness = 4.0;

    if (parity == 0) {
        fragColor = vec4(axis, sharpness);
        return;
    }

    vec3 color = fit_sg_color(probe_pos, axis, sharpness);
    fragColor = vec4(color, 1.0);
}
