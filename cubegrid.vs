#version 300 es
precision highp float;

in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;

// Instance transform (raylib provides this with DrawMeshInstanced)
in mat4 instanceTransform;

uniform mat4 mvp;

// Bullet uniforms (limit to 32 for safety)
uniform int uBulletCount;
uniform vec3 uBulletPos[32];
uniform vec3 uBulletScale[32];
uniform vec4 uBulletColor[32];

out vec4 vColor;

void main() {
    // Correct way to get cube center (translation column)
    vec3 cubeCenter = instanceTransform[3].xyz;

    float side_len = 0.2;
    vec4 color = vec4(0.0);

    for (int i = 0; i < uBulletCount; i++) {
        vec3 d = abs((cubeCenter - uBulletPos[i]) / uBulletScale[i]);
        float dist = d.x + d.y + d.z;
        float s = max(0.0, 1.0 * (1.0 - dist)); // scale by CUBE_SIZE if needed
        if (s > side_len) {
            side_len = s;
            color = uBulletColor[i];
        }
    }

    vColor = color;

    // Scale cube vertices by side_len
    vec3 scaledPos = vertexPosition * side_len;
    gl_Position = mvp * instanceTransform * vec4(scaledPos, 1.0);
}



// precision highp float;

// in vec3 vertexPosition;
// in vec3 vertexNormal;
// in vec2 vertexTexCoord;

// uniform mat4 mvp;

// out vec4 vColor;

// void main() {
//     vColor = vec4(1.0, 0.0, 0.0, 1.0); // solid red cube
//     gl_Position = mvp * vec4(vertexPosition, 1.0);
// }