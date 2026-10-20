#include "AlgorithmConfigurationResolver.h"

#include <set>
#include <stdexcept>

namespace sortinglogic
{
	namespace detail
	{
		namespace
		{
			AlgorithmConfiguration importLegacyConfiguration(const TraceBuildRequest& request,
				const AlgorithmDescriptor& descriptor)
			{
				AlgorithmConfiguration configuration;
				configuration.shellGapSequence = request.shellGapSequence;
				configuration.mergeWritebackMode = request.mergeWritebackMode;
				configuration.mergeExecutionMode = request.mergeExecutionMode;
				configuration.radixBase = request.radixBase;
				configuration.bucketCount = request.bucketCount;
				configuration.timeMagnitude = request.timeMagnitude;
				configuration.rotateMergeBufferSize = request.rotateMergeBufferSize;
				if (descriptor.id == SortAlgorithm::ArrayvBaseNHeap && request.bucketCount >= 2)
					configuration.heapArity = request.bucketCount;
				if (descriptor.id == SortAlgorithm::ArrayvGrail)
				{
					if (request.bucketCount == 1) configuration.grailBufferMode = GrailBufferMode::StaticBuffer;
					else if (request.bucketCount == 2) configuration.grailBufferMode = GrailBufferMode::DynamicBuffer;
				}
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
					if (!configuration.optionValue(option.target, selectedValue))
						throw std::invalid_argument("The requested algorithm option group has no trace configuration binding.");
					bool registeredValue = false;
					for (std::size_t candidateIndex = 0; candidateIndex < descriptor.supportedOptions.size(); ++candidateIndex)
					{
						const AlgorithmOptionDescriptor& candidate = descriptor.supportedOptions[candidateIndex];
						if (candidate.groupId == option.groupId && candidate.target == option.target &&
							candidate.value == selectedValue)
						{
							registeredValue = true;
							break;
						}
					}
					if (!registeredValue)
						throw std::invalid_argument("The explicit algorithm configuration contains an unregistered choice value.");
				}
			}
		}

		AlgorithmConfiguration resolveAlgorithmConfiguration(const TraceBuildRequest& request,
			const AlgorithmDescriptor& descriptor)
		{
			AlgorithmConfiguration configuration = request.hasExplicitConfiguration
				? request.configuration : importLegacyConfiguration(request, descriptor);

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
				if (!configuration.setOptionValue(resolved->target, resolved->value))
					throw std::invalid_argument("The requested algorithm option group has no trace configuration binding.");
			}

			if (request.hasExplicitConfiguration) validateExplicitChoiceOptions(configuration, descriptor);
			return configuration;
		}

		void projectAlgorithmConfigurationToLegacyFields(TraceBuildRequest& request,
			const AlgorithmConfiguration& configuration, const AlgorithmDescriptor& descriptor)
		{
			request.configuration = configuration;
			request.hasExplicitConfiguration = true;
			request.shellGapSequence = configuration.shellGapSequence;
			request.mergeWritebackMode = configuration.mergeWritebackMode;
			request.mergeExecutionMode = configuration.mergeExecutionMode;
			request.radixBase = configuration.radixBase;
			request.timeMagnitude = configuration.timeMagnitude;
			request.rotateMergeBufferSize = configuration.rotateMergeBufferSize;
			if (descriptor.id == SortAlgorithm::ArrayvBaseNHeap)
				request.bucketCount = configuration.heapArity;
			else if (descriptor.id == SortAlgorithm::ArrayvGrail)
				request.bucketCount = static_cast<int>(configuration.grailBufferMode);
			else request.bucketCount = configuration.bucketCount;
		}
	}
}
