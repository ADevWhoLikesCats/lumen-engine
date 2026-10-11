#version 330 core
uniform sampler2D uSceneColor;
uniform sampler2D uFogTexture;
uniform vec2      uInvResolution;
out vec4 fragColor;

void main() {
    vec2 uv = gl_FragCoord.xy * uInvResolution;
    vec3 scene = texture(uSceneColor, uv).rgb;
    vec4 fog = texture(uFogTexture, uv);
    vec3 result = scene * fog.a + fog.rgb;
    fragColor = vec4(result, 1.0);
}
