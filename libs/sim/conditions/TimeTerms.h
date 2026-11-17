#pragma once

namespace conditions {

	class ConditionVocabulary;

	/// The hours of the day that count as night: from `start` to midnight and on to `end`.
	struct NightHours {
		float start = 20.0F;
		float end = 6.0F;
	};

	/// hour (0-24) and is_night from the world's TimeSystem; both are Unknown in a world without one.
	void provideTimeTerms(ConditionVocabulary& vocabulary, NightHours night);

} // namespace conditions
