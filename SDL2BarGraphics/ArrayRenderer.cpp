#include "ArrayRenderer.h"

#include <algorithm>

ArrayRenderer::ArrayRenderer(
	Window& window,
	std::vector<GLfloat>& array,
	std::vector<int>& highlights,
	std::mutex& stateMutex,
	GLfloat projWidth,
	GLfloat projHeight,
	GLfloat heightMult)
	: window(window),
	array(array),
	highlights(highlights),
	stateMutex(stateMutex),
	bar(nullptr),
	highlightedBar(nullptr),
	titleLabel(nullptr),
	shader(new Shader()),
	projection(1.0f),
	projWidth(projWidth),
	projHeight(projHeight),
	heightMult(heightMult)
{
	shader->createFromFiles("Shaders/shader.vert", "Shaders/shader.frag");
	titleLabel.reset(new TextLabel("Sorting Audibilization v1.0alpha"));
	projection = glm::ortho(
		-projWidth / 2.0f,
		projWidth / 2.0f,
		-projHeight / 2.0f,
		projHeight / 2.0f,
		-10.0f,
		10.0f);

	const GLfloat white[] = { 1.0f, 1.0f, 1.0f, 1.0f };
	const GLfloat red[] = { 1.0f, 0.0f, 0.0f, 1.0f };
	bar.reset(new Rectangle(white));
	highlightedBar.reset(new Rectangle(red));
}

void ArrayRenderer::render()
{
	if (!shader || !shader->getShaderID())
		return;

	std::vector<GLfloat> arraySnapshot;
	std::vector<int> highlightsSnapshot;
	{
		std::lock_guard<std::mutex> lock(stateMutex);
		arraySnapshot = array;
		highlightsSnapshot = highlights;
	}

	glUseProgram(shader->getShaderID());
	glUniformMatrix4fv(
		shader->getProjectionLocation(),
		1,
		GL_FALSE,
		glm::value_ptr(projection));

	drawBars(arraySnapshot, highlightsSnapshot);
	drawTitleLabel();

	glUseProgram(0);
}

void ArrayRenderer::drawBars(
	const std::vector<GLfloat>& array,
	const std::vector<int>& highlights)
{
	if (array.empty())
		return;

	const GLfloat posY = -projHeight / 2.0f;
	const GLfloat rectWidth = projWidth / static_cast<GLfloat>(array.size());

	for (std::size_t i = 0; i < array.size(); ++i)
	{
		const GLfloat posX = -projWidth / 2.0f + static_cast<GLfloat>(i) * rectWidth;
		const glm::vec3 pos(posX, posY, -0.5f);
		const glm::vec3 scale(rectWidth, heightMult * array[i], 1.0f);
		Rectangle* selectedRect = isHighlighted(static_cast<int>(i), highlights)
			? highlightedBar.get()
			: bar.get();
		selectedRect->renderMesh(pos, scale, shader.get());
	}
}

void ArrayRenderer::drawTitleLabel()
{
	if (!titleLabel)
		return;

	const glm::vec2 textureDim = titleLabel->getTextureDimensions();
	if (textureDim.x <= 0.0f || textureDim.y <= 0.0f)
		return;

	int windowWidth = 0;
	int windowHeight = 0;
	SDL_GetWindowSize(window.getWindow(), &windowWidth, &windowHeight);
	if (windowWidth <= 0 || windowHeight <= 0)
		return;

	const GLfloat targetHeightPixels =
		static_cast<GLfloat>(windowHeight) * titleLabel->getHeightProportion();
	const GLfloat scaleFactor = targetHeightPixels / textureDim.y;
	const GLfloat finalQuadWidthPixels = textureDim.x * scaleFactor;
	const GLfloat finalQuadHeightPixels = targetHeightPixels;

	const GLfloat rectWidth = finalQuadWidthPixels / static_cast<GLfloat>(windowWidth);
	const GLfloat rectHeight = finalQuadHeightPixels / static_cast<GLfloat>(windowHeight);
	const glm::vec3 pos(-projWidth / 2.0f, projHeight / 2.0f, 0.0f);
	const glm::vec3 scale(rectWidth, rectHeight, 1.0f);

	titleLabel->renderMesh(pos, scale, shader.get());
}

bool ArrayRenderer::isHighlighted(int pos, const std::vector<int>& highlights) const
{
	return std::find(highlights.begin(), highlights.end(), pos) != highlights.end();
}
