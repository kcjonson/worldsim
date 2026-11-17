#pragma once

namespace conditions {

	class ConditionVocabulary;

	/// need(kind) (0-100), attribute(kind) (0-20), current_task(kind) and mid_craft, from the colonist's own
	/// components. current_task takes a task type, or `work` for any work task.
	void provideColonistTerms(ConditionVocabulary& vocabulary);

} // namespace conditions
