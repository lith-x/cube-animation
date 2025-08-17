#version 300 es
precision highp float;

layout (location=0) in vec3 vertexPosition;   // template cube edge vertex
layout (location=1) in vec3 instanceCenter;   // per-instance cube center

uniform mat4 mvp;

void main() {
    vec3 worldPos = vertexPosition + instanceCenter;
    gl_Position = mvp * vec4(worldPos, 1.0);
}