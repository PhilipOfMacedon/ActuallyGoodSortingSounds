#include "AlgorithmConfigurationResolver.h"

#include <set>
#include <stdexcept>

namespace sortinglogic
{
	namespace detail
	{
		namespace
		{
			AlgorithmConfiguration defaultConfiguration(const AlgorithmDescriptor& descriptor)
			{
				AlgorithmConfiguration configuration;
				for (std::size_t i = 0; i < descriptor.supportedOptions.size(); ++i)
					if (descriptor.supportedOptions[i].isDefault)
						configuration.setValue(descriptor.supportedOptions[i].groupId, descriptor.supportedOptions[i].value);
				return configuration;
			}

			void validateExplicitChoiceOptions(const AlgorithmConfiguration& configuration,
				const AlgorithmDescriptor& descriptor)
			{
				std::set<std::string> validatedGroups;
				for (std::size_t i = 0; i < descriptor.supportedOptions.size(); ++i)
				{
					const AlgorithmOptionDescriptor& option = descriptor.supportedOptions[i];
					if (!validatedGroups.insert(option.groupId).second) continue;

					int selectedValue = 0;
					if (!configuration.optionValue(option.groupId, selectedValue))
						throw std::invalid_argument("The requested algorithm property has no registered value.");
					bool registeredValue = false;
					for (std::size_t candidateIndex = 0; candidateIndex < descriptor.supportedOptions.size(); ++candidateIndex)
					{
						const AlgorithmOptionDescriptor& candidate = descriptor.supportedOptions[candidateIndex];
						if (candidate.groupId == option.groupId && candidate.value == selectedValue)
						{
							registeredValue = true;
							break;
						}
					}
					if (!registeredValue)
						throw std::invalid_argument("The algorithm configuration contains an unregistered property choice.");
				}
				for (std::map<std::string, int>::const_iterator value = configuration.values.begin();
					value != configuration.values.end(); ++value)
				{
					bool registeredGroup = false;
					for (std::size_t optionIndex = 0; optionIndex < descriptor.supportedOptions.size(); ++optionIndex)
						if (value->first == descriptor.supportedOptions[optionIndex].groupId)
						{
							registeredGroup = true;
							break;
						}
					if (!registeredGroup)
						throw std::invalid_argument("The algorithm configuration contains an unregistered property ID.");
				}
			}
		}

		AlgorithmConfiguration resolveAlgorithmConfiguration(const TraceBuildRequest& request,
			const AlgorithmDescriptor& descriptor)
		{
			AlgorithmConfiguration configuration = defaultConfiguration(descriptor);
			if (request.hasExplicitConfiguration)
				for (std::map<std::string, int>::const_iterator value = request.configuration.values.begin();
					value != request.configuration.values.end(); ++value)
					configuration.setValue(value->first, value->second);

			for (std::map<std::string, std::string>::const_iterator selected = request.optionSelections.begin();
				selected != request.optionSelections.end(); ++selected)
			{
				const AlgorithmOptionDescriptor* resolved = nullptr;
				for (std::size_t i = 0; i < descriptor.supportedOptions.size(); ++i)
				{
					const AlgorithmOptionDescriptor& option = descriptor.supportedOptions[i];
					if (selected->first == option.groupId && selected->second == option.stableId)
					{
						resolved = &option;
						break;
					}
				}
				if (!resolved)
					throw std::invalid_argument("The requested algorithm option ID is not registered for this algorithm.");
				configuration.setValue(resolved->groupId, resolved->value);
			}

			// Configuration values may enter through the typed API or the stable-ID
			// compatibility adapter above, but every value must resolve to a property
			// registered for this selected strategy.
			validateExplicitChoiceOptions(configuration, descriptor);
			return configuration;
		}
	}

	bool ResolvedAlgorithmPresentation::workspaceVisible(const std::string& stableId) const
	{
		for (std::size_t i = 0; i < workspaces.size(); ++i)
		{
			const AlgorithmWorkspaceDescriptor& workspace = workspaces[i];
			if (workspace.stableId != stableId || workspace.visibilityPropertyId.empty()) continue;
			return properties.value(workspace.visibilityPropertyId, 1) != 0;
		}
		return true;
	}

	ResolvedAlgorithmPresentation resolveAlgorithmPresentation(const AlgorithmDescriptor* descriptor,
		const AlgorithmConfiguration& configuration)
	{
		ResolvedAlgorithmPresentation presentation;
		presentation.properties = configuration;
		if (!descriptor) return presentation;
		presentation.registeredAlgorithm = true;
		presentation.workspaces = descriptor->workspaces;
		presentation.reverseAudioOperandOrder = descriptor->reverseAudioOperandOrder;
		presentation.comparisonCadenceOnly = descriptor->comparisonCadenceOnly;
		presentation.fixedDurationUsesEligibleEvents = descriptor->fixedDurationUsesEligibleEvents;
		presentation.reconstructionSupported = descriptor->reconstructionPresentationSupported;
		presentation.auxiliaryWritesAudible = descriptor->auxiliaryWritesAudible;
		presentation.mainChangeAccentsVisible = descriptor->mainChangeAccentsVisible;
		presentation.probeAccentsSupported = descriptor->probeAccentsSupported;
		presentation.discardAccentsSupported = descriptor->discardAccentsSupported;
		for (std::size_t i = 0; i < descriptor->supportedOptions.size(); ++i)
		{
			const AlgorithmOptionDescriptor& property = descriptor->supportedOptions[i];
			int ignored = 0;
			if (property.isDefault && !presentation.properties.optionValue(property.groupId, ignored))
				presentation.properties.setValue(property.groupId, property.value);
		}
		return presentation;
	}
}
