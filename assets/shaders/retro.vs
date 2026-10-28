#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec2 snapResolution;
// Desplazamiento de profundidad (en NDC) para superficies apoyadas en otras (un sendero sobre el
// suelo, una alfombra): las acerca a la camara solo en el test de profundidad, sin moverlas en
// pantalla, asi nunca parpadean con lo de debajo. 0 en todo lo demas.
uniform float depthBias;

// Iluminacion por vertice (Gouraud). Solo el programa de modelos la activa:
// las lineas y los sprites comparten este vertex shader con useLighting = 0.
uniform int   useLighting;
uniform vec3  lightDir;        // luz direccional: hacia donde viaja
uniform float ambient;
uniform float sun;             // fuerza de la luz direccional (0 = bajo techo)
uniform int   lightCount;
uniform vec4  lightPos[8];     // xyz = posicion de mundo, w = radio
uniform vec3  lightColor[8];

// UV con perspectiva correcta: la textura no se deforma de cerca.
out vec2 fragTexCoord;
noperspective out vec4 fragColor;
noperspective out vec3 fragLight;            // luz del vertice, interpolada en pantalla

// Profundidad de vista = w de clip space. Se mide ANTES del snapping.
out float fragDepth;
// Altura de mundo (para el corte de techos del editor: clipY en los fragment shaders).
out float fragWorldY;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragWorldY   = (matModel * vec4(vertexPosition, 1.0)).y;

    vec3 light = vec3(1.0);
    if (useLighting != 0) {
        vec3 worldPos = vec3(matModel * vec4(vertexPosition, 1.0));

        // Normal de mundo: correcta con escala no uniforme (transpuesta de la inversa).
        vec3  nw  = transpose(inverse(mat3(matModel))) * vertexNormal;
        float len = length(nw);
        vec3  n   = len > 1e-5 ? nw / len : vec3(0.0, 1.0, 0.0);

        float diff = max(dot(n, -normalize(lightDir)), 0.0);
        light = vec3(ambient + (1.0 - ambient) * sun * diff);

        for (int i = 0; i < min(lightCount, 8); ++i) {
            vec3  toLight = lightPos[i].xyz - worldPos;
            float dist    = length(toLight);
            float att     = clamp(1.0 - dist / lightPos[i].w, 0.0, 1.0);
            float ndl     = max(dot(n, toLight / max(dist, 0.0001)), 0.0);
            light += lightColor[i] * (att * att * ndl);
        }
    }
    fragLight = light;

    vec4 pos  = mvp * vec4(vertexPosition, 1.0);
    fragDepth = pos.w;

    if (pos.w > 0.0001) {
        vec2 grid = snapResolution * 0.5;
        vec2 ndc  = pos.xy / pos.w;
        ndc       = floor(ndc * grid + 0.5) / grid;
        pos.xy    = ndc * pos.w;
    }
    pos.z -= depthBias * pos.w;

    gl_Position = pos;
}
