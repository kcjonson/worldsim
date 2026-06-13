#pragma once

namespace ecs {

	/// Hit points for a structural entity. maxHp is wired from material config
	/// (HP per m² × area, or per m × length) when the blueprint transitions to
	/// UnderConstruction. Damage and repair systems arrive with the threats/combat
	/// epic; this component exists now because the architecture specifies it and
	/// adding it later would require a data migration.
	struct StructureHealth {
		float hp = 0.0F;
		float maxHp = 0.0F;
	};

} // namespace ecs
