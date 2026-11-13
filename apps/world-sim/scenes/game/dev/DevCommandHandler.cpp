#include "DevCommandHandler.h"

#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/game/world/placement/PlacementSystem.h"
#include "scenes/game/world/selection/SelectionSystem.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>
#include <assets/placement/FloraMeasure.h>
#include <assets/placement/PlacementExecutor.h>

#include <ui/components/Toast.h>

#include <construction/ConstructionWorld.h>
#include <construction/SnapEngine.h>

#include <debug/Tunables.h>

#include <ecs/InventoryMass.h>
#include <ecs/World.h>
#include <ecs/WorldSource.h>
#include <ecs/components/Action.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Needs.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/Structure.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/StructureHealth.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>
#include <ecs/systems/ConstructionSystem.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/TimeSystem.h>

#include <assets/RecipeRegistry.h>

#include <utils/Log.h>

#include <world/camera/WorldCamera.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/TerrainPolygonQuery.h>

#include <primitives/Primitives.h>

#include <predicates/Predicates.h>

#include <worldgen/data/Biome.h>
#include <worldgen/data/GeneratedWorld.h>
#include <worldgen/sampling/LandingSite.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <unordered_map>

namespace world_sim {

	std::vector<app::DevServer::Registration> DevCommandHandler::registerWith(app::DevServer& server) {
		std::vector<app::DevServer::Registration> registrations;
		const auto								  verb = [&](const char* name, void (DevCommandHandler::*run)(const app::DevRequest&)) {
			   registrations.push_back(server.route(name, [this, run](const app::DevRequest& cmd) {
				   (this->*run)(cmd);
				   return app::DevResponse{.body = "{\"status\":\"ok\",\"verb\":\"" + jsonEscape(cmd.name) + "\"}"};
			   }));
		};
		verb("freebuild", &DevCommandHandler::devFreeBuild);
		verb("construction", &DevCommandHandler::devFreeBuild);
		verb("give", &DevCommandHandler::devGive);
		verb("spawn", &DevCommandHandler::devSpawn);
		verb("colonist", &DevCommandHandler::devColonist);
		verb("need", &DevCommandHandler::devNeed);
		verb("time", &DevCommandHandler::devTime);
		verb("teleport", &DevCommandHandler::devTeleport);
		verb("select", &DevCommandHandler::devSelect);
		verb("kill", &DevCommandHandler::devKill);
		verb("complete", &DevCommandHandler::devComplete);
		verb("foundation", &DevCommandHandler::devFoundation);
		verb("walls", &DevCommandHandler::devWalls);
		verb("opening", &DevCommandHandler::devOpening);
		verb("craft", &DevCommandHandler::devCraft);
		verb("tunable", &DevCommandHandler::devTunable);
		registrations.push_back(server.route("foundation-edit", [this](const app::DevRequest& cmd) {
			return app::DevResponse{.body = devFoundationEdit(cmd)};
		}));
		registrations.push_back(server.route("flora", [this](const app::DevRequest& cmd) { return devFlora(cmd); }));
		registrations.push_back(server.route("ground", [this](const app::DevRequest& cmd) { return devGround(cmd); }));
		registrations.push_back(server.route("storage", [this](const app::DevRequest& cmd) { return devStorage(cmd); }));

		const auto view = [&](const char* what, void (DevCommandHandler::*serialize)(std::ostringstream&)) {
			registrations.push_back(server.stateProvider(what, [this, serialize] {
				std::ostringstream out;
				(this->*serialize)(out);
				return out.str();
			}));
		};
		view("colonists", &DevCommandHandler::serializeColonists);
		view("construction", &DevCommandHandler::serializeConstruction);
		view("stations", &DevCommandHandler::serializeStations);
		view("storage", &DevCommandHandler::serializeStorage);
		view("time", &DevCommandHandler::serializeTime);
		view("landing", &DevCommandHandler::serializeLanding);
		view("summary", &DevCommandHandler::serializeSummary);
		registrations.push_back(server.stateProvider("tunables", [] { return Foundation::Tunables::instance().toJson(); }));
		return registrations;
	}

	std::string DevCommandHandler::param(const app::DevRequest& cmd, const std::string& key, const std::string& fallback) {
		const auto it = cmd.params.find(key);
		return it != cmd.params.end() ? it->second : fallback;
	}

	bool DevCommandHandler::hasParam(const app::DevRequest& cmd, const std::string& key) {
		return cmd.params.contains(key);
	}

	void DevCommandHandler::devTunable(const app::DevRequest& cmd) {
		// tunable?name=terrain/shore/drySandW&value=1.6 (a color is value=r,g,b), or
		// tunable?reset=terrain/shore/* (a trailing '*' resets the whole group).
		Foundation::Tunables& tunables = Foundation::Tunables::instance();
		if (hasParam(cmd, "reset")) {
			const std::string name = param(cmd, "reset");
			const size_t	  count = tunables.reset(name);
			LOG_INFO(Game, "[DevAPI] tunable reset '%s': %zu value(s)", name.c_str(), count);
			return;
		}

		const std::string  name = param(cmd, "name");
		const std::string  spec = param(cmd, "value");
		std::vector<float> values;
		if (!Foundation::Tunables::parseValues(spec, values)) {
			LOG_WARNING(Game, "[DevAPI] tunable '%s' rejected value '%s' (malformed or non-finite)", name.c_str(), spec.c_str());
			return;
		}
		if (!tunables.set(name, values)) {
			LOG_WARNING(Game, "[DevAPI] tunable '%s' rejected value '%s' (unknown name or wrong component count)", name.c_str(), spec.c_str());
			return;
		}
		LOG_INFO(Game, "[DevAPI] tunable '%s' = %s", name.c_str(), spec.c_str());
	}

	void DevCommandHandler::devFreeBuild(const app::DevRequest& cmd) {
		// Accept on= (freebuild verb) or freebuild= (construction verb); truthy on 1/on/true/yes.
		std::string value = hasParam(cmd, "on") ? param(cmd, "on") : param(cmd, "freebuild", "1");
		const bool	on = (value == "1" || value == "on" || value == "true" || value == "yes");
		m_ctx.world->getSystem<ecs::ConstructionSystem>().setFreeBuild(on);
		LOG_INFO(Game, "[DevAPI] free-build %s", on ? "ON" : "OFF");
		m_ctx.ui->pushNotification("Dev", on ? "Free-build ON" : "Free-build OFF", ui::ToastSeverity::Info);
	}

	void DevCommandHandler::devGive(const app::DevRequest& cmd) {
		const std::string material = param(cmd, "material", "Wood");
		const long		  nRaw = std::strtol(param(cmd, "n", "100").c_str(), nullptr, 10);
		if (nRaw <= 0) {
			LOG_WARNING(Game, "[DevAPI] give: n must be > 0");
			return;
		}
		const auto		  n = static_cast<uint32_t>(nRaw);
		const std::string where = param(cmd, "where", "site");

		if (where == "site") {
			const uint32_t credited = m_ctx.world->getSystem<ecs::ConstructionSystem>().creditMaterialToSites(material, n);
			LOG_INFO(Game, "[DevAPI] give: credited %u %s across build sites", credited, material.c_str());
			m_ctx.ui->pushNotification("Dev", "Credited " + std::to_string(credited) + " " + material + " to sites", ui::ToastSeverity::Info);
			return;
		}

		if (where == "loose") {
			const auto* def = assets::AssetRegistry::Get().getDefinition(material);
			if (def == nullptr) {
				LOG_WARNING(Game, "[DevAPI] give loose: unknown asset '%s'", material.c_str());
				m_ctx.ui->pushNotification("Dev", "Unknown asset: " + material, ui::ToastSeverity::Warning);
				return;
			}
			// Spawning a furniture or station def would make a live box/station that is also a pile.
			if (!def->itemProperties.has_value()) {
				LOG_WARNING(Game, "[DevAPI] give loose: '%s' is not an item", material.c_str());
				m_ctx.ui->pushNotification("Dev", "Not an item: " + material, ui::ToastSeverity::Warning);
				return;
			}
			// Each new pile is cleared against every pile already placed, so the pile count (not
			// the unit count) is what a big n costs.
			constexpr uint32_t kMaxLoosePiles = 50;
			const uint32_t	   stackSize = def->itemProperties->stackSize;
			const uint32_t	   piles = stackSize == 0 ? 1U : (n - 1U) / stackSize + 1U;
			if (piles > kMaxLoosePiles) {
				LOG_WARNING(Game, "[DevAPI] give loose: %u '%s' is %u piles, over the %u limit", n, material.c_str(), piles, kMaxLoosePiles);
				m_ctx.ui->pushNotification("Dev", "Too many: " + std::to_string(piles) + " piles of " + material, ui::ToastSeverity::Warning);
				return;
			}
			const Foundation::Vec2 at = parsePoint(param(cmd, "at"));
			if (!requireValidPosition(at, "give loose")) {
				return;
			}
			m_ctx.dropLoose(material, at.x, at.y, n);
			LOG_INFO(Game, "[DevAPI] give: dropped %u loose '%s' at (%.1f, %.1f)", n, material.c_str(), at.x, at.y);
			m_ctx.ui->pushNotification("Dev", "Dropped " + std::to_string(n) + " " + material, ui::ToastSeverity::Info);
			return;
		}

		if (where == "colonist" || where == "storage") {
			const Foundation::Vec2 at = parsePoint(param(cmd, "at"));
			ecs::Inventory*		   inventory = (where == "colonist") ? nearestColonistInventory(at) : nearestStorageInventory(at);
			if (inventory == nullptr) {
				LOG_WARNING(Game, "[DevAPI] give %s: no target found", where.c_str());
				m_ctx.ui->pushNotification("Dev", "No " + where + " to give to", ui::ToastSeverity::Warning);
				return;
			}

			// where=colonist takes slot=pack|hands|belt (default pack) so every gear
			// surface can be filled for testing. hands respects the two-hand mirror.
			const std::string slot = param(cmd, "slot", "pack");
			uint32_t		  added = 0;
			if (where == "colonist" && slot == "hands") {
				const auto& registry = assets::AssetRegistry::Get();
				if (ecs::itemIsTwoHand(registry, material)) {
					added = ecs::addArmful(*inventory, registry, material, n);
				} else {
					while (added < n && inventory->pickUp(material)) {
						++added;
					}
				}
			} else if (where == "colonist" && slot == "belt") {
				while (added < n && inventory->stowToBelt(material)) {
					++added;
				}
			} else {
				added = inventory->addItem(material, n);
			}
			LOG_INFO(Game, "[DevAPI] give: added %u/%u %s to nearest %s (%s)", added, n, material.c_str(), where.c_str(), slot.c_str());
			m_ctx.ui->pushNotification("Dev", "Gave " + std::to_string(added) + " " + material + " to " + where, ui::ToastSeverity::Info);
			return;
		}

		LOG_WARNING(Game, "[DevAPI] give: unknown where=%s (site|loose|colonist|storage)", where.c_str());
		m_ctx.ui->pushNotification("Dev", "Unknown where: " + where, ui::ToastSeverity::Warning);
	}

	void DevCommandHandler::devSpawn(const app::DevRequest& cmd) {
		const std::string def = param(cmd, "def");
		if (def.empty()) {
			LOG_WARNING(Game, "[DevAPI] spawn: missing def=<assetName>");
			m_ctx.ui->pushNotification("Dev", "spawn needs def=", ui::ToastSeverity::Warning);
			return;
		}
		if (assets::AssetRegistry::Get().getDefinition(def) == nullptr) {
			LOG_WARNING(Game, "[DevAPI] spawn: unknown def '%s'", def.c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown asset: " + def, ui::ToastSeverity::Warning);
			return;
		}
		const Foundation::Vec2 at = parsePoint(param(cmd, "at"));
		if (!requireValidPosition(at, "spawn")) {
			return;
		}
		const long			   nRaw = std::strtol(param(cmd, "n", "1").c_str(), nullptr, 10);
		const long			   n = nRaw < 1 ? 1 : nRaw;
		const float			   scatter = std::strtof(param(cmd, "scatter", "0").c_str(), nullptr);

		// requireValidPosition gated the CENTER, but each scattered offset is its own point that
		// can land off-mesh (in a river). Snap every scattered position onto walkable ground with
		// the same primitive the colonist drops use: minDist 0 keeps a position that is already
		// valid and otherwise returns the nearest walkable point, so nothing spawns in water. A
		// position with no mesh anywhere near it (nullopt) is skipped rather than placed off-mesh.
		long spawned = 0;
		for (long i = 0; i < n; ++i) {
			const Foundation::Vec2 off = spreadOffset(i, n, scatter);
			const glm::vec2		   raw{at.x + off.x, at.y + off.y};
			const std::optional<glm::vec2> placed =
				(m_ctx.navigation != nullptr) ? m_ctx.navigation->findValidPositionNear(raw, 0.0F) : std::optional<glm::vec2>{raw};
			if (!placed.has_value()) {
				continue;
			}
			m_ctx.placement->spawnEntity(def, {placed->x, placed->y});
			++spawned;
		}
		LOG_INFO(Game, "[DevAPI] spawn: %ld/%ld x '%s' near (%.1f, %.1f)", spawned, n, def.c_str(), at.x, at.y);
		m_ctx.ui->pushNotification("Dev", "Spawned " + std::to_string(spawned) + " " + def, ui::ToastSeverity::Info);
	}

	void DevCommandHandler::devColonist(const app::DevRequest& cmd) {
		const Foundation::Vec2 at = parsePoint(param(cmd, "at"));
		if (!requireMoverPosition(at, "colonist")) {
			return;
		}
		const long			   nRaw = std::strtol(param(cmd, "n", "1").c_str(), nullptr, 10);
		const long			   n = nRaw < 1 ? 1 : nRaw;
		const std::string	   baseName = param(cmd, "name", "Dev");

		for (long i = 0; i < n; ++i) {
			const Foundation::Vec2 off = spreadOffset(i, n, n > 1 ? 1.0F : 0.0F);
			const std::string	   name = (n == 1) ? baseName : baseName + std::to_string(i + 1);
			m_ctx.spawnColonist({at.x + off.x, at.y + off.y}, name);
		}
		LOG_INFO(Game, "[DevAPI] colonist: spawned %ld at (%.1f, %.1f)", n, at.x, at.y);
		m_ctx.ui->pushNotification("Dev", "Spawned " + std::to_string(n) + " colonist(s)", ui::ToastSeverity::Info);
	}

	bool DevCommandHandler::requireValidPosition(Foundation::Vec2 at, const char* verb) {
		// Single validity gate: a world position is placeable IFF it is on an active
		// walkable nav face (NavigationSystem::isValidPosition). No terrain/world-source
		// reads here -- runtime walkability is owned by the nav mesh.
		if (m_ctx.navigation != nullptr && m_ctx.navigation->isValidPosition({at.x, at.y})) {
			return true;
		}
		const bool		  meshed = m_ctx.navigation != nullptr && m_ctx.navigation->inSimArea({at.x, at.y});
		const char* const why	 = meshed ? "is water or a placed obstacle" : "is ground no mover has seen (off the nav mesh)";
		LOG_WARNING(Game, "[DevAPI] %s: error: %.1f,%.1f %s, refused", verb, at.x, at.y, why);
		m_ctx.ui->pushNotification(
			"Dev", "Refused: (" + std::to_string(at.x) + ", " + std::to_string(at.y) + ") " + why, ui::ToastSeverity::Warning
		);
		return false;
	}

	bool DevCommandHandler::requireMoverPosition(Foundation::Vec2 at, const char* verb) {
		if (m_ctx.navigation != nullptr && !m_ctx.navigation->inSimArea({at.x, at.y})) {
			return true;
		}
		return requireValidPosition(at, verb);
	}

	bool DevCommandHandler::requireBuildableArea(const std::vector<Foundation::Vec2>& pts, const char* verb) {
		// The WHOLE footprint must be BUILDABLE, matching the foundation build tool (DrawingSystem):
		// water and off-mesh gaps block, but clearable entities (trees/rocks) do not -- they
		// become clear tasks. Wall conflicts are the validator's job. Without a nav system wired
		// (headless/test), validity is owned elsewhere; mirror requireValidPosition's permissive fallback.
		if (m_ctx.navigation == nullptr) {
			return true;
		}
		if (m_ctx.navigation->isAreaBuildable(Foundation::toGlmVec2(pts))) {
			return true;
		}
		LOG_WARNING(Game, "[DevAPI] %s: error: footprint not on buildable ground (water, or ground no mover has seen: off the nav mesh), refused", verb);
		m_ctx.ui->pushNotification("Dev", "Refused: footprint on water or ground no mover has seen", ui::ToastSeverity::Warning);
		return false;
	}

	bool DevCommandHandler::requireWalkableChain(const std::vector<Foundation::Vec2>& pts, const char* verb) {
		if (m_ctx.navigation == nullptr) {
			return true;
		}
		if (m_ctx.navigation->isPolylineWalkable(Foundation::toGlmVec2(pts))) {
			return true;
		}
		LOG_WARNING(Game, "[DevAPI] %s: error: chain not fully on an active walkable nav mesh, refused", verb);
		m_ctx.ui->pushNotification("Dev", "Refused: wall chain not fully on walkable ground", ui::ToastSeverity::Warning);
		return false;
	}

	ecs::EntityID DevCommandHandler::nearestColonist(Foundation::Vec2 at) {
		ecs::EntityID best = ecs::kInvalidEntity;
		float		  bestDistSq = 0.0F;
		for (auto [entity, colonist, pos] : m_ctx.world->view<ecs::Colonist, ecs::Position>()) {
			const float dx = pos.value.x - at.x;
			const float dy = pos.value.y - at.y;
			const float distSq = dx * dx + dy * dy;
			if (best == ecs::kInvalidEntity || distSq < bestDistSq) {
				best = entity;
				bestDistSq = distSq;
			}
		}
		return best;
	}

	ecs::Inventory* DevCommandHandler::nearestColonistInventory(Foundation::Vec2 at) {
		const ecs::EntityID id = nearestColonist(at);
		return id == ecs::kInvalidEntity ? nullptr : m_ctx.world->getComponent<ecs::Inventory>(id);
	}

	ecs::Inventory* DevCommandHandler::nearestStorageInventory(Foundation::Vec2 at) {
		const ecs::EntityID id = nearestStorageEntity(at);
		return id == ecs::kInvalidEntity ? nullptr : m_ctx.world->getComponent<ecs::Inventory>(id);
	}

	ecs::EntityID DevCommandHandler::nearestStorageEntity(Foundation::Vec2 at) {
		ecs::EntityID best = ecs::kInvalidEntity;
		float		  bestDistSq = 0.0F;
		for (auto [entity, storage, pos] : m_ctx.world->view<ecs::StorageConfiguration, ecs::Position>()) {
			const float dx = pos.value.x - at.x;
			const float dy = pos.value.y - at.y;
			const float distSq = dx * dx + dy * dy;
			if (best == ecs::kInvalidEntity || distSq < bestDistSq) {
				best = entity;
				bestDistSq = distSq;
			}
		}
		return best;
	}

	ecs::EntityID DevCommandHandler::parseEntity(const std::string& spec) {
		return static_cast<ecs::EntityID>(std::strtoull(spec.c_str(), nullptr, 10));
	}

	bool DevCommandHandler::parseNeedType(const std::string& name, ecs::NeedType& out) {
		for (size_t i = 0; i < ecs::kNeedLabels.size(); ++i) {
			if (equalsIgnoreCase(name, ecs::kNeedLabels[i])) {
				out = static_cast<ecs::NeedType>(i);
				return true;
			}
		}
		return false;
	}

	bool DevCommandHandler::equalsIgnoreCase(const std::string& a, const char* b) {
		size_t i = 0;
		for (; i < a.size() && b[i] != '\0'; ++i) {
			if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
				return false;
			}
		}
		return i == a.size() && b[i] == '\0';
	}

	float DevCommandHandler::parseClock(const std::string& spec) {
		const float h = std::strtof(spec.c_str(), nullptr);
		float		m = 0.0F;
		const auto	colon = spec.find(':');
		if (colon != std::string::npos) {
			m = std::strtof(spec.c_str() + colon + 1, nullptr);
		}
		return h + m / 60.0F;
	}

	float DevCommandHandler::parseDuration(const std::string& spec) {
		char*		end = nullptr;
		const float v = std::strtof(spec.c_str(), &end);
		if (end != nullptr && (*end == 'h' || *end == 'H')) {
			return v * 60.0F;
		}
		return v;
	}

	void DevCommandHandler::devNeed(const app::DevRequest& cmd) {
		const ecs::EntityID id = parseEntity(param(cmd, "colonist"));
		auto*				needs = m_ctx.world->getComponent<ecs::NeedsComponent>(id);
		if (needs == nullptr) {
			LOG_WARNING(Game, "[DevAPI] need: no colonist #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "No such colonist", ui::ToastSeverity::Warning);
			return;
		}
		ecs::NeedType type{};
		if (!parseNeedType(param(cmd, "need"), type)) {
			LOG_WARNING(Game, "[DevAPI] need: unknown need '%s'", param(cmd, "need").c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown need", ui::ToastSeverity::Warning);
			return;
		}
		float value = std::strtof(param(cmd, "value", "100").c_str(), nullptr);
		value = value < 0.0F ? 0.0F : (value > 100.0F ? 100.0F : value);
		needs->get(type).value = value;
		LOG_INFO(Game, "[DevAPI] need: %s=%.1f on #%llu", ecs::needLabel(type), static_cast<double>(value), static_cast<unsigned long long>(id));
		m_ctx.ui->pushNotification("Dev", std::string(ecs::needLabel(type)) + " set", ui::ToastSeverity::Info);
	}

	void DevCommandHandler::devTime(const app::DevRequest& cmd) {
		auto& timeSystem = m_ctx.world->getSystem<ecs::TimeSystem>();
		bool  acted = false;

		if (hasParam(cmd, "speed")) {
			const long s = std::strtol(param(cmd, "speed").c_str(), nullptr, 10);
			if (s >= 0 && s <= 3) {
				timeSystem.setSpeed(static_cast<ecs::GameSpeed>(s));
				acted = true;
			} else {
				LOG_WARNING(Game, "[DevAPI] time: speed must be 0..3");
			}
		}
		if (hasParam(cmd, "set")) {
			timeSystem.setTimeOfDay(parseClock(param(cmd, "set")));
			acted = true;
		}
		if (hasParam(cmd, "skip")) {
			timeSystem.skipTime(parseDuration(param(cmd, "skip")));
			acted = true;
		}

		const auto snap = timeSystem.snapshot();
		LOG_INFO(Game, "[DevAPI] time: day %d, %.2fh, speed %d", snap.day, static_cast<double>(snap.timeOfDay), static_cast<int>(snap.speed));
		m_ctx.ui->pushNotification("Dev", acted ? "Time updated" : "time: speed|set|skip", acted ? ui::ToastSeverity::Info : ui::ToastSeverity::Warning);
	}

	void DevCommandHandler::devTeleport(const app::DevRequest& cmd) {
		const ecs::EntityID id = parseEntity(param(cmd, "colonist"));
		auto*				pos = m_ctx.world->getComponent<ecs::Position>(id);
		if (pos == nullptr) {
			LOG_WARNING(Game, "[DevAPI] teleport: no entity #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "No such entity", ui::ToastSeverity::Warning);
			return;
		}
		const Foundation::Vec2 to = parsePoint(param(cmd, "to"));
		if (!requireMoverPosition(to, "teleport")) {
			return;
		}
		pos->value = {to.x, to.y};
		if (auto* target = m_ctx.world->getComponent<ecs::MovementTarget>(id)) {
			target->active = false;
		}
		LOG_INFO(Game, "[DevAPI] teleport: #%llu -> (%.1f, %.1f)", static_cast<unsigned long long>(id), to.x, to.y);
		m_ctx.ui->pushNotification("Dev", "Teleported", ui::ToastSeverity::Info);
	}

	void DevCommandHandler::devSelect(const app::DevRequest& cmd) {
		ecs::EntityID id = ecs::kInvalidEntity;
		if (hasParam(cmd, "colonist")) {
			id = parseEntity(param(cmd, "colonist"));
		} else if (hasParam(cmd, "at")) {
			id = nearestColonist(parsePoint(param(cmd, "at")));
		} else {
			LOG_WARNING(Game, "[DevAPI] select: needs colonist=<id> or at=x,y");
			return;
		}
		if (id == ecs::kInvalidEntity || !m_ctx.world->isAlive(id)) {
			LOG_WARNING(Game, "[DevAPI] select: no colonist to select");
			m_ctx.ui->pushNotification("Dev", "No colonist to select", ui::ToastSeverity::Warning);
			return;
		}
		m_ctx.selection->selectColonist(id);
		LOG_INFO(Game, "[DevAPI] select: colonist #%llu", static_cast<unsigned long long>(id));
	}

	void DevCommandHandler::devKill(const app::DevRequest& cmd) {
		const ecs::EntityID id = parseEntity(param(cmd, "colonist"));
		if (!m_ctx.world->isAlive(id)) {
			LOG_WARNING(Game, "[DevAPI] kill: no entity #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "No such entity", ui::ToastSeverity::Warning);
			return;
		}
		const auto* colonistSel = std::get_if<world_sim::ColonistSelection>(&m_ctx.selection->current());
		if (colonistSel != nullptr && colonistSel->entityId == id) {
			m_ctx.selection->clearSelection();
		}
		ecs::destroyWorldEntity(*m_ctx.world, id);
		LOG_INFO(Game, "[DevAPI] kill: removed #%llu", static_cast<unsigned long long>(id));
		m_ctx.ui->pushNotification("Dev", "Colonist removed", ui::ToastSeverity::Info);
	}

	void DevCommandHandler::devComplete(const app::DevRequest& cmd) {
		const ecs::EntityID id = parseEntity(param(cmd, "id"));
		if (m_ctx.world->getSystem<ecs::ConstructionSystem>().forceCompleteBlueprint(id)) {
			LOG_INFO(Game, "[DevAPI] complete: built blueprint #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "Blueprint built", ui::ToastSeverity::Info);
		} else {
			LOG_WARNING(Game, "[DevAPI] complete: #%llu is not a buildable blueprint", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "Not a blueprint", ui::ToastSeverity::Warning);
		}
	}

	void DevCommandHandler::devFoundation(const app::DevRequest& cmd) {
		std::vector<Foundation::Vec2> pts = parsePointList(param(cmd, "pts"));
		if (pts.size() < 3) {
			LOG_WARNING(Game, "[DevAPI] foundation: pts needs >= 3 'x,y' pairs (got %zu)", pts.size());
			return;
		}
		// The WHOLE footprint must sit on buildable land (vertices, edges, AND interior).
		// A footprint that spans water between on-land corners, or clips a water hole,
		// refuses the whole placement (stamp nothing), matching the real build tool's gate.
		if (!requireBuildableArea(pts, "foundation")) {
			return;
		}
		const std::string material = param(cmd, "material", "Wood");

		auto&	   constructionWorld = m_ctx.drawing->world();
		const auto commit = constructionWorld.commitFoundation(pts, material);
		if (!commit.ok()) {
			LOG_WARNING(Game, "[DevAPI] foundation: commit rejected (status %d)", static_cast<int>(commit.status));
			m_ctx.ui->pushNotification("Dev", "Foundation commit rejected", ui::ToastSeverity::Warning);
			return;
		}

		const ecs::EntityID entity = m_ctx.drawing->spawnBlueprintEntity(commit.id);
		if (entity == ecs::kInvalidEntity) {
			LOG_WARNING(Game, "[DevAPI] foundation: spawn failed for #%llu", static_cast<unsigned long long>(commit.id));
			return;
		}

		const std::string builtStr = param(cmd, "built", "0");
		const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");
		if (built) {
			// Route through the SAME instant-completion path free-build uses.
			m_ctx.world->getSystem<ecs::ConstructionSystem>().forceCompleteBlueprint(entity);
		}
		LOG_INFO(
			Game,
			"[DevAPI] foundation #%llu spawned (%zu pts, %s, built=%d, entity %u)",
			static_cast<unsigned long long>(commit.id),
			pts.size(),
			material.c_str(),
			built ? 1 : 0,
			static_cast<uint32_t>(entity)
		);
		m_ctx.ui->pushNotification("Dev", built ? "Built foundation placed" : "Foundation blueprint placed", ui::ToastSeverity::Info);
	}

	std::string DevCommandHandler::devFoundationEdit(const app::DevRequest& cmd) {
		namespace ec = construction;
		const auto		  target = static_cast<ec::FoundationId>(std::strtoull(param(cmd, "id", "0").c_str(), nullptr, 10));
		const std::string modeName = param(cmd, "mode", "add");
		const std::vector<Foundation::Vec2> pts = parsePointList(param(cmd, "pts"));

		DrawingSystem::FoundationEditOutcome outcome;
		outcome.target = target;
		if (modeName != "add" && modeName != "subtract") {
			outcome.reason = "mode must be add or subtract";
		} else {
			// The SAME validate + apply path the Add / Subtract tool's close takes.
			const auto mode = modeName == "add" ? ec::FoundationEditMode::Add : ec::FoundationEditMode::Subtract;
			outcome = m_ctx.drawing->applyFoundationEdit(target, pts, mode);
		}

		if (outcome.ok) {
			LOG_INFO(
				Game,
				"[DevAPI] foundation-edit: #%llu %s ok (extension %llu, %.1f m^2)",
				static_cast<unsigned long long>(target),
				modeName.c_str(),
				static_cast<unsigned long long>(outcome.extension),
				static_cast<double>(outcome.areaSquareMeters)
			);
		} else {
			LOG_WARNING(
				Game,
				"[DevAPI] foundation-edit: #%llu %s rejected: %s",
				static_cast<unsigned long long>(target),
				modeName.c_str(),
				outcome.reason.c_str()
			);
		}

		std::ostringstream json;
		json << "{\"status\":\"" << (outcome.ok ? "ok" : "rejected") << "\",\"verb\":\"foundation-edit\",\"mode\":\""
			 << jsonEscape(modeName) << "\",\"reason\":\"" << jsonEscape(outcome.reason) << "\",\"target\":" << outcome.target
			 << ",\"extension\":" << outcome.extension << ",\"entity\":" << outcome.entity
			 << ",\"area\":" << outcome.areaSquareMeters << "}";
		return json.str();
	}

	app::DevResponse DevCommandHandler::devGround(const app::DevRequest& cmd) {
		// Shore farther than this is reported as none: the probe is for points at a bank.
		constexpr double kShoreSearchMm = 32000.0;
		const auto kindName = [](world::TerrainRingKind kind) {
			switch (kind) {
				case world::TerrainRingKind::Waterline:
					return "Waterline";
				case world::TerrainRingKind::Channel:
					return "Channel";
				case world::TerrainRingKind::Pond:
					return "Pond";
			}
			return "Unknown";
		};

		std::ostringstream out;
		out << "{\"points\":[";
		bool first = true;
		for (const Foundation::Vec2& p : parsePointList(param(cmd, "pts"))) {
			const glm::vec2 meters{p.x, p.y};
			out << (first ? "" : ",") << "{\"x\":" << p.x << ",\"y\":" << p.y;
			first = false;
			if (m_ctx.navigation != nullptr) {
				out << ",\"onMesh\":" << (m_ctx.navigation->isOnMesh(meters) ? "true" : "false") << ",\"valid\":"
					<< (m_ctx.navigation->isValidPosition(meters) ? "true" : "false") << ",\"buildable\":"
					<< (m_ctx.navigation->isPointBuildable(meters) ? "true" : "false");
			}
			const world::Chunk* chunk = m_ctx.chunks != nullptr ? m_ctx.chunks->getChunk(world::worldToChunk({p.x, p.y})) : nullptr;
			if (chunk == nullptr || !chunk->isReady()) {
				out << ",\"chunk\":false}";
				continue;
			}
			const geometry::Vec2i64			   mm{static_cast<std::int64_t>(std::llround(static_cast<double>(p.x) * 1000.0)),
										  static_cast<std::int64_t>(std::llround(static_cast<double>(p.y) * 1000.0))};
			const world::ChunkTerrainPolygons& polygons = chunk->terrainPolygons();
			const world::TerrainPolygonQuery   query(polygons);
			const double					   toWaterMm = query.distanceToWaterMm(mm, kShoreSearchMm);
			out << ",\"chunk\":true,\"inWater\":" << (query.isInsideWater(mm) ? "true" : "false") << ",\"toWaterM\":";
			if (std::isfinite(toWaterMm)) {
				out << toWaterMm / 1000.0;
			} else {
				out << "null";
			}
			out << ",\"rings\":[";
			bool firstRing = true;
			for (const world::TerrainRing& ring : polygons.rings) {
				if (geometry::pointInPolygon(mm, ring.ring) == geometry::PointInPolygon::Outside) {
					continue;
				}
				out << (firstRing ? "" : ",") << "{\"kind\":\"" << kindName(ring.kind) << "\",\"blocksMovement\":"
					<< (ring.blocksMovement ? "true" : "false") << ",\"meanHalfWidthM\":" << ring.meanHalfWidthM << "}";
				firstRing = false;
			}
			out << "]}";
		}
		out << "]}";
		return app::DevResponse{.body = out.str()};
	}

	app::DevResponse DevCommandHandler::devFlora(const app::DevRequest& cmd) {
		// Far past any crown (the largest planned is 4.9 m across), so every tree whose
		// crown reaches into the rect is gathered for the cover.
		constexpr float kCrownReachMeters = 10.0F;
		// How far from near= the nearest-of-age query looks.
		constexpr float kNearSearchMeters = 200.0F;

		// The placed statics over a rect, with the chunk squares placement holds data for.
		std::vector<assets::FloraRect>			 loadedAreas;
		std::vector<const assets::PlacedEntity*> entities;
		const auto gather = [&](const assets::FloraRect& area) {
			const auto firstChunk = world::worldToChunk({area.minX, area.minY});
			const auto lastChunk = world::worldToChunk({area.maxX, area.maxY});
			for (int32_t cy = firstChunk.y; cy <= lastChunk.y; ++cy) {
				for (int32_t cx = firstChunk.x; cx <= lastChunk.x; ++cx) {
					const world::ChunkCoordinate coord{cx, cy};
					const assets::SpatialIndex*	 index = m_ctx.placementIndex->getChunkIndex(coord);
					if (index == nullptr) {
						continue;
					}
					const world::WorldPosition origin = coord.origin();
					loadedAreas.push_back({origin.x, origin.y, origin.x + world::kChunkWorldSize, origin.y + world::kChunkWorldSize});
					const auto nearby = index->queryRect(area.minX, area.minY, area.maxX, area.maxY);
					entities.insert(entities.end(), nearby.begin(), nearby.end());
				}
			}
		};

		auto&											 registry = assets::AssetRegistry::Get();
		std::unordered_map<std::string, assets::FloraDefTraits> traits;
		const auto describeDefs = [&] {
			for (const assets::PlacedEntity* e : entities) {
				if (!e->isTallOccluder || traits.contains(e->defName)) {
					continue;
				}
				assets::FloraDefTraits t;
				if (const auto* def = registry.getDefinition(e->defName); def != nullptr && assets::isTreeDef(*def)) {
					t.isTree = true;
					t.woodPerTree = assets::woodPerTree(*def);
					for (const assets::AgeClass& age : def->ageClasses) {
						t.ageClassNames.push_back(age.name);
					}
				}
				traits.emplace(e->defName, t);
			}
		};

		if (hasParam(cmd, "age") || hasParam(cmd, "near")) {
			const std::string age = param(cmd, "age");
			float			  nearX = 0.0F;
			float			  nearY = 0.0F;
			if (age.empty() || std::sscanf(param(cmd, "near").c_str(), "%f,%f", &nearX, &nearY) != 2 || !std::isfinite(nearX) ||
				!std::isfinite(nearY)) {
				return {.status = 400, .body = R"({"error":"flora: age=<name> and near=x,y go together"})"};
			}
			if (!assets::floraPointWithinWorld(nearX, nearY)) {
				return {.status = 400, .body = R"({"error":"flora: near must be within 16777216 m of the origin on both axes"})"};
			}
			const std::string defName = param(cmd, "def");
			gather({nearX - kNearSearchMeters, nearY - kNearSearchMeters, nearX + kNearSearchMeters, nearY + kNearSearchMeters});
			describeDefs();
			const std::vector<assets::FloraTreeSighting> trees =
				assets::nearestTreesOfAge({nearX, nearY}, age, defName, assets::kFloraNearestMax, entities, traits);

			std::ostringstream json;
			json << std::fixed << std::setprecision(2) << "{\"age\":\"" << jsonEscape(age) << "\",\"near\":[" << nearX << "," << nearY
				 << "],\"def\":\"" << jsonEscape(defName) << "\",\"searchHalfSizeM\":" << kNearSearchMeters << ",\"trees\":[";
			for (size_t i = 0; i < trees.size(); ++i) {
				const assets::FloraTreeSighting& t = trees[i];
				// x and y round-trip exactly, so a position read back names the same tree; the distance stays at two decimals.
				json << (i == 0 ? "" : ",") << "{\"def\":\"" << jsonEscape(t.defName) << "\",\"x\":" << std::defaultfloat
					 << std::setprecision(std::numeric_limits<float>::max_digits10) << t.position.x << ",\"y\":" << t.position.y
					 << std::fixed << std::setprecision(2) << ",\"ageClass\":\"" << jsonEscape(t.ageClass) << "\",\"variant\":" << t.variant
					 << ",\"distance\":" << t.distance << "}";
			}
			json << "]}";
			return {.body = json.str()};
		}

		assets::FloraRect rect;
		if (hasParam(cmd, "rect")) {
			float x0 = 0.0F;
			float y0 = 0.0F;
			float x1 = 0.0F;
			float y1 = 0.0F;
			if (std::sscanf(param(cmd, "rect").c_str(), "%f,%f,%f,%f", &x0, &y0, &x1, &y1) != 4 || !std::isfinite(x0) ||
				!std::isfinite(y0) || !std::isfinite(x1) || !std::isfinite(y1)) {
				return {.status = 400, .body = R"({"error":"flora: rect must be x0,y0,x1,y1 in world metres"})"};
			}
			rect = {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)};
		} else {
			int w = 0;
			int h = 0;
			Renderer::Primitives::getLogicalViewport(w, h);
			const Foundation::Rect view = m_ctx.camera->getVisibleRect(w, h, m_ctx.pixelsPerMeter);
			rect = {view.x, view.y, view.x + view.width, view.y + view.height};
		}
		if (!assets::floraRectWithinLimits(rect)) {
			return {
				.status = 400,
				.body = R"({"error":"flora: rect sides must be above 0 and at most 500 m, the rect at most 25 million 0.1 m cells, and every coordinate within 16777216 m of the origin"})"
			};
		}

		gather({rect.minX - kCrownReachMeters, rect.minY - kCrownReachMeters, rect.maxX + kCrownReachMeters, rect.maxY + kCrownReachMeters});
		describeDefs();
		const assets::FloraMeasurement m =
			assets::measureFlora(rect, loadedAreas, entities, traits, [&registry](const assets::PlacedEntity& tree) {
				const assets::AssetSilhouette* sil = registry.getSilhouette(tree.templateId);
				return sil != nullptr && sil->valid ? &sil->rings : nullptr;
			});

		std::ostringstream json;
		json << std::fixed << std::setprecision(2) << "{\"rect\":[" << m.rect.minX << "," << m.rect.minY << "," << m.rect.maxX << ","
			 << m.rect.maxY << "],\"rectAreaM2\":" << m.rectAreaM2 << ",\"measuredAreaM2\":" << m.measuredAreaM2
			 << ",\"cellSizeM\":" << assets::kFloraCellSizeMeters << ",\"trees\":" << m.treeCount
			 << ",\"treesPer1000m2\":" << m.treesPerThousandM2 << ",\"canopyCover\":" << std::setprecision(4) << m.canopyCover
			 << std::setprecision(2) << ",\"woodPer1000m2\":" << m.woodPerThousandM2 << ",\"defs\":[";
		for (size_t i = 0; i < m.defs.size(); ++i) {
			const assets::FloraDefMeasure& d = m.defs[i];
			json << (i == 0 ? "" : ",") << "{\"def\":\"" << jsonEscape(d.defName) << "\",\"count\":" << d.count
				 << ",\"per1000m2\":" << d.perThousandM2 << ",\"woodPerTree\":" << d.woodPerTree
				 << ",\"woodPer1000m2\":" << d.woodPerThousandM2 << ",\"ageClasses\":[";
			for (size_t k = 0; k < d.ageClasses.size(); ++k) {
				const assets::FloraAgeMeasure& a = d.ageClasses[k];
				json << (k == 0 ? "" : ",") << "{\"name\":\"" << jsonEscape(a.name) << "\",\"count\":" << a.count
					 << ",\"per1000m2\":" << a.perThousandM2 << "}";
			}
			json << "]}";
		}
		json << "],\"otherTallOccluders\":[";
		for (size_t i = 0; i < m.otherTallOccluders.size(); ++i) {
			const assets::FloraDefMeasure& d = m.otherTallOccluders[i];
			json << (i == 0 ? "" : ",") << "{\"def\":\"" << jsonEscape(d.defName) << "\",\"count\":" << d.count
				 << ",\"per1000m2\":" << d.perThousandM2 << "}";
		}
		json << "]}";
		return {.body = json.str()};
	}

	void DevCommandHandler::devWalls(const app::DevRequest& cmd) {
		std::vector<Foundation::Vec2> pts = parsePointList(param(cmd, "pts"));
		if (pts.size() < 2) {
			LOG_WARNING(Game, "[DevAPI] walls: pts needs >= 2 'x,y' pairs (got %zu)", pts.size());
			return;
		}
		const std::string material = param(cmd, "material", "Wood");
		const std::string thickness = param(cmd, "thickness", "Standard");
		// strtoull, not strtoul: FoundationId is 64-bit, and unsigned long is only 32-bit on Windows.
		const auto host = static_cast<construction::FoundationId>(std::strtoull(param(cmd, "host", "0").c_str(), nullptr, 10));

		const std::string builtStr = param(cmd, "built", "1");
		const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");
		const std::string closeStr = param(cmd, "close", "1");
		const bool		  close = (closeStr == "1" || closeStr == "on" || closeStr == "true" || closeStr == "yes");
		if (close && pts.size() >= 3) {
			pts.push_back(pts.front()); // close the loop so the chain encloses
		}

		// The WHOLE chain must lie on buildable land: every segment's endpoints AND its
		// centerline samples. A segment that bridges water between two on-land points refuses
		// the whole placement (stamp nothing). Checked AFTER the close-loop append so the
		// closing segment (last->first) is validated too.
		if (!requireWalkableChain(pts, "walls")) {
			return;
		}

		const int n = m_ctx.drawing->devCommitWalls(pts, material, thickness, host, built);
		LOG_INFO(
			Game,
			"[DevAPI] walls: committed %d segment(s) (%s/%s, built=%d, host=%llu)",
			n,
			material.c_str(),
			thickness.c_str(),
			built ? 1 : 0,
			static_cast<unsigned long long>(host)
		);
		m_ctx.ui->pushNotification("Dev", std::to_string(n) + (built ? " built wall(s)" : " wall blueprint(s)"), ui::ToastSeverity::Info);
	}

	void DevCommandHandler::devOpening(const app::DevRequest& cmd) {
		const std::string type = param(cmd, "type", "Door");
		const auto*		  typeDef = assets::ConstructionRegistry::Get().getOpeningType(type);
		if (typeDef == nullptr) {
			LOG_WARNING(Game, "[DevAPI] opening: unknown type '%s'", type.c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown opening type", ui::ToastSeverity::Warning);
			return;
		}

		auto& constructionWorld = m_ctx.drawing->world();

		// Resolve the target segment + centerline t: seg= names it directly (t defaults to
		// center), pt= snaps to the nearest built wall (t from the snap).
		construction::SegmentId segment = construction::kInvalidSegment;
		float							t = 0.5F;
		if (hasParam(cmd, "seg")) {
			segment = static_cast<construction::SegmentId>(std::strtoull(param(cmd, "seg").c_str(), nullptr, 10));
		} else if (hasParam(cmd, "pt")) {
			const std::vector<Foundation::Vec2> pts = parsePointList(param(cmd, "pt"));
			if (pts.empty()) {
				LOG_WARNING(Game, "[DevAPI] opening: pt must be 'x,y' world meters");
				return;
			}
			const auto&							   registry = assets::ConstructionRegistry::Get();
			const construction::SnapEngine snap(registry.snapping(), constructionWorld);
			const auto							   os = snap.snapOpening(pts.front(), typeDef->widthMeters);
			if (!os.valid) {
				LOG_WARNING(Game, "[DevAPI] opening: no built wall near pt");
				m_ctx.ui->pushNotification("Dev", "No built wall near point", ui::ToastSeverity::Warning);
				return;
			}
			segment = os.segment;
			t = os.t;
		} else {
			LOG_WARNING(Game, "[DevAPI] opening: needs seg=<id> or pt=x,y");
			return;
		}

		// Explicit t= overrides the resolved parameter (addOpening clamps to [0,1]).
		if (hasParam(cmd, "t")) {
			t = std::strtof(param(cmd, "t").c_str(), nullptr);
		}

		const std::string builtStr = param(cmd, "built", "1");
		const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");

		const construction::OpeningId id = m_ctx.drawing->devCommitOpening(segment, t, type, built);
		if (id == construction::kInvalidOpening) {
			LOG_WARNING(
				Game,
				"[DevAPI] opening: commit rejected (segment #%llu, type %s)",
				static_cast<unsigned long long>(segment),
				type.c_str()
			);
			m_ctx.ui->pushNotification("Dev", "Opening commit rejected", ui::ToastSeverity::Warning);
			return;
		}

		LOG_INFO(
			Game,
			"[DevAPI] opening #%llu spawned (%s on segment #%llu at t=%.2f, built=%d)",
			static_cast<unsigned long long>(id),
			type.c_str(),
			static_cast<unsigned long long>(segment),
			static_cast<double>(t),
			built ? 1 : 0
		);
		m_ctx.ui->pushNotification("Dev", built ? "Built opening placed" : "Opening blueprint placed", ui::ToastSeverity::Info);
	}

	void DevCommandHandler::devCraft(const app::DevRequest& cmd) {
		const std::string recipe = param(cmd, "recipe", "Recipe_AxePrimitive");
		const long		  qtyRaw = std::strtol(param(cmd, "n", "1").c_str(), nullptr, 10);
		const auto		  qty = static_cast<uint32_t>(qtyRaw < 1 ? 1 : qtyRaw);

		if (assets::RecipeRegistry::Get().getRecipe(recipe) == nullptr) {
			LOG_WARNING(Game, "[DevAPI] craft: unknown recipe '%s'", recipe.c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown recipe: " + recipe, ui::ToastSeverity::Warning);
			return;
		}

		// Queue the job at the nearest crafting station (entity with a WorkQueue) to `at`.
		const Foundation::Vec2 at = parsePoint(param(cmd, "at"));
		ecs::WorkQueue*		   best = nullptr;
		float				   bestDistSq = 0.0F;
		ecs::EntityID		   bestEntity = ecs::kInvalidEntity;
		for (auto [entity, workQueue, pos] : m_ctx.world->view<ecs::WorkQueue, ecs::Position>()) {
			const float dx = pos.value.x - at.x;
			const float dy = pos.value.y - at.y;
			const float distSq = dx * dx + dy * dy;
			if (best == nullptr || distSq < bestDistSq) {
				best = &workQueue;
				bestDistSq = distSq;
				bestEntity = entity;
			}
		}
		if (best == nullptr) {
			LOG_WARNING(Game, "[DevAPI] craft: no crafting station found (spawn a CraftingSpot first)");
			m_ctx.ui->pushNotification("Dev", "No crafting station to queue at", ui::ToastSeverity::Warning);
			return;
		}

		best->addJob(recipe, qty);
		LOG_INFO(
			Game, "[DevAPI] craft: queued %u x %s at station #%llu", qty, recipe.c_str(), static_cast<unsigned long long>(bestEntity)
		);
		m_ctx.ui->pushNotification("Dev", "Queued " + std::to_string(qty) + " " + recipe, ui::ToastSeverity::Info);
	}

	app::DevResponse DevCommandHandler::devStorage(const app::DevRequest& cmd) {
		const auto reject = [this](const std::string& message) {
			LOG_WARNING(Game, "[DevAPI] storage: %s", message.c_str());
			m_ctx.ui->pushNotification("Dev", "Storage: " + message, ui::ToastSeverity::Warning);
			return app::DevResponse{.status = 400, .body = "{\"error\":\"storage: " + jsonEscape(message) + "\"}"};
		};
		const Foundation::Vec2 at = parsePoint(param(cmd, "at"));
		const ecs::EntityID	   id = nearestStorageEntity(at);
		if (id == ecs::kInvalidEntity) {
			return reject("no storage entity near the point");
		}
		auto* storageConfig = m_ctx.world->getComponent<ecs::StorageConfiguration>(id);
		if (storageConfig == nullptr) {
			return reject("entity #" + std::to_string(id) + " has no StorageConfiguration");
		}

		const std::string	 defName = param(cmd, "item", "*");
		const std::string	 categoryStr = param(cmd, "category");
		assets::ItemCategory category = assets::ItemCategory::RawMaterial;
		if (equalsIgnoreCase(categoryStr, "Food")) {
			category = assets::ItemCategory::Food;
		} else if (equalsIgnoreCase(categoryStr, "Tool")) {
			category = assets::ItemCategory::Tool;
		} else if (equalsIgnoreCase(categoryStr, "Furniture")) {
			category = assets::ItemCategory::Furniture;
		} else if (equalsIgnoreCase(categoryStr, "None")) {
			category = assets::ItemCategory::None;
		}
		// A named item's rule is in the item's own category, or it never matches it.
		if (defName != "*") {
			const auto* def = assets::AssetRegistry::Get().getDefinition(defName);
			if (def == nullptr) {
				return reject("unknown item '" + defName + "'");
			}
			if (!categoryStr.empty() && category != def->category) {
				return reject("item '" + defName + "' is not in category " + categoryStr);
			}
			category = def->category;
		}

		ecs::StoragePriority priority = ecs::StoragePriority::Medium;
		const std::string	 priorityStr = param(cmd, "priority", "Medium");
		if (equalsIgnoreCase(priorityStr, "Low")) {
			priority = ecs::StoragePriority::Low;
		} else if (equalsIgnoreCase(priorityStr, "High")) {
			priority = ecs::StoragePriority::High;
		} else if (equalsIgnoreCase(priorityStr, "Critical")) {
			priority = ecs::StoragePriority::Critical;
		}

		const auto minAmount = static_cast<uint32_t>(std::strtol(param(cmd, "min", "0").c_str(), nullptr, 10));
		const auto maxAmount = static_cast<uint32_t>(std::strtol(param(cmd, "max", "0").c_str(), nullptr, 10));

		// As the storage dialog does: a rule naming an item replaces its category's wildcard.
		if (defName != "*") {
			std::erase_if(storageConfig->rules, [&](const ecs::StorageRule& existing) { return existing.isWildcard() && existing.category == category; });
		}

		ecs::StorageRule rule;
		rule.defName = defName;
		rule.category = category;
		rule.priority = priority;
		rule.minAmount = minAmount;
		rule.maxAmount = maxAmount;
		storageConfig->addRule(std::move(rule));

		LOG_INFO(
			Game,
			"[DevAPI] storage: added rule to #%llu (item=%s, category=%u, priority=%s, min=%u, max=%u)",
			static_cast<unsigned long long>(id),
			defName.c_str(),
			static_cast<unsigned>(category),
			priorityStr.c_str(),
			minAmount,
			maxAmount
		);
		m_ctx.ui->pushNotification("Dev", "Added storage rule: " + defName, ui::ToastSeverity::Info);
		return app::DevResponse{.body = R"({"status":"ok","verb":"storage"})"};
	}

	Foundation::Vec2 DevCommandHandler::parsePoint(const std::string& spec) {
		const std::vector<Foundation::Vec2> pts = parsePointList(spec);
		return pts.empty() ? Foundation::Vec2{0.0F, 0.0F} : pts.front();
	}

	Foundation::Vec2 DevCommandHandler::spreadOffset(long i, long n, float radius) {
		if (radius <= 0.0F || n <= 1) {
			return {0.0F, 0.0F};
		}
		const float t = (static_cast<float>(i) + 0.5F) / static_cast<float>(n);
		const float r = radius * std::sqrt(t);
		const float angle = static_cast<float>(i) * 2.399963229F; // golden angle (radians)
		return {r * std::cos(angle), r * std::sin(angle)};
	}

	std::vector<Foundation::Vec2> DevCommandHandler::parsePointList(const std::string& spec) {
		std::vector<Foundation::Vec2> pts;
		std::stringstream			  ss(spec);
		std::string					  pair;
		while (std::getline(ss, pair, ';')) {
			const auto comma = pair.find(',');
			if (comma == std::string::npos) {
				continue;
			}
			const std::string xs = pair.substr(0, comma);
			const std::string ys = pair.substr(comma + 1);
			char*			  endX = nullptr;
			char*			  endY = nullptr;
			const float		  x = std::strtof(xs.c_str(), &endX);
			const float		  y = std::strtof(ys.c_str(), &endY);
			// Skip the pair unless both parses consumed at least one character; strtof returns 0.0
			// on a non-numeric string, which would otherwise silently inject a (0,0) vertex.
			if (endX == xs.c_str() || endY == ys.c_str()) {
				continue;
			}
			pts.emplace_back(x, y);
		}
		return pts;
	}

	// ===================================================================== STATE READBACK

	void DevCommandHandler::serializeColonists(std::ostringstream& out) {
		out << "{\"colonists\":[";
		bool first = true;
		for (auto [entity, colonist, pos, needs] : m_ctx.world->view<ecs::Colonist, ecs::Position, ecs::NeedsComponent>()) {
			out << (first ? "" : ",");
			first = false;
			out << "{\"id\":" << static_cast<unsigned long long>(entity) << ",\"name\":\"" << jsonEscape(colonist.name) << "\""
				<< ",\"x\":" << pos.value.x << ",\"y\":" << pos.value.y << ",\"needs\":{";
			for (size_t i = 0; i < ecs::kNeedLabels.size(); ++i) {
				out << (i > 0 ? "," : "") << "\"" << ecs::kNeedLabels[i] << "\":" << needs.get(static_cast<ecs::NeedType>(i)).value;
			}
			out << "}";
			if (const auto* action = m_ctx.world->getComponent<ecs::Action>(entity)) {
				out << ",\"action\":\"" << ecs::actionTypeName(action->type) << "\"";
			}
			if (const auto* inv = m_ctx.world->getComponent<ecs::Inventory>(entity)) {
				// Aggregate hands + backpack so each defName appears once (no duplicate keys).
				std::unordered_map<std::string, uint32_t> totals;
				auto add = [&](const std::optional<ecs::ItemStack>& hand) {
					if (hand.has_value()) {
						totals[hand->defName] += hand->quantity;
					}
				};
				add(inv->leftHand);
				// Two-handed items mirror in both hands; don't double-count.
				if (!(inv->leftHand.has_value() && inv->rightHand.has_value() && inv->leftHand->defName == inv->rightHand->defName)) {
					add(inv->rightHand);
				}
				for (const auto& stack : inv->items) {
					totals[stack.defName] += stack.quantity;
				}
				out << ",\"inventory\":{";
				bool firstItem = true;
				for (const auto& [defName, qty] : totals) {
					out << (firstItem ? "" : ",") << "\"" << jsonEscape(defName) << "\":" << qty;
					firstItem = false;
				}
				out << "}";

				// Hands and belt, broken out explicitly so the carry surfaces (armful vs belted tool)
				// are visible -- the aggregate above folds hands into the same map as the backpack, so
				// it can't tell a held axe from a stowed one. A two-hand armful mirrors both hands; show
				// the single logical stack.
				auto handJson = [&](const std::optional<ecs::ItemStack>& hand) {
					if (hand.has_value()) {
						out << "{\"" << jsonEscape(hand->defName) << "\":" << hand->quantity << "}";
					} else {
						out << "null";
					}
				};
				out << ",\"hands\":{\"left\":";
				handJson(inv->leftHand);
				out << ",\"right\":";
				handJson(inv->rightHand);
				out << "},\"belt\":[";
				bool firstBelt = true;
				for (const auto& slot : inv->belt) {
					if (slot.has_value()) {
						out << (firstBelt ? "" : ",") << "\"" << jsonEscape(slot->defName) << "\"";
						firstBelt = false;
					}
				}
				out << "]";

				out << ",\"cargoKg\":" << ecs::carriedCargoMassKg(*inv, assets::AssetRegistry::Get())
					<< ",\"carryCapacityKg\":" << inv->carryCapacityKg;
			}
			out << "}";
		}
		out << "]}";
	}

	void DevCommandHandler::serializeConstruction(std::ostringstream& out) {
		// Emit a StructureBlueprint's required/delivered material maps + progress (shared by the
		// foundation and wall serializers, which carry the same material bookkeeping).
		auto emitMaterialProgress = [](std::ostringstream& os, const ecs::StructureBlueprint& bp) {
			os << ",\"required\":{";
			bool firstReq = true;
			for (const auto& [defName, qty] : bp.required) {
				os << (firstReq ? "" : ",") << "\"" << jsonEscape(defName) << "\":" << qty;
				firstReq = false;
			}
			os << "},\"delivered\":{";
			bool firstDel = true;
			for (const auto& [defName, qty] : bp.delivered) {
				os << (firstDel ? "" : ",") << "\"" << jsonEscape(defName) << "\":" << qty;
				firstDel = false;
			}
			os << "},\"progress\":" << bp.progress();
		};

		const auto& cworld = m_ctx.drawing->world();
		out << "{\"foundations\":[";
		bool first = true;
		for (const auto& foundation : cworld.foundations()) {
			out << (first ? "" : ",");
			first = false;
			out << "{\"id\":" << static_cast<unsigned long long>(foundation.id) << ",\"material\":\"" << jsonEscape(foundation.material)
				<< "\",\"state\":\"" << (foundation.state == construction::FoundationState::Built ? "Built" : "Blueprint")
				<< "\",\"entity\":" << static_cast<unsigned long long>(foundation.entity) << ",\"area\":" << cworld.areaSquareMeters(foundation.id);

			// Include material progress from the ECS mirror entity's StructureBlueprint
			if (foundation.entity != ecs::kInvalidEntity) {
				if (const auto* bp = m_ctx.world->getComponent<ecs::StructureBlueprint>(foundation.entity)) {
					const char* phaseStr = "Clearing";
					switch (bp->phase) {
						case ecs::StructureBlueprint::BuildPhase::Clearing: phaseStr = "Clearing"; break;
						case ecs::StructureBlueprint::BuildPhase::AwaitingMaterials: phaseStr = "AwaitingMaterials"; break;
						case ecs::StructureBlueprint::BuildPhase::UnderConstruction: phaseStr = "UnderConstruction"; break;
						case ecs::StructureBlueprint::BuildPhase::Complete: phaseStr = "Complete"; break;
					}
					out << ",\"phase\":\"" << phaseStr << "\"";
					emitMaterialProgress(out, *bp);
				}
			}

			out << "}";
		}

		// Wall segments with build state, material, endpoints (mm -> meters), and material progress
		out << "],\"walls\":[";
		bool firstWall = true;
		for (const auto& seg : cworld.segments()) {
			out << (firstWall ? "" : ",");
			firstWall = false;

			const auto* v0 = cworld.getVertex(seg.v0);
			const auto* v1 = cworld.getVertex(seg.v1);
			out << "{\"id\":" << static_cast<unsigned long long>(seg.id) << ",\"material\":\"" << jsonEscape(seg.material) << "\",\"state\":\""
				<< (seg.state == construction::FoundationState::Built ? "Built" : "Blueprint") << "\",\"entity\":"
				<< static_cast<unsigned long long>(seg.entity);
			if (v0 != nullptr) {
				out << ",\"x0\":" << (static_cast<double>(v0->pos.x) / 1000.0) << ",\"y0\":" << (static_cast<double>(v0->pos.y) / 1000.0);
			}
			if (v1 != nullptr) {
				out << ",\"x1\":" << (static_cast<double>(v1->pos.x) / 1000.0) << ",\"y1\":" << (static_cast<double>(v1->pos.y) / 1000.0);
			}

			// Material progress from ECS mirror entity's StructureBlueprint
			if (seg.entity != ecs::kInvalidEntity) {
				if (const auto* bp = m_ctx.world->getComponent<ecs::StructureBlueprint>(seg.entity)) {
					emitMaterialProgress(out, *bp);
				}
			}

			out << "}";
		}
		out << "]}";
	}

	void DevCommandHandler::serializeStations(std::ostringstream& out) {
		out << "{\"stations\":[";
		bool first = true;
		for (auto [entity, workQueue, pos] : m_ctx.world->view<ecs::WorkQueue, ecs::Position>()) {
			out << (first ? "" : ",");
			first = false;
			out << "{\"id\":" << static_cast<unsigned long long>(entity) << ",\"x\":" << pos.value.x << ",\"y\":" << pos.value.y;

			out << ",\"jobs\":[";
			bool firstJob = true;
			for (const auto& job : workQueue.jobs) {
				out << (firstJob ? "" : ",") << "{\"recipe\":\"" << jsonEscape(job.recipeDefName) << "\",\"completed\":" << job.completed
					<< ",\"quantity\":" << job.quantity << "}";
				firstJob = false;
			}
			out << "]";

			// Material store: what the station physically holds for the queued recipe.
			if (const auto* inv = m_ctx.world->getComponent<ecs::Inventory>(entity)) {
				out << ",\"store\":{";
				bool firstItem = true;
				for (const auto& stack : inv->items) {
					out << (firstItem ? "" : ",") << "\"" << jsonEscape(stack.defName) << "\":" << stack.quantity;
					firstItem = false;
				}
				out << "}";
			}
			out << "}";
		}
		out << "]}";
	}

	void DevCommandHandler::serializeStorage(std::ostringstream& out) {
		out << "{\"storages\":[";
		bool first = true;
		for (auto [entity, storageConfig, pos, inventory] : m_ctx.world->view<ecs::StorageConfiguration, ecs::Position, ecs::Inventory>()) {
			out << (first ? "" : ",");
			first = false;

			// Attempt to look up the defName from an Appearance or Structure component;
			// fall back to empty string if neither is present.
			std::string defName;
			if (const auto* appearance = m_ctx.world->getComponent<ecs::Appearance>(entity)) {
				defName = appearance->defName;
			}

			out << "{\"id\":" << static_cast<unsigned long long>(entity) << ",\"defName\":\"" << jsonEscape(defName) << "\",\"x\":"
				<< pos.value.x << ",\"y\":" << pos.value.y;

			// Inventory contents (backpack only; storage containers don't use hand slots)
			out << ",\"inventory\":{";
			bool firstItem = true;
			for (const auto& stack : inventory.getAllItems()) {
				out << (firstItem ? "" : ",") << "\"" << jsonEscape(stack.defName) << "\":" << stack.quantity;
				firstItem = false;
			}
			out << "},\"slots\":" << inventory.getSlotCount() << ",\"maxSlots\":" << inventory.maxCapacity;

			// Storage rules
			out << ",\"rules\":[";
			bool firstRule = true;
			for (const auto& rule : storageConfig.rules) {
				out << (firstRule ? "" : ",");
				firstRule = false;
				out << "{\"item\":\"" << jsonEscape(rule.defName) << "\",\"category\":\"";
				switch (rule.category) {
					case assets::ItemCategory::None: out << "None"; break;
					case assets::ItemCategory::RawMaterial: out << "RawMaterial"; break;
					case assets::ItemCategory::Food: out << "Food"; break;
					case assets::ItemCategory::Tool: out << "Tool"; break;
					case assets::ItemCategory::Furniture: out << "Furniture"; break;
					default: out << "Unknown"; break;
				}
				out << "\",\"priority\":\"" << ecs::storagePriorityToString(rule.priority) << "\",\"min\":" << rule.minAmount
					<< ",\"max\":" << rule.maxAmount << "}";
			}
			out << "]}";
		}
		out << "]}";
	}

	void DevCommandHandler::serializeTime(std::ostringstream& out) {
		const auto snap = m_ctx.world->getSystem<ecs::TimeSystem>().snapshot();
		out << "{\"day\":" << snap.day << ",\"season\":\"" << ecs::seasonName(snap.season) << "\",\"timeOfDay\":" << snap.timeOfDay
			<< ",\"speed\":" << static_cast<int>(snap.speed) << ",\"paused\":" << (snap.isPaused ? "true" : "false") << "}";
	}

	void DevCommandHandler::serializeLanding(std::ostringstream& out) {
		out << "{\"landingLatDeg\":" << m_ctx.landingLatDeg
		    << ",\"landingLonDeg\":" << m_ctx.landingLonDeg;

		if (!m_ctx.planet || !m_ctx.planet->grid) {
			// No planet available (test/fallback session): return coords only.
			out << ",\"biome\":null,\"waterClass\":null,\"riverInTile\":null}";
			return;
		}

		const worldgen::GeneratedWorld& planet = *m_ctx.planet;
		const worldgen::TileId tile = planet.grid->fromLatLon(m_ctx.landingLatDeg, m_ctx.landingLonDeg);

		// Biome at the landing tile.
		const bool haveBiome = (planet.validFields & static_cast<uint32_t>(worldgen::WorldField::Biome)) != 0;
		if (haveBiome && tile != worldgen::kInvalidTile && tile < planet.data.biome.size()) {
			const worldgen::Biome b = static_cast<worldgen::Biome>(planet.data.biome[tile]);
			out << ",\"biome\":\"" << worldgen::biomeToString(b) << "\"";
		} else {
			out << ",\"biome\":null";
		}

		// Water classification using the same logic as the landing-site scorer.
		const bool haveFlags = (planet.validFields & static_cast<uint32_t>(worldgen::WorldField::Flags)) != 0;
		if (haveFlags && tile != worldgen::kInvalidTile && tile < planet.data.flags.size()) {
			const worldgen::WaterClass wc = worldgen::classifyWater(planet, tile);
			out << ",\"waterClass\":\"" << worldgen::waterClassToString(wc) << "\"";

			// River-through-tile is the "best" water signal: the landing origin IS the
			// channel center, which means a riverbank is within a few meters of spawn.
			const bool riverInTile = (planet.data.flags[tile] & worldgen::kFlagRiver) != 0;
			out << ",\"riverInTile\":" << (riverInTile ? "true" : "false");
		} else {
			out << ",\"waterClass\":null,\"riverInTile\":null";
		}

		out << "}";
	}

	void DevCommandHandler::serializeSummary(std::ostringstream& out) {
		size_t colonists = 0;
		for (auto colonistRow : m_ctx.world->view<ecs::Colonist>()) {
			(void)colonistRow;
			++colonists;
		}
		out << "{\"colonists\":" << colonists << ",\"foundations\":" << m_ctx.drawing->world().foundations().size()
			<< ",\"chunks\":" << m_ctx.chunks->loadedChunkCount() << "}";
	}

	std::string DevCommandHandler::jsonEscape(const std::string& s) {
		std::string out;
		out.reserve(s.size() + 8);
		for (char c : s) {
			switch (c) {
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\b': out += "\\b"; break;
				case '\f': out += "\\f"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) {
						// Other control chars: emit a \u00XX escape rather than dropping them.
						char buf[7];
						std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
						out += buf;
					} else {
						out += c;
					}
			}
		}
		return out;
	}

} // namespace world_sim
