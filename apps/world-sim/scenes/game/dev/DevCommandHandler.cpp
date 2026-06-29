#include "DevCommandHandler.h"

#include "scenes/game/ui/GameUI.h"
#include "scenes/game/world/construction/DrawingSystem.h"
#include "scenes/game/world/placement/PlacementSystem.h"
#include "scenes/game/world/selection/SelectionSystem.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <assets/ConstructionRegistry.h>

#include <components/toast/Toast.h> // UI::ToastSeverity

#include <construction/ConstructionWorld.h>
#include <construction/SnapEngine.h>

#include <debug/DebugServer.h> // Foundation::DevCommand

#include <ecs/InventoryMass.h>
#include <ecs/World.h>
#include <ecs/components/Action.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Packaged.h>
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

#include <world/chunk/ChunkManager.h>

#include <worldgen/data/Biome.h>
#include <worldgen/data/GeneratedWorld.h>
#include <worldgen/sampling/LandingSite.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace world_sim {

	void DevCommandHandler::handle(const Foundation::DevCommand& cmd) {
		if (cmd.verb == "freebuild" || cmd.verb == "construction") {
			devFreeBuild(cmd);
		} else if (cmd.verb == "give") {
			devGive(cmd);
		} else if (cmd.verb == "spawn") {
			devSpawn(cmd);
		} else if (cmd.verb == "colonist") {
			devColonist(cmd);
		} else if (cmd.verb == "need") {
			devNeed(cmd);
		} else if (cmd.verb == "time") {
			devTime(cmd);
		} else if (cmd.verb == "teleport") {
			devTeleport(cmd);
		} else if (cmd.verb == "select") {
			devSelect(cmd);
		} else if (cmd.verb == "kill") {
			devKill(cmd);
		} else if (cmd.verb == "complete") {
			devComplete(cmd);
		} else if (cmd.verb == "foundation") {
			devFoundation(cmd);
		} else if (cmd.verb == "walls") {
			devWalls(cmd);
		} else if (cmd.verb == "opening") {
			devOpening(cmd);
		} else if (cmd.verb == "craft") {
			devCraft(cmd);
		} else if (cmd.verb == "storage") {
			devStorage(cmd);
		} else {
			LOG_WARNING(Game, "[DevAPI] Unknown dev command verb '%s'", cmd.verb.c_str());
		}
	}

	void DevCommandHandler::devFreeBuild(const Foundation::DevCommand& cmd) {
		// Accept on= (freebuild verb) or freebuild= (construction verb); truthy on 1/on/true/yes.
		std::string value = cmd.hasParam("on") ? cmd.param("on") : cmd.param("freebuild", "1");
		const bool	on = (value == "1" || value == "on" || value == "true" || value == "yes");
		m_ctx.world->getSystem<ecs::ConstructionSystem>().setFreeBuild(on);
		LOG_INFO(Game, "[DevAPI] free-build %s", on ? "ON" : "OFF");
		m_ctx.ui->pushNotification("Dev", on ? "Free-build ON" : "Free-build OFF", UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devGive(const Foundation::DevCommand& cmd) {
		const std::string material = cmd.param("material", "Wood");
		const long		  nRaw = std::strtol(cmd.param("n", "100").c_str(), nullptr, 10);
		if (nRaw <= 0) {
			LOG_WARNING(Game, "[DevAPI] give: n must be > 0");
			return;
		}
		const auto		  n = static_cast<uint32_t>(nRaw);
		const std::string where = cmd.param("where", "site");

		if (where == "site") {
			const uint32_t credited = m_ctx.world->getSystem<ecs::ConstructionSystem>().creditMaterialToSites(material, n);
			LOG_INFO(Game, "[DevAPI] give: credited %u %s across build sites", credited, material.c_str());
			m_ctx.ui->pushNotification("Dev", "Credited " + std::to_string(credited) + " " + material + " to sites", UI::ToastSeverity::Info);
			return;
		}

		if (where == "loose") {
			if (engine::assets::AssetRegistry::Get().getDefinition(material) == nullptr) {
				LOG_WARNING(Game, "[DevAPI] give loose: unknown asset '%s'", material.c_str());
				m_ctx.ui->pushNotification("Dev", "Unknown asset: " + material, UI::ToastSeverity::Warning);
				return;
			}
			const Foundation::Vec2 at = parsePoint(cmd.param("at"));
			if (!requireValidPosition(at, "give loose")) {
				return;
			}
			// Each packaged item is its own entity (no ground stacks); cap so a big N can't spawn thousands.
			constexpr uint32_t kMaxLoose = 50;
			const uint32_t	   count = n < kMaxLoose ? n : kMaxLoose;
			for (uint32_t i = 0; i < count; ++i) {
				const Foundation::Vec2 off = spreadOffset(static_cast<long>(i), static_cast<long>(count), 1.0F);
				const ecs::EntityID	   entity = m_ctx.placement->spawnEntity(material, {at.x + off.x, at.y + off.y});
				m_ctx.world->addComponent<ecs::Packaged>(entity, ecs::Packaged{});
			}
			if (n > kMaxLoose) {
				LOG_WARNING(Game, "[DevAPI] give loose: capped %u -> %u entities", n, kMaxLoose);
			}
			LOG_INFO(Game, "[DevAPI] give: dropped %u loose '%s' near (%.1f, %.1f)", count, material.c_str(), at.x, at.y);
			m_ctx.ui->pushNotification("Dev", "Dropped " + std::to_string(count) + " " + material, UI::ToastSeverity::Info);
			return;
		}

		if (where == "colonist" || where == "storage") {
			const Foundation::Vec2 at = parsePoint(cmd.param("at"));
			ecs::Inventory*		   inventory = (where == "colonist") ? nearestColonistInventory(at) : nearestStorageInventory(at);
			if (inventory == nullptr) {
				LOG_WARNING(Game, "[DevAPI] give %s: no target found", where.c_str());
				m_ctx.ui->pushNotification("Dev", "No " + where + " to give to", UI::ToastSeverity::Warning);
				return;
			}
			const uint32_t added = inventory->addItem(material, n);
			LOG_INFO(Game, "[DevAPI] give: added %u/%u %s to nearest %s", added, n, material.c_str(), where.c_str());
			m_ctx.ui->pushNotification("Dev", "Gave " + std::to_string(added) + " " + material + " to " + where, UI::ToastSeverity::Info);
			return;
		}

		LOG_WARNING(Game, "[DevAPI] give: unknown where=%s (site|loose|colonist|storage)", where.c_str());
		m_ctx.ui->pushNotification("Dev", "Unknown where: " + where, UI::ToastSeverity::Warning);
	}

	void DevCommandHandler::devSpawn(const Foundation::DevCommand& cmd) {
		const std::string def = cmd.param("def");
		if (def.empty()) {
			LOG_WARNING(Game, "[DevAPI] spawn: missing def=<assetName>");
			m_ctx.ui->pushNotification("Dev", "spawn needs def=", UI::ToastSeverity::Warning);
			return;
		}
		if (engine::assets::AssetRegistry::Get().getDefinition(def) == nullptr) {
			LOG_WARNING(Game, "[DevAPI] spawn: unknown def '%s'", def.c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown asset: " + def, UI::ToastSeverity::Warning);
			return;
		}
		const Foundation::Vec2 at = parsePoint(cmd.param("at"));
		if (!requireValidPosition(at, "spawn")) {
			return;
		}
		const long			   nRaw = std::strtol(cmd.param("n", "1").c_str(), nullptr, 10);
		const long			   n = nRaw < 1 ? 1 : nRaw;
		const float			   scatter = std::strtof(cmd.param("scatter", "0").c_str(), nullptr);

		for (long i = 0; i < n; ++i) {
			const Foundation::Vec2 off = spreadOffset(i, n, scatter);
			m_ctx.placement->spawnEntity(def, {at.x + off.x, at.y + off.y});
		}
		LOG_INFO(Game, "[DevAPI] spawn: %ld x '%s' near (%.1f, %.1f)", n, def.c_str(), at.x, at.y);
		m_ctx.ui->pushNotification("Dev", "Spawned " + std::to_string(n) + " " + def, UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devColonist(const Foundation::DevCommand& cmd) {
		const Foundation::Vec2 at = parsePoint(cmd.param("at"));
		if (!requireValidPosition(at, "colonist")) {
			return;
		}
		const long			   nRaw = std::strtol(cmd.param("n", "1").c_str(), nullptr, 10);
		const long			   n = nRaw < 1 ? 1 : nRaw;
		const std::string	   baseName = cmd.param("name", "Dev");

		for (long i = 0; i < n; ++i) {
			const Foundation::Vec2 off = spreadOffset(i, n, n > 1 ? 1.0F : 0.0F);
			const std::string	   name = (n == 1) ? baseName : baseName + std::to_string(i + 1);
			m_ctx.spawnColonist({at.x + off.x, at.y + off.y}, name);
		}
		LOG_INFO(Game, "[DevAPI] colonist: spawned %ld at (%.1f, %.1f)", n, at.x, at.y);
		m_ctx.ui->pushNotification("Dev", "Spawned " + std::to_string(n) + " colonist(s)", UI::ToastSeverity::Info);
	}

	bool DevCommandHandler::requireValidPosition(Foundation::Vec2 at, const char* verb) {
		// Single validity gate: a world position is placeable IFF it is on an active
		// walkable nav face (NavigationSystem::isValidPosition). No terrain/world-source
		// reads here -- runtime walkability is owned by the nav mesh.
		if (m_ctx.navigation != nullptr && m_ctx.navigation->isValidPosition({at.x, at.y})) {
			return true;
		}
		LOG_WARNING(Game, "[DevAPI] %s: error: %.1f,%.1f not on an active walkable nav mesh, refused", verb, at.x, at.y);
		m_ctx.ui->pushNotification(
			"Dev", "Refused: (" + std::to_string(at.x) + ", " + std::to_string(at.y) + ") not on walkable nav mesh", UI::ToastSeverity::Warning
		);
		return false;
	}

	bool DevCommandHandler::requireWalkableArea(const std::vector<Foundation::Vec2>& pts, const char* verb) {
		// The WHOLE footprint must be on walkable mesh, not just the vertices. Without a nav
		// system wired (headless/test), validity is owned elsewhere; mirror requireValidPosition's
		// permissive fallback.
		if (m_ctx.navigation == nullptr) {
			return true;
		}
		std::vector<glm::vec2> poly;
		poly.reserve(pts.size());
		for (const auto& p : pts) {
			poly.emplace_back(p.x, p.y);
		}
		if (m_ctx.navigation->isAreaWalkable(poly)) {
			return true;
		}
		LOG_WARNING(Game, "[DevAPI] %s: error: footprint not fully on an active walkable nav mesh, refused", verb);
		m_ctx.ui->pushNotification("Dev", "Refused: footprint not fully on walkable ground", UI::ToastSeverity::Warning);
		return false;
	}

	bool DevCommandHandler::requireWalkableChain(const std::vector<Foundation::Vec2>& pts, const char* verb) {
		if (m_ctx.navigation == nullptr) {
			return true;
		}
		std::vector<glm::vec2> chain;
		chain.reserve(pts.size());
		for (const auto& p : pts) {
			chain.emplace_back(p.x, p.y);
		}
		if (m_ctx.navigation->isPolylineWalkable(chain)) {
			return true;
		}
		LOG_WARNING(Game, "[DevAPI] %s: error: chain not fully on an active walkable nav mesh, refused", verb);
		m_ctx.ui->pushNotification("Dev", "Refused: wall chain not fully on walkable ground", UI::ToastSeverity::Warning);
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
		ecs::Inventory* best = nullptr;
		float			bestDistSq = 0.0F;
		for (auto [entity, storage, pos, inventory] : m_ctx.world->view<ecs::StorageConfiguration, ecs::Position, ecs::Inventory>()) {
			const float dx = pos.value.x - at.x;
			const float dy = pos.value.y - at.y;
			const float distSq = dx * dx + dy * dy;
			if (best == nullptr || distSq < bestDistSq) {
				best = &inventory;
				bestDistSq = distSq;
			}
		}
		return best;
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

	void DevCommandHandler::devNeed(const Foundation::DevCommand& cmd) {
		const ecs::EntityID id = parseEntity(cmd.param("colonist"));
		auto*				needs = m_ctx.world->getComponent<ecs::NeedsComponent>(id);
		if (needs == nullptr) {
			LOG_WARNING(Game, "[DevAPI] need: no colonist #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "No such colonist", UI::ToastSeverity::Warning);
			return;
		}
		ecs::NeedType type{};
		if (!parseNeedType(cmd.param("need"), type)) {
			LOG_WARNING(Game, "[DevAPI] need: unknown need '%s'", cmd.param("need").c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown need", UI::ToastSeverity::Warning);
			return;
		}
		float value = std::strtof(cmd.param("value", "100").c_str(), nullptr);
		value = value < 0.0F ? 0.0F : (value > 100.0F ? 100.0F : value);
		needs->get(type).value = value;
		LOG_INFO(Game, "[DevAPI] need: %s=%.1f on #%llu", ecs::needLabel(type), static_cast<double>(value), static_cast<unsigned long long>(id));
		m_ctx.ui->pushNotification("Dev", std::string(ecs::needLabel(type)) + " set", UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devTime(const Foundation::DevCommand& cmd) {
		auto& timeSystem = m_ctx.world->getSystem<ecs::TimeSystem>();
		bool  acted = false;

		if (cmd.hasParam("speed")) {
			const long s = std::strtol(cmd.param("speed").c_str(), nullptr, 10);
			if (s >= 0 && s <= 3) {
				timeSystem.setSpeed(static_cast<ecs::GameSpeed>(s));
				acted = true;
			} else {
				LOG_WARNING(Game, "[DevAPI] time: speed must be 0..3");
			}
		}
		if (cmd.hasParam("set")) {
			timeSystem.setTimeOfDay(parseClock(cmd.param("set")));
			acted = true;
		}
		if (cmd.hasParam("skip")) {
			timeSystem.skipTime(parseDuration(cmd.param("skip")));
			acted = true;
		}

		const auto snap = timeSystem.snapshot();
		LOG_INFO(Game, "[DevAPI] time: day %d, %.2fh, speed %d", snap.day, static_cast<double>(snap.timeOfDay), static_cast<int>(snap.speed));
		m_ctx.ui->pushNotification("Dev", acted ? "Time updated" : "time: speed|set|skip", acted ? UI::ToastSeverity::Info : UI::ToastSeverity::Warning);
	}

	void DevCommandHandler::devTeleport(const Foundation::DevCommand& cmd) {
		const ecs::EntityID id = parseEntity(cmd.param("colonist"));
		auto*				pos = m_ctx.world->getComponent<ecs::Position>(id);
		if (pos == nullptr) {
			LOG_WARNING(Game, "[DevAPI] teleport: no entity #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "No such entity", UI::ToastSeverity::Warning);
			return;
		}
		const Foundation::Vec2 to = parsePoint(cmd.param("to"));
		if (!requireValidPosition(to, "teleport")) {
			return;
		}
		pos->value = {to.x, to.y};
		if (auto* target = m_ctx.world->getComponent<ecs::MovementTarget>(id)) {
			target->active = false;
		}
		if (auto* vel = m_ctx.world->getComponent<ecs::Velocity>(id)) {
			vel->value = {0.0F, 0.0F};
		}
		LOG_INFO(Game, "[DevAPI] teleport: #%llu -> (%.1f, %.1f)", static_cast<unsigned long long>(id), to.x, to.y);
		m_ctx.ui->pushNotification("Dev", "Teleported", UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devSelect(const Foundation::DevCommand& cmd) {
		ecs::EntityID id = ecs::kInvalidEntity;
		if (cmd.hasParam("colonist")) {
			id = parseEntity(cmd.param("colonist"));
		} else if (cmd.hasParam("at")) {
			id = nearestColonist(parsePoint(cmd.param("at")));
		} else {
			LOG_WARNING(Game, "[DevAPI] select: needs colonist=<id> or at=x,y");
			return;
		}
		if (id == ecs::kInvalidEntity || !m_ctx.world->isAlive(id)) {
			LOG_WARNING(Game, "[DevAPI] select: no colonist to select");
			m_ctx.ui->pushNotification("Dev", "No colonist to select", UI::ToastSeverity::Warning);
			return;
		}
		m_ctx.selection->selectColonist(id);
		LOG_INFO(Game, "[DevAPI] select: colonist #%llu", static_cast<unsigned long long>(id));
	}

	void DevCommandHandler::devKill(const Foundation::DevCommand& cmd) {
		const ecs::EntityID id = parseEntity(cmd.param("colonist"));
		if (!m_ctx.world->isAlive(id)) {
			LOG_WARNING(Game, "[DevAPI] kill: no entity #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "No such entity", UI::ToastSeverity::Warning);
			return;
		}
		const auto* colonistSel = std::get_if<world_sim::ColonistSelection>(&m_ctx.selection->current());
		if (colonistSel != nullptr && colonistSel->entityId == id) {
			m_ctx.selection->clearSelection();
		}
		m_ctx.world->destroyEntity(id);
		LOG_INFO(Game, "[DevAPI] kill: removed #%llu", static_cast<unsigned long long>(id));
		m_ctx.ui->pushNotification("Dev", "Colonist removed", UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devComplete(const Foundation::DevCommand& cmd) {
		const ecs::EntityID id = parseEntity(cmd.param("id"));
		if (m_ctx.world->getSystem<ecs::ConstructionSystem>().forceCompleteBlueprint(id)) {
			LOG_INFO(Game, "[DevAPI] complete: built blueprint #%llu", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "Blueprint built", UI::ToastSeverity::Info);
		} else {
			LOG_WARNING(Game, "[DevAPI] complete: #%llu is not a buildable blueprint", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "Not a blueprint", UI::ToastSeverity::Warning);
		}
	}

	void DevCommandHandler::devFoundation(const Foundation::DevCommand& cmd) {
		std::vector<Foundation::Vec2> pts = parsePointList(cmd.param("pts"));
		if (pts.size() < 3) {
			LOG_WARNING(Game, "[DevAPI] foundation: pts needs >= 3 'x,y' pairs (got %zu)", pts.size());
			return;
		}
		// The WHOLE footprint must sit on buildable land (vertices, edges, AND interior).
		// A footprint that spans water between on-land corners, or clips a water hole,
		// refuses the whole placement (stamp nothing), matching the real build tool's gate.
		if (!requireWalkableArea(pts, "foundation")) {
			return;
		}
		const std::string material = cmd.param("material", "Wood");

		auto&	   constructionWorld = m_ctx.drawing->world();
		const auto commit = constructionWorld.commitFoundation(pts, material);
		if (!commit.ok()) {
			LOG_WARNING(Game, "[DevAPI] foundation: commit rejected (status %d)", static_cast<int>(commit.status));
			m_ctx.ui->pushNotification("Dev", "Foundation commit rejected", UI::ToastSeverity::Warning);
			return;
		}

		const ecs::EntityID entity = spawnFoundationBlueprintEntity(commit.id, pts, material);
		if (entity == ecs::kInvalidEntity) {
			LOG_WARNING(Game, "[DevAPI] foundation: spawn failed for #%llu", static_cast<unsigned long long>(commit.id));
			return;
		}

		const std::string builtStr = cmd.param("built", "0");
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
		m_ctx.ui->pushNotification("Dev", built ? "Built foundation placed" : "Foundation blueprint placed", UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devWalls(const Foundation::DevCommand& cmd) {
		std::vector<Foundation::Vec2> pts = parsePointList(cmd.param("pts"));
		if (pts.size() < 2) {
			LOG_WARNING(Game, "[DevAPI] walls: pts needs >= 2 'x,y' pairs (got %zu)", pts.size());
			return;
		}
		const std::string material = cmd.param("material", "Wood");
		const std::string thickness = cmd.param("thickness", "Standard");
		// strtoull, not strtoul: FoundationId is 64-bit, and unsigned long is only 32-bit on Windows.
		const auto host = static_cast<engine::construction::FoundationId>(std::strtoull(cmd.param("host", "0").c_str(), nullptr, 10));

		const std::string builtStr = cmd.param("built", "1");
		const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");
		const std::string closeStr = cmd.param("close", "1");
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
		m_ctx.ui->pushNotification("Dev", std::to_string(n) + (built ? " built wall(s)" : " wall blueprint(s)"), UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devOpening(const Foundation::DevCommand& cmd) {
		const std::string type = cmd.param("type", "Door");
		const auto*		  typeDef = engine::assets::ConstructionRegistry::Get().getOpeningType(type);
		if (typeDef == nullptr) {
			LOG_WARNING(Game, "[DevAPI] opening: unknown type '%s'", type.c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown opening type", UI::ToastSeverity::Warning);
			return;
		}

		auto& constructionWorld = m_ctx.drawing->world();

		// Resolve the target segment + centerline t: seg= names it directly (t defaults to
		// center), pt= snaps to the nearest built wall (t from the snap).
		engine::construction::SegmentId segment = engine::construction::kInvalidSegment;
		float							t = 0.5F;
		if (cmd.hasParam("seg")) {
			segment = static_cast<engine::construction::SegmentId>(std::strtoull(cmd.param("seg").c_str(), nullptr, 10));
		} else if (cmd.hasParam("pt")) {
			const std::vector<Foundation::Vec2> pts = parsePointList(cmd.param("pt"));
			if (pts.empty()) {
				LOG_WARNING(Game, "[DevAPI] opening: pt must be 'x,y' world meters");
				return;
			}
			const auto&							   registry = engine::assets::ConstructionRegistry::Get();
			const engine::construction::SnapEngine snap(registry.snapping(), constructionWorld);
			const auto							   os = snap.snapOpening(pts.front(), typeDef->widthMeters);
			if (!os.valid) {
				LOG_WARNING(Game, "[DevAPI] opening: no built wall near pt");
				m_ctx.ui->pushNotification("Dev", "No built wall near point", UI::ToastSeverity::Warning);
				return;
			}
			segment = os.segment;
			t = os.t;
		} else {
			LOG_WARNING(Game, "[DevAPI] opening: needs seg=<id> or pt=x,y");
			return;
		}

		// Explicit t= overrides the resolved parameter (addOpening clamps to [0,1]).
		if (cmd.hasParam("t")) {
			t = std::strtof(cmd.param("t").c_str(), nullptr);
		}

		const std::string builtStr = cmd.param("built", "1");
		const bool		  built = (builtStr == "1" || builtStr == "on" || builtStr == "true" || builtStr == "yes");

		const engine::construction::OpeningId id = m_ctx.drawing->devCommitOpening(segment, t, type, built);
		if (id == engine::construction::kInvalidOpening) {
			LOG_WARNING(
				Game,
				"[DevAPI] opening: commit rejected (segment #%llu, type %s)",
				static_cast<unsigned long long>(segment),
				type.c_str()
			);
			m_ctx.ui->pushNotification("Dev", "Opening commit rejected", UI::ToastSeverity::Warning);
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
		m_ctx.ui->pushNotification("Dev", built ? "Built opening placed" : "Opening blueprint placed", UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devCraft(const Foundation::DevCommand& cmd) {
		const std::string recipe = cmd.param("recipe", "Recipe_AxePrimitive");
		const long		  qtyRaw = std::strtol(cmd.param("n", "1").c_str(), nullptr, 10);
		const auto		  qty = static_cast<uint32_t>(qtyRaw < 1 ? 1 : qtyRaw);

		if (engine::assets::RecipeRegistry::Get().getRecipe(recipe) == nullptr) {
			LOG_WARNING(Game, "[DevAPI] craft: unknown recipe '%s'", recipe.c_str());
			m_ctx.ui->pushNotification("Dev", "Unknown recipe: " + recipe, UI::ToastSeverity::Warning);
			return;
		}

		// Queue the job at the nearest crafting station (entity with a WorkQueue) to `at`.
		const Foundation::Vec2 at = parsePoint(cmd.param("at"));
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
			m_ctx.ui->pushNotification("Dev", "No crafting station to queue at", UI::ToastSeverity::Warning);
			return;
		}

		best->addJob(recipe, qty);
		LOG_INFO(
			Game, "[DevAPI] craft: queued %u x %s at station #%llu", qty, recipe.c_str(), static_cast<unsigned long long>(bestEntity)
		);
		m_ctx.ui->pushNotification("Dev", "Queued " + std::to_string(qty) + " " + recipe, UI::ToastSeverity::Info);
	}

	void DevCommandHandler::devStorage(const Foundation::DevCommand& cmd) {
		const Foundation::Vec2 at = parsePoint(cmd.param("at"));
		const ecs::EntityID	   id = nearestStorageEntity(at);
		if (id == ecs::kInvalidEntity) {
			LOG_WARNING(Game, "[DevAPI] storage: no storage entity found near (%.1f, %.1f)", at.x, at.y);
			m_ctx.ui->pushNotification("Dev", "No storage entity found", UI::ToastSeverity::Warning);
			return;
		}
		auto* storageConfig = m_ctx.world->getComponent<ecs::StorageConfiguration>(id);
		if (storageConfig == nullptr) {
			LOG_WARNING(Game, "[DevAPI] storage: entity #%llu has no StorageConfiguration", static_cast<unsigned long long>(id));
			m_ctx.ui->pushNotification("Dev", "Entity has no StorageConfiguration", UI::ToastSeverity::Warning);
			return;
		}

		// Parse category (default: RawMaterial)
		engine::assets::ItemCategory category = engine::assets::ItemCategory::RawMaterial;
		const std::string			  categoryStr = cmd.param("category", "RawMaterial");
		if (equalsIgnoreCase(categoryStr, "Food")) {
			category = engine::assets::ItemCategory::Food;
		} else if (equalsIgnoreCase(categoryStr, "Tool")) {
			category = engine::assets::ItemCategory::Tool;
		} else if (equalsIgnoreCase(categoryStr, "Furniture")) {
			category = engine::assets::ItemCategory::Furniture;
		} else if (equalsIgnoreCase(categoryStr, "None")) {
			category = engine::assets::ItemCategory::None;
		}
		// RawMaterial is the default (already set above)

		// Parse priority (default: Medium)
		ecs::StoragePriority priority = ecs::StoragePriority::Medium;
		const std::string	 priorityStr = cmd.param("priority", "Medium");
		if (equalsIgnoreCase(priorityStr, "Low")) {
			priority = ecs::StoragePriority::Low;
		} else if (equalsIgnoreCase(priorityStr, "High")) {
			priority = ecs::StoragePriority::High;
		} else if (equalsIgnoreCase(priorityStr, "Critical")) {
			priority = ecs::StoragePriority::Critical;
		}
		// Medium is the default (already set above)

		const std::string defName = cmd.param("item", "*");
		const auto		  minAmount = static_cast<uint32_t>(std::strtol(cmd.param("min", "0").c_str(), nullptr, 10));
		const auto		  maxAmount = static_cast<uint32_t>(std::strtol(cmd.param("max", "0").c_str(), nullptr, 10));

		ecs::StorageRule rule;
		rule.defName = defName;
		rule.category = category;
		rule.priority = priority;
		rule.minAmount = minAmount;
		rule.maxAmount = maxAmount;
		storageConfig->addRule(std::move(rule));

		LOG_INFO(
			Game,
			"[DevAPI] storage: added rule to #%llu (item=%s, category=%s, priority=%s, min=%u, max=%u)",
			static_cast<unsigned long long>(id),
			defName.c_str(),
			categoryStr.c_str(),
			priorityStr.c_str(),
			minAmount,
			maxAmount
		);
		m_ctx.ui->pushNotification("Dev", "Added storage rule: " + defName + " (" + categoryStr + ")", UI::ToastSeverity::Info);
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

	ecs::EntityID DevCommandHandler::spawnFoundationBlueprintEntity(
		engine::construction::FoundationId id, const std::vector<Foundation::Vec2>& pts, const std::string& material
	) {
		auto& constructionWorld = m_ctx.drawing->world();
		if (constructionWorld.get(id) == nullptr) {
			return ecs::kInvalidEntity;
		}

		const float area = constructionWorld.areaSquareMeters(id);

		const auto& registry = engine::assets::ConstructionRegistry::Get();
		const auto* mat = registry.getMaterial(material);
		float		costRate = 0.0F;
		float		workRate = 0.0F;
		float		hpRate = 0.0F;
		if (mat != nullptr) {
			costRate = mat->costRatePerSquareMeter;
			workRate = mat->workRatePerSquareMeter;
			hpRate = mat->hp;
		}

		auto entity = m_ctx.world->createEntity();

		// Centroid (average of vertices) keeps the transform inside the footprint.
		Foundation::Vec2 centroid{0.0F, 0.0F};
		for (const auto& p : pts) {
			centroid += p;
		}
		centroid /= static_cast<float>(pts.size());
		m_ctx.world->addComponent<ecs::Position>(entity, ecs::Position{{centroid.x, centroid.y}});

		m_ctx.world->addComponent<ecs::Structure>(entity, ecs::Structure{ecs::StructureKind::Foundation, id});

		ecs::StructureBlueprint blueprint;
		blueprint.phase = ecs::StructureBlueprint::BuildPhase::Clearing;
		const auto requiredQty = static_cast<uint32_t>(std::ceil(static_cast<double>(area) * static_cast<double>(costRate)));
		if (requiredQty > 0) {
			blueprint.required.emplace_back(material, requiredQty);
		}
		blueprint.workTotal = area * workRate;
		m_ctx.world->addComponent<ecs::StructureBlueprint>(entity, std::move(blueprint));

		const float maxHp = area * hpRate;
		m_ctx.world->addComponent<ecs::StructureHealth>(entity, ecs::StructureHealth{maxHp, maxHp});

		constructionWorld.setEntity(id, entity);
		return entity;
	}

	// ===================================================================== STATE READBACK

	std::string DevCommandHandler::serializeState(const std::string& what) {
		std::ostringstream out;
		if (what == "colonists") {
			serializeColonists(out);
		} else if (what == "construction") {
			serializeConstruction(out);
		} else if (what == "stations") {
			serializeStations(out);
		} else if (what == "storage") {
			serializeStorage(out);
		} else if (what == "time") {
			serializeTime(out);
		} else if (what == "landing") {
			serializeLanding(out);
		} else {
			serializeSummary(out);
		}
		return out.str();
	}

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

				out << ",\"cargoKg\":" << ecs::carriedCargoMassKg(*inv, engine::assets::AssetRegistry::Get())
					<< ",\"carryCapacityKg\":" << inv->carryCapacityKg;
			}
			out << "}";
		}
		out << "]}";
	}

	void DevCommandHandler::serializeConstruction(std::ostringstream& out) {
		const auto& cworld = m_ctx.drawing->world();
		out << "{\"foundations\":[";
		bool first = true;
		for (const auto& foundation : cworld.foundations()) {
			out << (first ? "" : ",");
			first = false;
			out << "{\"id\":" << static_cast<unsigned long long>(foundation.id) << ",\"material\":\"" << jsonEscape(foundation.material)
				<< "\",\"state\":\"" << (foundation.state == engine::construction::FoundationState::Built ? "Built" : "Blueprint")
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
					out << ",\"required\":{";
					bool firstReq = true;
					for (const auto& [defName, qty] : bp->required) {
						out << (firstReq ? "" : ",") << "\"" << jsonEscape(defName) << "\":" << qty;
						firstReq = false;
					}
					out << "},\"delivered\":{";
					bool firstDel = true;
					for (const auto& [defName, qty] : bp->delivered) {
						out << (firstDel ? "" : ",") << "\"" << jsonEscape(defName) << "\":" << qty;
						firstDel = false;
					}
					out << "},\"progress\":" << bp->progress();
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
				<< (seg.state == engine::construction::FoundationState::Built ? "Built" : "Blueprint") << "\",\"entity\":"
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
					out << ",\"required\":{";
					bool firstReq = true;
					for (const auto& [defName, qty] : bp->required) {
						out << (firstReq ? "" : ",") << "\"" << jsonEscape(defName) << "\":" << qty;
						firstReq = false;
					}
					out << "},\"delivered\":{";
					bool firstDel = true;
					for (const auto& [defName, qty] : bp->delivered) {
						out << (firstDel ? "" : ",") << "\"" << jsonEscape(defName) << "\":" << qty;
						firstDel = false;
					}
					out << "},\"progress\":" << bp->progress();
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
					case engine::assets::ItemCategory::None: out << "None"; break;
					case engine::assets::ItemCategory::RawMaterial: out << "RawMaterial"; break;
					case engine::assets::ItemCategory::Food: out << "Food"; break;
					case engine::assets::ItemCategory::Tool: out << "Tool"; break;
					case engine::assets::ItemCategory::Furniture: out << "Furniture"; break;
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
