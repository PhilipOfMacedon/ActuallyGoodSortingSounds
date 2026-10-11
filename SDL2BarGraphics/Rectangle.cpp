#include "Rectangle.h"

void Rectangle::createRectangle(const GLfloat* color)
{
	GLfloat verts[] =
	{
		0.0f, 0.0f, 0.0f,
		0.0f, 1.0f, 0.0f,
		1.0f, 1.0f, 0.0f,
		1.0f, 0.0f, 0.0f
	};

	GLfloat colors[16];
	for (int i = 0; i < 16; i++)
	{
		//std::copy(color, color + 4, colors + (i*4));
		colors[i] = color[i % 4];
	}

	GLuint indx[] =
	{
		0, 1, 2,
		0, 2, 3
	};

	createMesh(verts, colors, indx, 4, 6);
	assignTexture();
}
