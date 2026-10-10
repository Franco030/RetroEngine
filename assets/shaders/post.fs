#version 330

in vec2 fragTexCoord;
in vec4 fragColor;

uniform sampler2D texture0;
uniform vec4 colDiffuse;

uniform vec2  texSize;
uniform vec2  scale;  
uniform int   filterMode;
uniform float vignette;  
uniform float grain;     
uniform float saturation;
uniform vec3  tint;      
uniform float time;

out vec4 finalColor;

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 texel = fragTexCoord * texSize;
    vec2 uv;

    if (filterMode == 0) {
        uv = (floor(texel) + 0.5) / texSize;
    } else if (filterMode == 1) {
        vec2 sc     = max(scale, vec2(1.0));
        vec2 base   = floor(texel);
        vec2 d      = (texel - base) - 0.5;
        vec2 region = 0.5 - 0.5 / sc;
        vec2 f      = (d - clamp(d, -region, region)) * sc + 0.5;
        uv = (base + f) / texSize;
    } else {
        uv = fragTexCoord;
    }

    vec3 rgb = texture(texture0, uv).rgb;

    rgb *= tint;
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(vec3(luma), rgb, saturation);

    float r = length(fragTexCoord - 0.5);
    rgb *= 1.0 - vignette * smoothstep(0.30, 0.78, r);

    float frame = floor(time * 24.0);
    float n = hash12(floor(gl_FragCoord.xy * 0.5) + frame * vec2(17.0, 31.0));
    rgb += (n - 0.5) * grain;

    finalColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
