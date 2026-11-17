#pragma once

namespace conditions {

	class ConditionVocabulary;

	/// near(kind, radius) and count_near(kind, radius) from what the colonist remembers, measured from where he
	/// stands: food, storage, station, bed and loose_item by the capability memory indexes them under, colonist
	/// by the colonists memory holds a last known position for. Nothing remembers structures or fires by
	/// position yet, so those kinds answer Unknown.
	void provideNearbyTerms(ConditionVocabulary& vocabulary);

} // namespace conditions
