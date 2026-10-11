#version 330

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;     // Input for vertex color
layout (location = 2) in vec2 aTexCoord;  // Input for texture coords

uniform mat4 model;
uniform mat4 projection;

out vec4 vertexColor;
out vec2 TexCoord;

void main() {
    gl_Position = projection * model * vec4(aPos, 1.0);
    vertexColor = aColor;
    TexCoord = aTexCoord;
}