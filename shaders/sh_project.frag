#version 330 core
uniform sampler2D uOcta;
uniform float     uOctaSize;
out vec4 fragColor;

const float PI = 3.14159265359;

void sh_basis(vec3 d, out float Y[9]) {
    Y[0] = 0.282095;
    Y[1] = 0.488603 * d.y;
    Y[2] = 0.488603 * d.z;
    Y[3] = 0.488603 * d.x;
    Y[4] = 1.092548 * d.x * d.y;
    Y[5] = 1.092548 * d.y * d.z;
    Y[6] = 0.315392 * (3.0 * d.z * d.z - 1.0);
    Y[7] = 1.092548 * d.x * d.z;
    Y[8] = 0.546274 * (d.x * d.x - d.y * d.y);
}

vec3 oct_decode(vec2 uv) {
    uv = uv * 2.0 - 1.0;
    vec3 n = vec3(uv.x, uv.y, 1.0 - abs(uv.x) - abs(uv.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

float texel_solid_angle(vec2 uv01, float N) {
    vec2 uv = uv01 * 2.0 - 1.0;
    float du = 2.0 / N;
    float r2 = dot(uv, uv);
    return (4.0 * du * du) / pow(1.0 + r2, 1.5);
}

void main() {
    int coef = int(gl_FragCoord.x);
    vec3 accum = vec3(0.0);
    float N = uOctaSize;
    int Ni = int(N);

    for (int y = 0; y < Ni; ++y) {
        for (int x = 0; x < Ni; ++x) {
            vec2 uv = (vec2(float(x), float(y)) + 0.5) / N;
            vec3 d = oct_decode(uv);
            vec3 col = texture(uOcta, uv).rgb;
            float dOmega = texel_solid_angle(uv, N);

            float Y[9];
            sh_basis(d, Y);
            accum += col * Y[coef] * dOmega;
        }
    }
    fragColor = vec4(accum, 1.0);
}
