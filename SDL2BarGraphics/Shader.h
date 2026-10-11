#pragma once

#include <string>

#include <GL/glew.h>

class Shader
{
public:
	Shader();
	~Shader();

	Shader(const Shader&) = delete;
	Shader& operator=(const Shader&) = delete;

	void createFromString(const char* vertexCode, const char* fragmentCode);
	void createFromFiles(const char* vertexCodeFile, const char* fragmentCodeFile);
	GLint getProjectionLocation() const { return uniformProjection; }
	GLint getModelLocation() const { return uniformModel; }
	GLint getTextureLocation() const { return uniformTexture; }
	GLint getUseTextureFlagLocation() const { return uniformUseTextureFlag; }
	GLuint getShaderID() const { return shaderID; }
	void clearShader();

private:
	GLuint shaderID;
	GLint uniformProjection;
	GLint uniformModel;
	GLint uniformTexture;
	GLint uniformUseTextureFlag;

	void compileProgram(const char* vertexCode, const char* fragmentCode);
	GLuint compileShader(const char* shaderCode, GLenum shaderType);
	std::string readFile(const char* file);
};
