#version 330

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec4 vertexColor;

uniform mat4 mvp;
uniform mat4 matModel;
uniform vec2 snapResolution;

noperspective out vec2 fragTexCoord;
noperspective out vec4 fragColor;
out vec3 fragWorldPos;

out float fragDepth;

void main() {
    fragTexCoord = vertexTexCoord;
    fragColor    = vertexColor;
    fragWorldPos = vec3(matModel * vec4(vertexPosition, 1.0));

    vec4 pos  = mvp * vec4(vertexPosition, 1.0);
    fragDepth = pos.w;

    if (pos.w > 0.0001) {
        vec2 grid = snapResolution * 0.5;
        vec2 ndc  = pos.xy / pos.w;
        ndc       = floor(ndc * grid + 0.5) / grid;
        pos.xy    = ndc * pos.w;
    }

    gl_Position = pos;
}