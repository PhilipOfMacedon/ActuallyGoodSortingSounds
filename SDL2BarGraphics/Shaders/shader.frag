#version 330

in vec4 vertexColor;
in vec2 TexCoord;

out vec4 FragColor;

uniform sampler2D customTexture;
uniform bool useTexture; // Flag to switch between textured and un-textured

void main() {
    if (useTexture) {
        // Sample the texture and blend with the vertex color
        FragColor = texture(customTexture, TexCoord) * vertexColor;
    } else {
        // Just use the vertex color
        FragColor = vertexColor;
    }
}