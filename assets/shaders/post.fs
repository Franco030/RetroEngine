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
// Ojos cerrados: 0 abiertos .. 1 cerrados (la vista de "la ciudad que duerme"). Al pasar por la
// mitad los parpados tapan todo. 'drowsy': lo cerca que esta el sueno (0..1).
uniform float eyes;
uniform float drowsy;
// Relampago o flash de camara (0..1): la imagen se aclara y se enfria de golpe.
uniform float flash;

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

    if (eyes > 0.0) {
        // Con los ojos cerrados se ve en azul, temblando un poco, como a traves del parpado.
        vec2 onda = vec2(sin(fragTexCoord.y * 19.0 + time * 1.3), cos(fragTexCoord.x * 15.0 + time * 1.1));
        vec3 d = texture(texture0, uv + onda * 0.0025 * eyes).rgb;
        float l = dot(d, vec3(0.299, 0.587, 0.114));
        // la raiz levanta lo oscuro: con los ojos cerrados se ve aunque este a oscuras
        vec3 sueno = vec3(0.05, 0.07, 0.17) + vec3(0.55, 0.64, 1.0) * min(1.0, pow(l, 0.5) * 1.35);
        sueno *= 1.0 - 0.35 * drowsy * (0.6 + 0.4 * sin(time * 2.2));
        rgb = mix(rgb, sueno, smoothstep(0.5, 1.0, eyes));
    }

    if (flash > 0.0) {
        float l = dot(rgb, vec3(0.299, 0.587, 0.114));
        vec3 blanco = vec3(0.86, 0.9, 1.0) * (0.18 + pow(l, 0.6) * 1.6);
        rgb = mix(rgb, blanco, min(1.0, flash * 0.85));
    }

    rgb *= tint;
    float luma = dot(rgb, vec3(0.299, 0.587, 0.114));
    rgb = mix(vec3(luma), rgb, saturation);

    float r = length(fragTexCoord - 0.5);
    rgb *= 1.0 - vignette * smoothstep(0.30, 0.78, r);
    // El sueno cierra la vista por los bordes; los parpados, de arriba y abajo hacia el centro.
    rgb *= 1.0 - min(1.0, drowsy * 1.2) * smoothstep(0.12, 0.62, r);
    float parpado = 1.0 - abs(2.0 * eyes - 1.0);
    float abierto = 0.5 * (1.0 - parpado) + 0.06 * (1.0 - step(eyes, 0.0));
    if (eyes > 0.0 && abs(fragTexCoord.y - 0.5) > abierto) rgb = vec3(0.0);

    float frame = floor(time * 24.0);
    float n = hash12(floor(gl_FragCoord.xy * 0.5) + frame * vec2(17.0, 31.0));
    rgb += (n - 0.5) * grain;

    finalColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
