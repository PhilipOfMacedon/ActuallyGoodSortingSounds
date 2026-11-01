#include "AlgorithmConfigurationResolver.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>
#include <stdexcept>

namespace sortinglogic
{
	namespace detail
	{
		namespace
		{
			void applyPropertyValue(AlgorithmConfiguration& configuration, const std::string& propertyId,
				const AlgorithmPropertyValue& value)
			{
				switch (value.type)
				{
				case AlgorithmPropertyValueType::Boolean:
					configuration.setBooleanValue(propertyId, value.booleanValue);
					break;
				case AlgorithmPropertyValueType::Choice:
					configuration.setChoiceValue(propertyId, value.choiceId, value.integerValue);
					break;
				case AlgorithmPropertyValueType::BoundedInteger:
					configuration.setValue(propertyId, value.integerValue);
					break;
				case AlgorithmPropertyValueType::FiniteDecimal:
					configuration.setDecimalValue(propertyId, value.decimalValue);
					break;
				}
			}

			void applyDefault(AlgorithmConfiguration& configuration, const AlgorithmPropertyDescriptor& property)
			{
				AlgorithmPropertyValue value;
				value.type = property.valueType;
				value.booleanValue = property.value != 0;
				value.integerValue = property.value;
				value.decimalValue = property.numericDefaultValue;
				if (property.valueType == AlgorithmPropertyValueType::Choice) value.choiceId = property.stableId;
				applyPropertyValue(configuration, property.groupId, value);
			}

			AlgorithmConfiguration defaultConfiguration(const AlgorithmDescriptor& descriptor)
			{
				AlgorithmConfiguration configuration;
				for (std::size_t i = 0; i < descriptor.supportedOptions.size(); ++i)
					if (descriptor.supportedOptions[i].isDefault)
						applyDefault(configuration, descriptor.supportedOptions[i]);
				return configuration;
			}

			bool parseBoundedInteger(const std::string& value, int& parsed)
			{
				if (value.empty()) return false;
				char* end = nullptr;
				errno = 0;
				const long number = std::strtol(value.c_str(), &end, 10);
				if (errno == ERANGE || end != value.c_str() + value.size() || number < (std::numeric_limits<int>::min)() ||
					number > (std::numeric_limits<int>::max)()) return false;
				parsed = static_cast<int>(number);
				return true;
			}

			bool parseFiniteDecimal(const std::string& value, double& parsed)
			{
				if (value.empty()) return false;
				char* end = nullptr;
				errno = 0;
				const double number = std::strtod(value.c_str(), &end);
				if (errno == ERANGE || end != value.c_str() + value.size() || !std::isfinite(number)) return false;
				parsed = number;
				return true;
			}

			const AlgorithmPropertyDescriptor* firstProperty(const AlgorithmDescriptor& descriptor,
				const std::string& propertyId)
			{
				for (std::size_t i = 0; i < descriptor.supportedOptions.size(); ++i)
					if (propertyId == descriptor.supportedOptions[i].groupId) return &descriptor.supportedOptions[i];
				return nullptr;
			}

			void validateExplicitProperties(const AlgorithmConfiguration& configuration,
				const AlgorithmDescriptor& descriptor)
			{
				std::set<std::string> validatedGroups;
				for (std::size_t i = 0; i < descriptor.supportedOptions.size(); ++i)
				{
					const AlgorithmPropertyDescriptor& property = descriptor.supportedOptions[i];
					if (!validatedGroups.insert(property.groupId).second) continue;

					if (property.valueType == AlgorithmPropertyValueType::FiniteDecimal)
					{
						const std::map<std::string, double>::const_iterator selected = configuration.numericValues.find(property.groupId);
						if (selected == configuration.numericValues.end() || !std::isfinite(selected->second) ||
							selected->second < property.minimumNumericValue || selected->second > property.maximumNumericValue)
							throw std::invalid_argument("The requested finite-decimal property is missing or outside its registered bounds.");
						continue;
					}

					int selectedValue = 0;
					if (!configuration.optionValue(property.groupId, selectedValue))
						throw std::invalid_argument("The requested algorithm property has no registered value.");
					if (property.valueType == AlgorithmPropertyValueType::BoundedInteger)
					{
						if (static_cast<double>(selectedValue) < property.minimumNumericValue ||
							static_cast<double>(selectedValue) > property.maximumNumericValue)
							throw std::invalid_argument("The requested integer property is outside its registered bounds.");
						continue;
					}

					bool registeredValue = false;
					for (std::size_t candidateIndex = 0; candidateIndex < descriptor.supportedOptions.size(); ++candidateIndex)
					{
						const AlgorithmPropertyDescriptor& candidate = descriptor.supportedOptions[candidateIndex];
						if (candidate.groupId == property.groupId && candidate.value == selectedValue)
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
					if (!firstProperty(descriptor, value->first))
						throw std::invalid_argument("The algorithm configuration contains an unregistered property ID.");

				for (std::map<std::string, double>::const_iterator value = configuration.numericValues.begin();
					value != configuration.numericValues.end(); ++value)
				{
					const AlgorithmPropertyDescriptor* property = firstProperty(descriptor, value->first);
					if (!property || property->valueType != AlgorithmPropertyValueType::FiniteDecimal)
						throw std::invalid_argument("The algorithm configuration contains an unregistered finite-decimal property ID.");
				}

				for (AlgorithmPropertyValues::const_iterator value = configuration.properties.begin();
					value != configuration.properties.end(); ++value)
				{
					const AlgorithmPropertyDescriptor* property = firstProperty(descriptor, value->first);
					if (!property || property->valueType != value->second.type)
						throw std::invalid_argument("The algorithm configuration contains a property with an unregistered ID or value type.");
				}
			}
		}

		AlgorithmConfiguration resolveAlgorithmConfiguration(const TraceBuildRequest& request,
			const AlgorithmDescriptor& descriptor)
		{
			AlgorithmConfiguration configuration = defaultConfiguration(descriptor);
			if (request.hasExplicitConfiguration)
			{
				for (AlgorithmPropertyValues::const_iterator value = request.configuration.properties.begin();
					value != request.configuration.properties.end(); ++value)
				{
					const AlgorithmPropertyDescriptor* schema = firstProperty(descriptor, value->first);
					if (!schema || schema->valueType == value->second.type)
					{
						applyPropertyValue(configuration, value->first, value->second);
						continue;
					}
					// Older typed-config callers only supplied the integer projection.
					// Reinterpret that value through the registered schema at this edge.
					const std::map<std::string, int>::const_iterator legacyValue = request.configuration.values.find(value->first);
					if (legacyValue == request.configuration.values.end())
						throw std::invalid_argument("The explicit algorithm property value type does not match its registration.");
					if (schema->valueType == AlgorithmPropertyValueType::Boolean)
						configuration.setBooleanValue(value->first, legacyValue->second != 0);
					else if (schema->valueType == AlgorithmPropertyValueType::Choice)
					{
						const AlgorithmPropertyDescriptor* choice = nullptr;
						for (std::size_t option = 0; option < descriptor.supportedOptions.size(); ++option)
							if (value->first == descriptor.supportedOptions[option].groupId &&
								legacyValue->second == descriptor.supportedOptions[option].value)
							{ choice = &descriptor.supportedOptions[option]; break; }
						if (!choice) throw std::invalid_argument("The explicit algorithm choice is not registered.");
						configuration.setChoiceValue(value->first, choice->stableId, choice->value);
					}
					else throw std::invalid_argument("The explicit algorithm property value type does not match its registration.");
				}
				for (std::map<std::string, int>::const_iterator value = request.configuration.values.begin();
					value != request.configuration.values.end(); ++value)
					if (request.configuration.properties.find(value->first) == request.configuration.properties.end())
						configuration.setValue(value->first, value->second);
				for (std::map<std::string, double>::const_iterator value = request.configuration.numericValues.begin();
					value != request.configuration.numericValues.end(); ++value)
					if (request.configuration.properties.find(value->first) == request.configuration.properties.end())
						configuration.setDecimalValue(value->first, value->second);
			}

			for (std::map<std::string, std::string>::const_iterator selected = request.optionSelections.begin();
				selected != request.optionSelections.end(); ++selected)
			{
				const AlgorithmPropertyDescriptor* schema = firstProperty(descriptor, selected->first);
				if (schema && schema->valueType == AlgorithmPropertyValueType::BoundedInteger)
				{
					int numericValue = 0;
					if (!parseBoundedInteger(selected->second, numericValue) ||
						static_cast<double>(numericValue) < schema->minimumNumericValue ||
						static_cast<double>(numericValue) > schema->maximumNumericValue)
						throw std::invalid_argument("The requested bounded-integer property is invalid or outside its registered bounds.");
					configuration.setValue(selected->first, numericValue);
					continue;
				}
				if (schema && schema->valueType == AlgorithmPropertyValueType::FiniteDecimal)
				{
					double numericValue = 0.0;
					if (!parseFiniteDecimal(selected->second, numericValue) || numericValue < schema->minimumNumericValue ||
						numericValue > schema->maximumNumericValue)
						throw std::invalid_argument("The requested finite-decimal property is invalid or outside its registered bounds.");
					configuration.setDecimalValue(selected->first, numericValue);
					continue;
				}

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
				if (resolved->valueType == AlgorithmPropertyValueType::Boolean)
					configuration.setBooleanValue(resolved->groupId, resolved->value != 0);
				else
					configuration.setChoiceValue(resolved->groupId, resolved->stableId, resolved->value);
			}

			validateExplicitProperties(configuration, descriptor);
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
			const AlgorithmPropertyDescriptor& property = descriptor->supportedOptions[i];
			if (!property.isDefault || presentation.properties.properties.find(property.groupId) != presentation.properties.properties.end()) continue;
			AlgorithmPropertyValue value;
			value.type = property.valueType;
			value.booleanValue = property.value != 0;
			value.integerValue = property.value;
			value.decimalValue = property.numericDefaultValue;
			if (property.valueType == AlgorithmPropertyValueType::Choice) value.choiceId = property.stableId;
			switch (property.valueType)
			{
			case AlgorithmPropertyValueType::Boolean: presentation.properties.setBooleanValue(property.groupId, value.booleanValue); break;
			case AlgorithmPropertyValueType::Choice: presentation.properties.setChoiceValue(property.groupId, value.choiceId, value.integerValue); break;
			case AlgorithmPropertyValueType::BoundedInteger: presentation.properties.setValue(property.groupId, value.integerValue); break;
			case AlgorithmPropertyValueType::FiniteDecimal: presentation.properties.setDecimalValue(property.groupId, value.decimalValue); break;
			}
		}
		return presentation;
	}
}
