#version 330

noperspective in vec2 fragTexCoord;
noperspective in vec4 fragColor;
in vec3 fragWorldPos;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec3 lightDir;
uniform float ambient;
uniform float colorLevels;
uniform float ditherStrenght;

out vec4 finalColor;

const float BAYER4[16] = float[16](
    0.0, 8.0, 2.0, 10.0,
    12.0, 4.0, 14.0, 6.0,
    3.0, 11.0, 1.0, 9.0,
    15.0, 7.0, 13.0, 5.0
);

void main() {
    vec4 texel = texture(texture0, fragTexCoord) * colDiffuse;
    if (texel.a < 0.5) discard;

    vec3 c = cross(dFdx(fragWorldPos), dFdy(fragWorldPos));
    vec3 n = (dot(c, c) > 1e-20) ? normalize(c) : vec3(0.0, 1.0, 0.0);

    float diff = max(dot(n, -normalize(lightDir)), 0.0);
    float light = ambient + (1.0 - ambient) * diff;

    vec3 rgb = texel.rgb * fragColor.rgb * light;

    float steps = max(colorLevels, 2.0) - 1.0;
    ivec2 p = ivec2(gl_FragCoord.xy) & 3;
    float threshold = (BAYER4[p.y * 4 + p.x] + 0.5) / 16.0 - 0.5;
    rgb = floor(rgb * steps + 0.5 + threshold * ditherStrenght) / steps;

    finalColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}