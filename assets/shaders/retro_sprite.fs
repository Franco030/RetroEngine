#version 330

noperspective in vec2 fragTexCoord;
noperspective in vec4 fragColor;
in float fragDepth;

uniform sampler2D texture0;

uniform float colorLevels;
uniform float ditherStrength;

uniform vec3  fogColor;
uniform float fogStart;
uniform float fogEnd;

out vec4 finalColor;

const float BAYER4[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0);

void main() {
    // El color de vertice lleva el tinte y el brillo de la escena (lo calcula C++).
    vec4 texel = texture(texture0, fragTexCoord) * fragColor;
    if (texel.a < 0.5) discard;   // recorte duro: sin blending ni ordenar sprites

    vec3 rgb = texel.rgb;

    float fog = clamp((fragDepth - fogStart) / max(fogEnd - fogStart, 0.0001), 0.0, 1.0);
    rgb = mix(rgb, fogColor, fog);

    float strength  = ditherStrength * (1.0 - fog);
    float steps     = max(colorLevels, 2.0) - 1.0;
    ivec2 p         = ivec2(gl_FragCoord.xy) & 3;
    float threshold = (BAYER4[p.y * 4 + p.x] + 0.5) / 16.0 - 0.5;
    rgb = floor(rgb * steps + 0.5 + threshold * strength) / steps;

    finalColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
