#version 330

in vec2 fragTexCoord;
noperspective in vec4 fragColor;
in float fragDepth;
in float fragWorldY;
in vec2 fragWorldXZ;

// Corte por altura (editor: ver un interior por dentro). Fuera del editor es enorme.
uniform float clipY;

uniform sampler2D texture0;

uniform float colorLevels;
uniform float ditherStrength;

uniform vec3  fogColor;
uniform float fogStart;
uniform float fogEnd;

// Neblina baja: mas densa cerca del suelo (por debajo de mistHeight), en bancos que varian por
// el terreno y nunca pegada a la camara. Se mezcla hacia mistColor. mistDensity 0 = sin neblina.
// Panorama (el horizonte lejano que sigue a la camara): bruma fija en vez de la niebla por
// distancia. < 0 = no es el panorama.
uniform float fogFixed;

uniform float mistDensity;
uniform float mistHeight;
uniform vec3  mistColor;

float MistAmount() {
    if (mistDensity <= 0.0) return 0.0;
    float suelo = clamp(1.0 - fragWorldY / max(mistHeight, 0.01), 0.0, 1.0);
    float lejos = clamp((fragDepth - 1.5) / 7.0, 0.0, 1.0);
    float banco = 0.62 + 0.38 * sin(fragWorldXZ.x * 0.33 + 2.0 * sin(fragWorldXZ.y * 0.21))
                             * sin(fragWorldXZ.y * 0.29 + 1.7 + sin(fragWorldXZ.x * 0.17));
    return clamp(mistDensity * suelo * suelo * lejos * banco, 0.0, 1.0);
}

out vec4 finalColor;

const float BAYER4[16] = float[16](
     0.0,  8.0,  2.0, 10.0,
    12.0,  4.0, 14.0,  6.0,
     3.0, 11.0,  1.0,  9.0,
    15.0,  7.0, 13.0,  5.0);

void main() {
    if (fragWorldY > clipY) discard;
    vec4 texel = texture(texture0, fragTexCoord) * fragColor;
    if (texel.a < 0.5) discard;

    vec3 rgb = texel.rgb;

    float fog = clamp((fragDepth - fogStart) / max(fogEnd - fogStart, 0.0001), 0.0, 1.0);
    float mist = MistAmount();
    if (fogFixed >= 0.0) {
        fog = fogFixed;
        mist = 0.0;
    }
    rgb = mix(mix(rgb, mistColor, mist), fogColor, fog);
    fog = 1.0 - (1.0 - fog) * (1.0 - mist); // el dithering tambien se apaga dentro de la neblina

    float strength  = ditherStrength * (1.0 - fog);
    float steps     = max(colorLevels, 2.0) - 1.0;
    ivec2 p         = ivec2(gl_FragCoord.xy) & 3;
    float threshold = (BAYER4[p.y * 4 + p.x] + 0.5) / 16.0 - 0.5;
    rgb = floor(rgb * steps + 0.5 + threshold * strength) / steps;

    finalColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
