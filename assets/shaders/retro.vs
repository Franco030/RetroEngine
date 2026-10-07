#version 330

// Nombres exigidos por Raylib
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;

// Cuadricula de snapping
uniform vec2 snapResolution;

// noperspective = interpolacion linea en pantalla (sin dividir por w)
// Esto produce la deformacion afin de texturas
noperspective out vec2 fragTexCoord;
noperspective out vec4 fragColor;

// Posicion de mundo con interpolacion normal (perspectiva correcta)
// el fragment shader la deriva pata obtener la normal plana de cada cara
out vec3 fragWorldPos;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragWorldPos = vec3(matModel * vec4(vertexPosition, 1.0));

    vec4 pos = mvp * vec4(vertexPosition, 1.0);

    if (pos.w > 0.0001) {
        vec2 grid = snapResolution * 0.5;
        vec2 ndc = pos.xy / pos.w;
        ndc = floor(ndc * grid + 0.5) / grid;
        pos.xy = ndc * pos.w;
    }

    gl_Position = pos;
}