#include "ArrayRenderer.h"

#include <algorithm>
#include <map>

#include "LayoutMetrics.h"

namespace
{
	const std::size_t FirstChartMaterial = static_cast<std::size_t>(MaterialId::Bar);
	std::size_t materialBucket(MaterialId material)
	{
		return static_cast<std::size_t>(material) - FirstChartMaterial;
	}
}

ArrayRenderer::ArrayRenderer(Window& window, Renderer2D& renderer)
	: window_(window), renderer_(renderer), materialBuckets_(), gapMarkers_(), laneLabels_(), laneLabelPositions_()
{
}

bool ArrayRenderer::isReady() const
{
	return renderer_.isReady();
}

void ArrayRenderer::render(
	const sortinglogic::RenderSnapshot& snapshot,
	const UserSettings& settings,
	float chartTop)
{
	if (!isReady() || snapshot.state.slots.empty())
		return;
	const int width = renderer_.logicalWidth();
	const int height = renderer_.logicalHeight();
	if (width <= 0 || height <= 0)
		return;

	const ThemePalette& palette = paletteFor(settings.visual.palette);
	const float left = 16.0f;
	const float right = static_cast<float>(width) - 16.0f;
	const float top = std::min(static_cast<float>(height - 12), std::max(40.0f, chartTop));
	const float bottom = static_cast<float>(height) - 18.0f;
	const float areaHeight = std::max(10.0f, bottom - top);
	const std::size_t laneCount = snapshot.state.auxiliaryItems.size() + snapshot.state.integerWorkspaces.size() +
		(snapshot.state.discardedItems.empty() ? 0 : 1);
	const float auxiliaryHeight = laneCount == 0 ? 0.0f : std::min(areaHeight * 0.30f, static_cast<float>(laneCount) * 38.0f);
	const float mainBottom = bottom - auxiliaryHeight - (laneCount ? 6.0f : 0.0f);
	const float mainHeight = std::max(8.0f, mainBottom - top);
	const float columnWidth = std::max(0.0f, right - left) / static_cast<float>(snapshot.state.slots.size());
	const BarSlotMetrics spacing = calculateBarSlotMetrics(columnWidth, settings.visual.spacing);
	for (std::size_t i = 0; i < materialBuckets_.size(); ++i)
		materialBuckets_[i].clear();
	gapMarkers_.clear();
	laneLabelPositions_.clear();

	for (std::size_t index = 0; index < snapshot.state.slots.size(); ++index)
	{
		const float x = left + static_cast<float>(index) * columnWidth + spacing.sidePadding;
		if (!snapshot.state.slots[index].occupied)
		{
			gapMarkers_.push_back(QuadInstance(
				glm::vec2(x, mainBottom - 4.0f), glm::vec2(spacing.barWidth, 4.0f),
				RenderDescriptor(palette.gap, 0, MaterialId::GapMarker), RenderLayer::ChartGaps));
			continue;
		}
		const sortinglogic::SortItem& item = snapshot.state.slots[index].item;
		const float barHeight = std::max(1.0f, item.value * mainHeight);
		const RenderDescriptor descriptor = descriptorForItem(item.stableId, snapshot.activeRoles, palette, "main");
		materialBuckets_[materialBucket(descriptor.materialId)].push_back(QuadInstance(
			glm::vec2(x, mainBottom - barHeight), glm::vec2(spacing.barWidth, barHeight), descriptor, RenderLayer::ChartBars));
	}

	// Auxiliary item lanes and integer counter lanes share the chart's remaining
	// vertical space. Repeated stable IDs are intentional copies, not duplicate
	// primary items; lane-qualified roles keep their highlighting unambiguous.
	std::map<std::string, std::pair<std::size_t, std::size_t> > laneGeometry;
	if (laneCount != 0)
	{
		const float laneGap = 4.0f;
		const float laneHeight = std::max(8.0f, (auxiliaryHeight - laneGap * static_cast<float>(laneCount - 1)) / static_cast<float>(laneCount));
		std::size_t laneNumber = 0;
		for (std::map<std::string, std::vector<sortinglogic::SortSlot> >::const_iterator lane = snapshot.state.auxiliaryItems.begin(); lane != snapshot.state.auxiliaryItems.end(); ++lane, ++laneNumber)
		{
			const float laneTop = mainBottom + 6.0f + static_cast<float>(laneNumber) * (laneHeight + laneGap);
			const float laneBottom = laneTop + laneHeight;
			laneGeometry[lane->first] = std::make_pair(lane->second.size(), laneNumber);
			submitLaneLabel(lane->first, right - 112.0f, laneTop + 1.0f, palette.text);
			const float cellWidth = lane->second.empty() ? 0.0f : (right - left) / static_cast<float>(lane->second.size());
			const BarSlotMetrics laneSpacing = calculateBarSlotMetrics(cellWidth, settings.visual.spacing);
			for (std::size_t i = 0; i < lane->second.size(); ++i)
			{
				if (!lane->second[i].occupied) continue;
				const sortinglogic::SortItem& item = lane->second[i].item;
				const float barHeight = std::max(1.0f, item.value * std::max(4.0f, laneHeight - 2.0f));
				const RenderDescriptor descriptor = descriptorForItem(item.stableId, snapshot.activeRoles, palette, lane->first);
				materialBuckets_[materialBucket(descriptor.materialId)].push_back(QuadInstance(
					glm::vec2(left + static_cast<float>(i) * cellWidth + laneSpacing.sidePadding, laneBottom - barHeight),
					glm::vec2(laneSpacing.barWidth, barHeight), descriptor, RenderLayer::ChartBars));
			}
		}
		for (std::map<std::string, std::vector<int> >::const_iterator lane = snapshot.state.integerWorkspaces.begin(); lane != snapshot.state.integerWorkspaces.end(); ++lane, ++laneNumber)
		{
			const float laneTop = mainBottom + 6.0f + static_cast<float>(laneNumber) * (laneHeight + laneGap);
			const float laneBottom = laneTop + laneHeight;
			laneGeometry[lane->first] = std::make_pair(lane->second.size(), laneNumber);
			submitLaneLabel(lane->first, right - 112.0f, laneTop + 1.0f, palette.text);
			int maximum = 1;
			for (std::size_t i = 0; i < lane->second.size(); ++i) maximum = std::max(maximum, lane->second[i]);
			const float cellWidth = lane->second.empty() ? 0.0f : (right - left) / static_cast<float>(lane->second.size());
			for (std::size_t i = 0; i < lane->second.size(); ++i)
			{
				const float normalized = static_cast<float>(lane->second[i]) / static_cast<float>(maximum);
				const float barHeight = std::max(1.0f, normalized * std::max(4.0f, laneHeight - 2.0f));
				materialBuckets_[materialBucket(MaterialId::Bar)].push_back(QuadInstance(
					glm::vec2(left + static_cast<float>(i) * cellWidth + 1.0f, laneBottom - barHeight),
					glm::vec2(std::max(1.0f, cellWidth - 2.0f), barHeight),
					RenderDescriptor(palette.bar, 0, MaterialId::Bar), RenderLayer::ChartBars));
			}
		}
		if (!snapshot.state.discardedItems.empty())
		{
			const float laneTop = mainBottom + 6.0f + static_cast<float>(laneNumber) * (laneHeight + laneGap);
			const float laneBottom = laneTop + laneHeight;
			laneGeometry["discard"] = std::make_pair(snapshot.state.discardedItems.size(), laneNumber);
			submitLaneLabel("discard", right - 112.0f, laneTop + 1.0f, palette.text);
			const float cellWidth = (right - left) / static_cast<float>(snapshot.state.discardedItems.size());
			const BarSlotMetrics laneSpacing = calculateBarSlotMetrics(cellWidth, settings.visual.spacing);
			for (std::size_t i = 0; i < snapshot.state.discardedItems.size(); ++i)
			{
				const sortinglogic::SortItem& item = snapshot.state.discardedItems[i];
				const float barHeight = std::max(1.0f, item.value * std::max(4.0f, laneHeight - 2.0f));
				const RenderDescriptor descriptor = descriptorForItem(item.stableId, snapshot.activeRoles, palette, "discard");
				materialBuckets_[materialBucket(descriptor.materialId)].push_back(QuadInstance(
					glm::vec2(left + static_cast<float>(i) * cellWidth + laneSpacing.sidePadding, laneBottom - barHeight),
					glm::vec2(laneSpacing.barWidth, barHeight), descriptor, RenderLayer::ChartBars));
			}
		}
	}

	// Bars do not overlap each other. Grouping this non-overlapping chart geometry by material
	// keeps the per-frame GL work bounded by semantic styles rather than item count.
	for (std::size_t group = 0; group < materialBuckets_.size(); ++group)
		for (std::size_t i = 0; i < materialBuckets_[group].size(); ++i)
			renderer_.submit(materialBuckets_[group][i]);
	for (std::map<std::string, std::unique_ptr<TextLabel> >::const_iterator label = laneLabels_.begin(); label != laneLabels_.end(); ++label)
	{
		if (!label->second || !label->second->getTextureId() || label->second->getTextureDimensions().y <= 0.0f) continue;
		const float labelHeight = 13.0f;
		const float labelWidth = std::min(112.0f, labelHeight * label->second->getTextureDimensions().x / label->second->getTextureDimensions().y);
		const std::map<std::string, glm::vec2>::const_iterator position = laneLabelPositions_.find(label->first);
		if (position == laneLabelPositions_.end()) continue;
		renderer_.submit(QuadInstance(glm::vec2(position->second.x + 110.0f - labelWidth, position->second.y), glm::vec2(labelWidth, labelHeight),
			RenderDescriptor(palette.text, label->second->getTextureId(), MaterialId::Text), RenderLayer::ChartLabels));
	}

	if (snapshot.state.hasHeldItem && snapshot.state.gapIndex < snapshot.state.slots.size())
	{
		const float x = left + static_cast<float>(snapshot.state.gapIndex) * columnWidth + spacing.sidePadding;
		const float barHeight = std::max(1.0f, snapshot.state.heldItem.value * mainHeight);
		const float lift = std::min(18.0f, mainHeight * 0.12f);
		renderer_.submit(QuadInstance(
			glm::vec2(x, mainBottom - barHeight - lift), glm::vec2(spacing.barWidth, barHeight),
			descriptorForItem(snapshot.state.heldItem.stableId, snapshot.activeRoles, palette, "main"), RenderLayer::HeldItem));
	}

	for (std::size_t regionIndex = 0; regionIndex < snapshot.activeRegions.size(); ++regionIndex)
	{
		const sortinglogic::RegionAnnotation& region = snapshot.activeRegions[regionIndex];
		std::size_t laneSize = snapshot.state.slots.size();
		float laneTop = top;
		float laneBottom = mainBottom;
		if (region.lane != "main")
		{
			std::map<std::string, std::pair<std::size_t, std::size_t> >::const_iterator geometry = laneGeometry.find(region.lane);
			if (geometry == laneGeometry.end()) continue;
			laneSize = geometry->second.first;
			const std::size_t laneNumber = geometry->second.second;
			const float laneHeight = std::max(8.0f, (auxiliaryHeight - 4.0f * static_cast<float>(laneCount - 1)) / static_cast<float>(laneCount));
			laneTop = mainBottom + 6.0f + static_cast<float>(laneNumber) * (laneHeight + 4.0f);
			laneBottom = laneTop + laneHeight;
		}
		if (laneSize == 0) continue;
		const float column = (right - left) / static_cast<float>(laneSize);
		if (region.kind == sortinglogic::RegionKind::Strided)
		{
			for (std::size_t i = region.first; i < region.last && i < laneSize; i += std::max<std::size_t>(1, region.stride))
				gapMarkers_.push_back(QuadInstance(glm::vec2(left + static_cast<float>(i) * column, laneTop),
					glm::vec2(std::max(1.0f, column * 0.16f), std::max(2.0f, laneBottom - laneTop)),
					RenderDescriptor(palette.accent, 0, MaterialId::GapMarker), RenderLayer::ChartGaps));
		}
		else
		{
			const std::size_t first = region.kind == sortinglogic::RegionKind::Bucket && region.group >= 0
				? static_cast<std::size_t>(region.group) : region.first;
			const std::size_t last = region.kind == sortinglogic::RegionKind::Bucket && region.group >= 0
				? first + 1 : region.last;
			const float x = left + static_cast<float>(std::min(first, laneSize)) * column;
			const float width = std::max(1.0f, static_cast<float>(std::min(last, laneSize) - std::min(first, laneSize)) * column);
			gapMarkers_.push_back(QuadInstance(glm::vec2(x, laneTop), glm::vec2(width, 2.0f),
				RenderDescriptor(palette.accent, 0, MaterialId::GapMarker), RenderLayer::ChartGaps));
		}
	}
	for (std::size_t i = 0; i < gapMarkers_.size(); ++i)
		renderer_.submit(gapMarkers_[i]);
}

void ArrayRenderer::submitLaneLabel(const std::string& lane, float x, float y, const glm::vec4& color)
{
	std::string caption = lane;
	if (lane == "merge") caption = "Merge buffer";
	else if (lane == "count_output") caption = "Stable output";
	else if (lane == "radix_output") caption = "Radix output";
	else if (lane == "counts") caption = "Key counts";
	else if (lane == "digits") caption = "Digit buckets";
	else if (lane == "discard") caption = "Discarded items";
	SDL_Color text = {
		static_cast<Uint8>(std::max(0.0f, std::min(255.0f, color.r * 255.0f))),
		static_cast<Uint8>(std::max(0.0f, std::min(255.0f, color.g * 255.0f))),
		static_cast<Uint8>(std::max(0.0f, std::min(255.0f, color.b * 255.0f))),
		static_cast<Uint8>(std::max(0.0f, std::min(255.0f, color.a * 255.0f))) };
	std::map<std::string, std::unique_ptr<TextLabel> >::iterator found = laneLabels_.find(lane);
	if (found == laneLabels_.end())
		found = laneLabels_.insert(std::make_pair(lane, std::unique_ptr<TextLabel>(new TextLabel(caption, "Courier New Bold", 11, text)))).first;
	else found->second->setText(caption, "Courier New Bold", 11, text);
	laneLabelPositions_[lane] = glm::vec2(x, y);
}

RenderDescriptor ArrayRenderer::descriptorForRole(
	sortinglogic::SemanticRole role,
	const ThemePalette& palette) const
{
	switch (role)
	{
	case sortinglogic::SemanticRole::ComparedItem:
		return RenderDescriptor(palette.compared, 0, MaterialId::ComparedBar);
	case sortinglogic::SemanticRole::HeldKey:
		return RenderDescriptor(palette.held, 0, MaterialId::HeldBar);
	case sortinglogic::SemanticRole::ShiftedItem:
		return RenderDescriptor(palette.shifted, 0, MaterialId::ShiftedBar);
	case sortinglogic::SemanticRole::WriteTarget:
		return RenderDescriptor(palette.writeTarget, 0, MaterialId::WriteTarget);
	case sortinglogic::SemanticRole::Gap:
		return RenderDescriptor(palette.gap, 0, MaterialId::GapMarker);
	case sortinglogic::SemanticRole::Pivot:
		return RenderDescriptor(palette.held, 0, MaterialId::HeldBar);
	case sortinglogic::SemanticRole::CurrentMinimum:
	case sortinglogic::SemanticRole::CurrentMaximum:
		return RenderDescriptor(palette.compared, 0, MaterialId::ComparedBar);
	case sortinglogic::SemanticRole::AuxiliaryCopy:
	case sortinglogic::SemanticRole::ProcessedItem:
		return RenderDescriptor(palette.shifted, 0, MaterialId::ShiftedBar);
	case sortinglogic::SemanticRole::ProbedItem:
		return RenderDescriptor(palette.compared, 0, MaterialId::ComparedBar);
	case sortinglogic::SemanticRole::DiscardedItem:
		return RenderDescriptor(palette.accent, 0, MaterialId::WriteTarget);
	}
	return RenderDescriptor(palette.bar, 0, MaterialId::Bar);
}

RenderDescriptor ArrayRenderer::descriptorForItem(
	std::uint64_t itemId,
	const std::vector<sortinglogic::ActiveRole>& roles,
	const ThemePalette& palette,
	const std::string& lane) const
{
	int priority = 0;
	sortinglogic::SemanticRole selectedRole = sortinglogic::SemanticRole::ComparedItem;
	for (std::size_t i = 0; i < roles.size(); ++i)
	{
		if (roles[i].itemId != itemId || roles[i].lane != lane)
			continue;
		int candidatePriority = 0;
		switch (roles[i].role)
		{
		case sortinglogic::SemanticRole::ComparedItem: candidatePriority = 1; break;
		case sortinglogic::SemanticRole::ShiftedItem: candidatePriority = 2; break;
		case sortinglogic::SemanticRole::WriteTarget: candidatePriority = 3; break;
		case sortinglogic::SemanticRole::HeldKey: candidatePriority = 4; break;
		case sortinglogic::SemanticRole::Gap: candidatePriority = 0; break;
		case sortinglogic::SemanticRole::Pivot: candidatePriority = 5; break;
		case sortinglogic::SemanticRole::CurrentMinimum:
		case sortinglogic::SemanticRole::CurrentMaximum: candidatePriority = 3; break;
		case sortinglogic::SemanticRole::AuxiliaryCopy:
		case sortinglogic::SemanticRole::ProcessedItem:
		case sortinglogic::SemanticRole::ProbedItem: candidatePriority = 2; break;
		case sortinglogic::SemanticRole::DiscardedItem: candidatePriority = 5; break;
		}
		if (candidatePriority > priority)
		{
			priority = candidatePriority;
			selectedRole = roles[i].role;
		}
	}
	return priority == 0
		? RenderDescriptor(palette.bar, 0, MaterialId::Bar)
		: descriptorForRole(selectedRole, palette);
}
