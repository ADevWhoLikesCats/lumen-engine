#version 330 core
in vec2 vNDC;

uniform mat4 uInvViewProj;   // inverse(proj * view)
uniform vec3 uHorizonColor;
uniform vec3 uZenithColor;
uniform vec3 uCameraPos;

out vec4 FragColor;

void main() {
    // Reconstruct a world-space ray through this NDC point.
    vec4 pNear = uInvViewProj * vec4(vNDC, -1.0, 1.0);
    vec4 pFar  = uInvViewProj * vec4(vNDC,  1.0, 1.0);
    vec3 dir = normalize((pFar.xyz / pFar.w) - (pNear.xyz / pNear.w));

    float t = max(dir.y, 0.0);
    // Curve: sharper near horizon, flatter near zenith.
    t = pow(t, 0.55);
    vec3 color = mix(uHorizonColor, uZenithColor, t);

    FragColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
