#include "SimTerms.h"

#include "ColonistTerms.h"
#include "ConditionVocabulary.h"
#include "NearbyTerms.h"
#include "TimeTerms.h"

namespace conditions {

	std::vector<std::string> buildSimVocabulary(ConditionVocabulary& vocabulary, const assets::ConditionConfig& config) {
		std::vector<std::string> errors = vocabulary.declare(config);
		provideColonistTerms(vocabulary, config, errors);
		provideNearbyTerms(vocabulary, config, errors);
		provideTimeTerms(vocabulary, config, errors);
		vocabulary.freeze();
		return errors;
	}

} // namespace conditions
