#pragma once

#include <cstdio>

#include "Mesh.h"

class Rectangle : public Mesh
{
public:
	Rectangle(const GLfloat* color) : Mesh() { createRectangle(color); }
	~Rectangle() override = default;

private:
	void createRectangle(const GLfloat* color);
};

