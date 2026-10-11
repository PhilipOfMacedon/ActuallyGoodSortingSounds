#include "Shader.h"

#include <fstream>
#include <iostream>
#include <iterator>

Shader::Shader()
	: shaderID(0),
	uniformProjection(-1),
	uniformModel(-1),
	uniformTexture(-1),
	uniformUseTextureFlag(-1)
{
}

Shader::~Shader()
{
	clearShader();
}

void Shader::createFromString(const char* vertexCode, const char* fragmentCode)
{
	clearShader();
	compileProgram(vertexCode, fragmentCode);
}

void Shader::createFromFiles(const char* vertexCodeFile, const char* fragmentCodeFile)
{
	clearShader();
	const std::string vertexCode = readFile(vertexCodeFile);
	const std::string fragmentCode = readFile(fragmentCodeFile);
	if (vertexCode.empty() || fragmentCode.empty())
		return;

	compileProgram(vertexCode.c_str(), fragmentCode.c_str());
}

void Shader::clearShader()
{
	if (shaderID)
	{
		glDeleteProgram(shaderID);
		shaderID = 0;
	}

	uniformModel = -1;
	uniformProjection = -1;
	uniformTexture = -1;
	uniformUseTextureFlag = -1;
}

void Shader::compileProgram(const char* vertexCode, const char* fragmentCode)
{
	if (!vertexCode || !fragmentCode)
		return;

	const GLuint program = glCreateProgram();
	if (!program)
	{
		std::cerr << "Could not create OpenGL shader program.\n";
		return;
	}

	const GLuint vertexShader = compileShader(vertexCode, GL_VERTEX_SHADER);
	if (!vertexShader)
	{
		glDeleteProgram(program);
		return;
	}

	const GLuint fragmentShader = compileShader(fragmentCode, GL_FRAGMENT_SHADER);
	if (!fragmentShader)
	{
		glDeleteShader(vertexShader);
		glDeleteProgram(program);
		return;
	}

	glAttachShader(program, vertexShader);
	glAttachShader(program, fragmentShader);
	glBindFragDataLocation(program, 0, "FragColor");
	glLinkProgram(program);

	GLint linkStatus = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
	if (linkStatus != GL_TRUE)
	{
		GLchar log[1024] = {};
		glGetProgramInfoLog(program, sizeof(log), nullptr, log);
		std::cerr << "Could not link OpenGL shader program: " << log << '\n';
	}

	glDetachShader(program, vertexShader);
	glDetachShader(program, fragmentShader);
	glDeleteShader(vertexShader);
	glDeleteShader(fragmentShader);

	if (linkStatus != GL_TRUE)
	{
		glDeleteProgram(program);
		return;
	}

	shaderID = program;
	uniformProjection = glGetUniformLocation(shaderID, "projection");
	uniformModel = glGetUniformLocation(shaderID, "model");
	uniformTexture = glGetUniformLocation(shaderID, "customTexture");
	uniformUseTextureFlag = glGetUniformLocation(shaderID, "useTexture");
}

GLuint Shader::compileShader(const char* shaderCode, GLenum shaderType)
{
	const GLuint shader = glCreateShader(shaderType);
	if (!shader)
		return 0;

	const GLchar* source = shaderCode;
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint compileStatus = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compileStatus);
	if (compileStatus != GL_TRUE)
	{
		GLchar log[1024] = {};
		glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
		std::cerr << "Could not compile OpenGL shader: " << log << '\n';
		glDeleteShader(shader);
		return 0;
	}

	return shader;
}

std::string Shader::readFile(const char* file)
{
	if (!file)
		return std::string();

	std::ifstream fileStream(file, std::ios::in | std::ios::binary);
	if (!fileStream)
	{
		std::cerr << "Could not open shader file '" << file << "'.\n";
		return std::string();
	}

	return std::string(
		std::istreambuf_iterator<char>(fileStream),
		std::istreambuf_iterator<char>());
}
