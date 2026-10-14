#pragma once

#include <cstdint>
#include <map>

enum class RenderLayer : std::uint32_t
{
	ChartBars = 0,
	ChartGaps = 1,
	HeldItem = 2,
	ChartLabels = 3,
	MainControls = 100,
	MainText = 110,
	Dropdown = 150,
	DropdownText = 151,
	ModalDim = 190,
	SettingsPanel = 200,
	SettingsAccent = 201,
	SettingsControls = 202,
	SettingsText = 210,
	SettingsDropdown = 230,
	SettingsDropdownText = 231
};

inline RenderLayer layerOffset(RenderLayer layer, std::uint32_t offset)
{
	return static_cast<RenderLayer>(static_cast<std::uint32_t>(layer) + offset);
}

inline bool painterLayerBefore(RenderLayer left, RenderLayer right)
{
	return static_cast<std::uint32_t>(left) < static_cast<std::uint32_t>(right);
}

enum class MaterialId : std::uint32_t
{
	Bar = 1,
	ComparedBar = 2,
	HeldBar = 3,
	ShiftedBar = 4,
	WriteTarget = 5,
	GapMarker = 6,
	Panel = 20,
	Button = 21,
	ValueButton = 22,
	Timeline = 23,
	Accent = 24,
	Popup = 25,
	ModalDim = 26,
	TimelineThumb = 27,
	PixelIcon = 28,
	Gear = 29,
	GearCutout = 30,
	Text = 1000
};

enum class BlendMode : std::uint8_t
{
	Opaque,
	Alpha
};

enum class TextureBinding : std::uint8_t
{
	None,
	CommandTexture
};

struct MaterialState
{
	BlendMode blendMode;
	TextureBinding textureBinding;

	MaterialState(BlendMode blend = BlendMode::Opaque, TextureBinding texture = TextureBinding::None)
		: blendMode(blend), textureBinding(texture) {}
};

// Renderer-owned immutable defaults with a small extension point for app
// materials. Per-command texture handles remain owned by the submitting asset.
class MaterialTable
{
public:
	MaterialTable()
	{
		registerMaterial(MaterialId::Bar, MaterialState());
		registerMaterial(MaterialId::ComparedBar, MaterialState());
		registerMaterial(MaterialId::HeldBar, MaterialState());
		registerMaterial(MaterialId::ShiftedBar, MaterialState());
		registerMaterial(MaterialId::WriteTarget, MaterialState());
		registerMaterial(MaterialId::GapMarker, MaterialState());
		registerMaterial(MaterialId::Panel, MaterialState());
		registerMaterial(MaterialId::Button, MaterialState());
		registerMaterial(MaterialId::ValueButton, MaterialState());
		registerMaterial(MaterialId::Timeline, MaterialState());
		registerMaterial(MaterialId::Accent, MaterialState());
		registerMaterial(MaterialId::Popup, MaterialState());
		registerMaterial(MaterialId::TimelineThumb, MaterialState());
		registerMaterial(MaterialId::PixelIcon, MaterialState());
		registerMaterial(MaterialId::Gear, MaterialState());
		registerMaterial(MaterialId::GearCutout, MaterialState());
		registerMaterial(MaterialId::Text, MaterialState(BlendMode::Alpha, TextureBinding::CommandTexture));
		registerMaterial(MaterialId::ModalDim, MaterialState(BlendMode::Alpha, TextureBinding::None));
	}

	void registerMaterial(MaterialId id, const MaterialState& state)
	{
		materials_[id] = state;
	}

	MaterialState stateFor(MaterialId id) const
	{
		const std::map<MaterialId, MaterialState>::const_iterator found = materials_.find(id);
		return found == materials_.end() ? MaterialState() : found->second;
	}

private:
	std::map<MaterialId, MaterialState> materials_;
};

struct RenderBatchKey
{
	RenderLayer layer;
	MaterialId material;
	std::uint32_t texture;
	bool clipped;
	float clipX;
	float clipY;
	float clipWidth;
	float clipHeight;

	RenderBatchKey(RenderLayer layerValue, MaterialId materialValue, std::uint32_t textureValue,
		bool isClipped = false, float x = 0.0f, float y = 0.0f, float width = 0.0f, float height = 0.0f)
		: layer(layerValue), material(materialValue), texture(textureValue), clipped(isClipped),
			clipX(x), clipY(y), clipWidth(width), clipHeight(height) {}
};

inline bool compatibleBatch(const RenderBatchKey& left, const RenderBatchKey& right)
{
	return left.layer == right.layer && left.material == right.material && left.texture == right.texture &&
		left.clipped == right.clipped && (!left.clipped ||
		(left.clipX == right.clipX && left.clipY == right.clipY &&
		 left.clipWidth == right.clipWidth && left.clipHeight == right.clipHeight));
}
