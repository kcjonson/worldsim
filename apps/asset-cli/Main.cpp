// asset-cli: headless command-line surface for the asset library.
//
// Commands: list, search, inspect (with triangle and vertex counts for the
// def's template and each variant), validate, render. Everything is
// machine-readable with --json. No window and no debug server, so many
// invocations can run in parallel (agent loops, CI matrices).

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/AssetRenderer.h"
#include "assets/AssetValidator.h"
#include "assets/VariantSeed.h"

#include "BaseShader.h"
#include "backend/gl/GLBackend.h"
#include "graphics/Color.h"
#include "PngEncoder.h"
#include "primitives/Primitives.h"
#include "utils/Log.h"
#include "utils/ResourcePath.h"
#include "vector/Types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using assets::AssetDefinition;
using assets::AssetRegistry;
using assets::AssetType;
using assets::Severity;
using assets::ValidationReport;
using json = nlohmann::json;

namespace {

	enum ExitCode { kOk = 0, kUsage = 1, kNotFound = 2, kFailure = 3 };

	const char* kUsageText =
		"Usage: asset-cli <command> [options]\n"
		"\n"
		"Commands:\n"
		"  list [--type simple|procedural] [--group <name>] [--json]\n"
		"  search <query> [--json]\n"
		"  inspect <defName> [--json]\n"
		"  validate [--json] [--render-smoke]\n"
		"  render <defName> --out <file.png> [--size WxH] [--bg r,g,b,a] [--seed N] [--samples N]\n"
		"  render <defName> --out <file.png> (--grid | --age a,b,... [--seed s,t,...] [--pairs N]) [--silhouette] [--ppm N] [--bg r,g,b,a]\n"
		"\n"
		"render with --seed alone fits one generator run to --size. --grid draws every template the game\n"
		"draws (age classes as rows, variants as columns); --age runs the generator at each age from 0 to 1\n"
		"(columns) for each seed (rows, default 0), or with --pairs N the i-th age with the i-th seed, N to a\n"
		"row. Both draw at one scale, --ppm pixels a metre (default\n"
		"48). --silhouette fills what the selection silhouette keeps in one flat grey, on white unless --bg\n"
		"is given.\n";

	// --- argument helpers ---

	bool hasFlag(const std::vector<std::string>& args, const std::string& flag) {
		return std::find(args.begin(), args.end(), flag) != args.end();
	}

	std::optional<std::string> flagValue(const std::vector<std::string>& args, const std::string& flag) {
		for (size_t i = 0; i + 1 < args.size(); ++i) {
			if (args[i] == flag) {
				return args[i + 1];
			}
		}
		return std::nullopt;
	}

	// Flags that consume the following argument as their value.
	bool flagTakesValue(const std::string& flag) {
		static const char* kValueFlags[] = {"--type", "--group", "--out", "--size", "--bg", "--seed", "--samples", "--age", "--ppm", "--pairs"};
		for (const char* vf : kValueFlags) {
			if (flag == vf) {
				return true;
			}
		}
		return false;
	}

	// First positional argument, skipping flags and the values of value-taking
	// flags. Boolean flags (--json, --render-smoke) consume no value.
	std::optional<std::string> positional(const std::vector<std::string>& args) {
		for (size_t i = 0; i < args.size(); ++i) {
			if (args[i].rfind("--", 0) == 0) {
				if (flagTakesValue(args[i])) {
					++i; // also skip this flag's value
				}
				continue;
			}
			return args[i];
		}
		return std::nullopt;
	}

	std::string toLower(std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	const char* typeName(AssetType t) {
		return t == AssetType::Simple ? "simple" : "procedural";
	}

	const char* severityName(Severity s) {
		return s == Severity::Error ? "error" : "warning";
	}

	// --- library loading (no GL) ---

	bool loadLibrary() {
		const std::string assetsRoot = Foundation::findResourceString("assets/world");
		if (assetsRoot.empty()) {
			std::cerr << "error: could not locate assets/world (run from a directory with assets/, or next to the staged exe)\n";
			return false;
		}
		const std::string shared = Foundation::findResourceString("assets/shared/scripts");
		if (!shared.empty()) {
			AssetRegistry::Get().setSharedScriptsPath(shared);
		}
		AssetRegistry::Get().loadDefinitionsFromFolder(assetsRoot);
		return true;
	}

	std::vector<std::string> sortedDefNames() {
		auto names = AssetRegistry::Get().getDefinitionNames();
		std::sort(names.begin(), names.end());
		return names;
	}

	// --- GL context for `render` (hidden window, no debug server) ---

	GLFWwindow* g_window = nullptr;

	bool initGl() {
		if (glfwInit() != GLFW_TRUE) {
			return false;
		}
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
		g_window = glfwCreateWindow(16, 16, "asset-cli", nullptr, nullptr);
		if (g_window == nullptr) {
			glfwTerminate();
			return false;
		}
		glfwMakeContextCurrent(g_window);
		// The renderer's GL backend initializes GLEW on this context
		std::unique_ptr<Renderer::backend::gl::GLBackend> backend = Renderer::backend::gl::GLBackend::create();
		if (backend == nullptr) {
			glfwDestroyWindow(g_window);
			glfwTerminate();
			g_window = nullptr;
			return false;
		}
		Renderer::Primitives::init(std::move(backend), base_shader::source());
		return true;
	}

	void shutdownGl() {
		if (g_window != nullptr) {
			Renderer::Primitives::shutdown();
			glfwDestroyWindow(g_window);
			glfwTerminate();
			g_window = nullptr;
		}
	}

	bool parseSize(const std::string& s, int& w, int& h) {
		return std::sscanf(s.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0;
	}

	Foundation::Color parseColor(const std::optional<std::string>& s) {
		float r = 0.0F;
		float g = 0.0F;
		float b = 0.0F;
		float a = 0.0F; // default: transparent
		if (s) {
			std::sscanf(s->c_str(), "%f,%f,%f,%f", &r, &g, &b, &a);
		}
		return {r, g, b, a};
	}

	std::string insertSuffix(const std::string& path, int index) {
		const size_t dot = path.find_last_of('.');
		const std::string suffix = "_" + std::to_string(index);
		if (dot == std::string::npos) {
			return path + suffix;
		}
		return path.substr(0, dot) + suffix + path.substr(dot);
	}

	// --- mesh counts for inspect ---

	// Ground tiles and groundcover build their variants by seed in their own renderers; every
	// other def draws through the registry's templates.
	bool drawsTemplates(const AssetDefinition& def) {
		return def.role != assets::AssetRole::Ground && def.role != assets::AssetRole::Groundcover;
	}

	// The template the game draws for one (age class, variant) of the def, by the same key
	// placement resolves (AssetRegistry::templateId). A simple asset is one drawing, so it
	// lists one row.
	json templateCounts(const AssetDefinition& def, assets::TreeTraits traits) {
		json row{
			{"ageClass", def.ageClasses.empty() ? json(nullptr) : json(def.ageClasses[traits.ageClass].name)},
			{"variant", traits.variant},
			{"seed", AssetRegistry::generationSeed(def, traits)},
			{"triangles", nullptr},
			{"vertices", nullptr},
		};
		if (const renderer::TessellatedMesh* mesh = AssetRegistry::Get().getTemplate(def.defName, traits)) {
			row["triangles"] = mesh->getTriangleCount();
			row["vertices"] = mesh->getVertexCount();
		}
		return row;
	}

	json templateTable(const AssetDefinition& def) {
		json rows = json::array();
		if (def.assetType == AssetType::Simple) {
			rows.push_back(templateCounts(def, {}));
		} else if (def.ageClasses.empty()) {
			for (uint32_t v = 0; v < std::max<uint32_t>(def.variantCount, 1U); ++v) {
				rows.push_back(templateCounts(def, {.ageClass = 0, .variant = static_cast<uint16_t>(v)}));
			}
		} else {
			for (size_t a = 0; a < def.ageClasses.size(); ++a) {
				for (uint16_t v = 0; v < def.ageClasses[a].variants; ++v) {
					rows.push_back(templateCounts(def, {.ageClass = static_cast<uint8_t>(a), .variant = v}));
				}
			}
		}
		return rows;
	}

	// One of a ground or groundcover def's variants at the seed its renderer builds it with:
	// the ground tile baker's variantSeed for a ground def, the variant index for groundcover.
	json variantCounts(const AssetDefinition& def, uint32_t variant) {
		const uint32_t seed =
			def.role == assets::AssetRole::Ground ? assets::variantSeed(def.ground.seedGroup, variant) : variant;
		std::vector<renderer::TessellatedMesh> meshes;
		json								   row{{"seed", seed}, {"triangles", nullptr}, {"vertices", nullptr}};
		if (AssetRegistry::Get().buildMeshes(def.defName, seed, meshes)) {
			size_t triangles = 0;
			size_t vertices = 0;
			for (const auto& m : meshes) {
				triangles += m.getTriangleCount();
				vertices += m.getVertexCount();
			}
			row["triangles"] = triangles;
			row["vertices"] = vertices;
		}
		return row;
	}

	std::string countsText(const json& counts) {
		if (counts["triangles"].is_null()) {
			return "no geometry";
		}
		return std::to_string(counts["triangles"].get<size_t>()) + " triangles, " + std::to_string(counts["vertices"].get<size_t>()) +
			   " vertices";
	}

	// --- commands ---

	int cmdList(const std::vector<std::string>& args) {
		if (!loadLibrary()) {
			return kFailure;
		}
		const auto typeFilter = flagValue(args, "--type");
		const auto groupFilter = flagValue(args, "--group");
		const bool asJson = hasFlag(args, "--json");

		std::vector<std::string> groupMembers;
		if (groupFilter) {
			groupMembers = AssetRegistry::Get().getGroupMembers(*groupFilter);
		}

		json arr = json::array();
		for (const auto& name : sortedDefNames()) {
			const AssetDefinition* d = AssetRegistry::Get().getDefinition(name);
			if (d == nullptr) {
				continue;
			}
			if (typeFilter) {
				const bool simple = d->assetType == AssetType::Simple;
				if ((*typeFilter == "simple") != simple) {
					continue;
				}
			}
			if (groupFilter && std::find(groupMembers.begin(), groupMembers.end(), name) == groupMembers.end()) {
				continue;
			}
			if (asJson) {
				arr.push_back({{"defName", name}, {"label", d->label}, {"type", typeName(d->assetType)}});
			} else {
				std::cout << name << "\t" << typeName(d->assetType) << "\t" << d->label << "\n";
			}
		}
		if (asJson) {
			std::cout << arr.dump(2) << "\n";
		}
		return kOk;
	}

	int cmdSearch(const std::vector<std::string>& args) {
		const auto query = positional(args);
		if (!query) {
			std::cerr << "error: search needs a query\n";
			return kUsage;
		}
		if (!loadLibrary()) {
			return kFailure;
		}
		const bool asJson = hasFlag(args, "--json");
		const std::string needle = toLower(*query);

		json arr = json::array();
		for (const auto& name : sortedDefNames()) {
			const AssetDefinition* d = AssetRegistry::Get().getDefinition(name);
			if (d == nullptr) {
				continue;
			}
			if (toLower(name).find(needle) == std::string::npos && toLower(d->label).find(needle) == std::string::npos) {
				continue;
			}
			if (asJson) {
				arr.push_back({{"defName", name}, {"label", d->label}, {"type", typeName(d->assetType)}});
			} else {
				std::cout << name << "\t" << typeName(d->assetType) << "\t" << d->label << "\n";
			}
		}
		if (asJson) {
			std::cout << arr.dump(2) << "\n";
		}
		return kOk;
	}

	int cmdInspect(const std::vector<std::string>& args) {
		const auto defName = positional(args);
		if (!defName) {
			std::cerr << "error: inspect needs a defName\n";
			return kUsage;
		}
		if (!loadLibrary()) {
			return kFailure;
		}
		const AssetDefinition* d = AssetRegistry::Get().getDefinition(*defName);
		if (d == nullptr) {
			std::cerr << "error: no such defName: " << *defName << "\n";
			return kNotFound;
		}
		const bool asJson = hasFlag(args, "--json");

		const std::string resolvedSvg = d->svgPath.empty() ? "" : d->resolvePath(d->svgPath).string();

		json warnings = json::array();
		for (const auto& issue : AssetRegistry::Get().getValidationReport().issues) {
			if (issue.defName == *defName) {
				warnings.push_back({{"severity", severityName(issue.severity)}, {"field", issue.field}, {"message", issue.message}});
			}
		}

		json out;
		out["defName"] = d->defName;
		out["label"] = d->label;
		out["type"] = typeName(d->assetType);
		out["worldHeight"] = d->worldHeight;
		out["svgPath"] = d->svgPath;
		out["resolvedSvgPath"] = resolvedSvg;
		out["scriptPath"] = d->scriptPath;
		out["generatorName"] = d->generatorName;
		out["groups"] = d->placement.groups;
		if (drawsTemplates(*d)) {
			out["templates"] = templateTable(*d);
		} else {
			json variants = json::array();
			for (uint32_t v = 0; v < std::max<uint32_t>(d->variantCount, 1U); ++v) {
				json row = variantCounts(*d, v);
				row["variant"] = v;
				variants.push_back(row);
			}
			out["variants"] = variants;
		}
		out["issues"] = warnings;

		if (asJson) {
			std::cout << out.dump(2) << "\n";
		} else {
			std::cout << "defName:       " << d->defName << "\n";
			std::cout << "label:         " << d->label << "\n";
			std::cout << "type:          " << typeName(d->assetType) << "\n";
			std::cout << "worldHeight:   " << d->worldHeight << "\n";
			if (!d->svgPath.empty()) {
				std::cout << "svgPath:       " << d->svgPath << "  ->  " << resolvedSvg << "\n";
			}
			if (!d->scriptPath.empty()) {
				std::cout << "scriptPath:    " << d->scriptPath << "\n";
			}
			if (!d->generatorName.empty()) {
				std::cout << "generatorName: " << d->generatorName << "\n";
			}
			if (!d->placement.groups.empty()) {
				std::cout << "groups:        ";
				for (const auto& g : d->placement.groups) {
					std::cout << g << " ";
				}
				std::cout << "\n";
			}
			if (out.contains("templates")) {
				std::cout << "templates:\n";
				for (const auto& row : out["templates"]) {
					std::cout << "  ";
					if (!row["ageClass"].is_null()) {
						std::cout << row["ageClass"].get<std::string>() << " ";
					}
					std::cout << "variant " << row["variant"].get<uint32_t>() << " (seed " << row["seed"].get<uint32_t>()
							  << "): " << countsText(row) << "\n";
				}
			}
			if (out.contains("variants")) {
				for (const auto& row : out["variants"]) {
					std::cout << "  variant " << row["variant"].get<uint32_t>() << " (seed " << row["seed"].get<uint32_t>()
							  << "): " << countsText(row) << "\n";
				}
			}
			for (const auto& w : warnings) {
				std::cout << "[" << w["severity"].get<std::string>() << "] " << w["field"].get<std::string>() << ": "
						  << w["message"].get<std::string>() << "\n";
			}
		}
		return kOk;
	}

	int cmdValidate(const std::vector<std::string>& args) {
		if (!loadLibrary()) {
			return kFailure;
		}
		const bool asJson = hasFlag(args, "--json");
		const bool renderSmoke = hasFlag(args, "--render-smoke");

		ValidationReport report = AssetRegistry::Get().getValidationReport();

		// Optional smoke pass: confirm every asset produces geometry (CPU; covers
		// missing SVGs, generator failures, and empty output). No GL required.
		if (renderSmoke) {
			for (const auto& name : sortedDefNames()) {
				std::vector<renderer::TessellatedMesh> meshes;
				if (!AssetRegistry::Get().buildMeshes(name, 0, meshes)) {
					report.add(Severity::Error, name, "render", "Asset produced no geometry (render smoke test)", "");
				}
			}
			AssetRegistry::Get().checkCollisionVariantIndependence(report);
		}

		if (asJson) {
			json issues = json::array();
			for (const auto& i : report.issues) {
				issues.push_back(
					{{"severity", severityName(i.severity)}, {"defName", i.defName}, {"field", i.field}, {"message", i.message}, {"context", i.context}}
				);
			}
			std::cout << json{{"errors", report.errorCount()}, {"warnings", report.warningCount()}, {"issues", issues}}.dump(2) << "\n";
		} else {
			for (const auto& i : report.issues) {
				std::cout << "[" << severityName(i.severity) << "] " << (i.defName.empty() ? "-" : i.defName) << " " << i.field << ": " << i.message;
				if (!i.context.empty()) {
					std::cout << "  (" << i.context << ")";
				}
				std::cout << "\n";
			}
			std::cout << report.errorCount() << " error(s), " << report.warningCount() << " warning(s)\n";
		}
		return report.errorCount() > 0 ? kFailure : kOk;
	}

	std::vector<float> parseNumbers(const std::string& list) {
		std::vector<float> out;
		size_t			   start = 0;
		while (start <= list.size()) {
			const size_t comma = std::min(list.find(',', start), list.size());
			out.push_back(std::stof(list.substr(start, comma - start)));
			start = comma + 1;
		}
		return out;
	}

	// A sheet at one scale: every template the game draws for the def (--grid: age classes as rows,
	// variants as columns), or the generator run at each of --age's ages (columns) for each of --seed's
	// seeds (rows).
	int renderSheet(const std::string& defName, const std::string& out, const std::vector<std::string>& args) {
		const bool grid = hasFlag(args, "--grid");
		const auto ageArg = flagValue(args, "--age");
		if (grid == ageArg.has_value()) {
			std::cerr << "error: a sheet is either --grid or --age <a,b,...>\n";
			return kUsage;
		}
		if (flagValue(args, "--size") || flagValue(args, "--samples") || (grid && flagValue(args, "--seed"))) {
			std::cerr << "error: a sheet is sized by --ppm, and --grid draws the game's own seeds\n";
			return kUsage;
		}
		float			   ppm = 48.0F;
		std::vector<float> ages;
		std::vector<float> seeds{0.0F};
		size_t			   pairsPerRow = 0;
		try {
			if (const auto pairsArg = flagValue(args, "--pairs")) {
				pairsPerRow = static_cast<size_t>(std::stoul(*pairsArg));
			}
			if (const auto ppmArg = flagValue(args, "--ppm")) {
				ppm = std::stof(*ppmArg);
			}
			if (ageArg) {
				ages = parseNumbers(*ageArg);
			}
			if (const auto seedArg = flagValue(args, "--seed")) {
				seeds = parseNumbers(*seedArg);
			}
		} catch (const std::exception&) {
			ppm = 0.0F;
		}
		if (flagValue(args, "--pairs") && (pairsPerRow == 0 || ages.size() != seeds.size())) {
			std::cerr << "error: --pairs N takes as many --age values as --seed values, N to a row\n";
			return kUsage;
		}
		if (!(ppm > 0.0F) || std::any_of(ages.begin(), ages.end(), [](float a) { return a < 0.0F || a > 1.0F; }) ||
			std::any_of(seeds.begin(), seeds.end(), [](float v) { return v < 0.0F; })) {
			std::cerr << "error: --ppm must be positive, --age a list of numbers from 0 to 1, --seed a list of non-negative integers\n";
			return kUsage;
		}
		const bool				silhouette = hasFlag(args, "--silhouette");
		const auto				bgArg = flagValue(args, "--bg");
		const Foundation::Color bg = (silhouette && !bgArg) ? Foundation::Color(1.0F, 1.0F, 1.0F, 1.0F) : parseColor(bgArg);

		if (!loadLibrary()) {
			return kFailure;
		}
		AssetRegistry&		   registry = AssetRegistry::Get();
		const AssetDefinition* def = registry.getDefinition(defName);
		if (def == nullptr) {
			std::cerr << "error: no such defName: " << defName << "\n";
			return kNotFound;
		}

		std::vector<std::vector<const renderer::TessellatedMesh*>> rows;
		std::vector<std::unique_ptr<renderer::TessellatedMesh>>	   built;
		if (grid) {
			const size_t classes = std::max<size_t>(def->ageClasses.size(), 1);
			for (size_t a = 0; a < classes; ++a) {
				auto&		   row = rows.emplace_back();
				const uint32_t variants = def->ageClasses.empty() ? std::max<uint32_t>(def->variantCount, 1U) : def->ageClasses[a].variants;
				for (uint32_t v = 0; v < (def->assetType == AssetType::Simple ? 1U : variants); ++v) {
					row.push_back(registry.getTemplate(defName, {.ageClass = static_cast<uint8_t>(a), .variant = static_cast<uint16_t>(v)}));
				}
				std::cout << "  row " << (a + 1) << ": " << (def->ageClasses.empty() ? defName : def->ageClasses[a].name) << ", " << row.size()
						  << " variants\n";
			}
		} else {
			// The generator reads its age from the def's `age` param, so the def is registered again with it set.
			// One cell per (age, seed): every age by every seed, or with --pairs N the i-th age with the
			// i-th seed, N to a row.
			struct Cell {
				float  age;
				float  seed;
				size_t row;
			};
			std::vector<Cell> cells;
			if (pairsPerRow > 0) {
				for (size_t i = 0; i < ages.size(); ++i) {
					cells.push_back({ages[i], seeds[i], i / pairsPerRow});
				}
			} else {
				for (size_t sIdx = 0; sIdx < seeds.size(); ++sIdx) {
					for (const float age : ages) {
						cells.push_back({age, seeds[sIdx], sIdx});
					}
				}
			}
			const AssetDefinition original = *def;
			for (const Cell& cell : cells) {
				const float age = cell.age;
				const float seed = cell.seed;
				if (rows.size() <= cell.row) {
					rows.emplace_back();
				}
				auto& row = rows.back();
				{
					AssetDefinition aged = original;
					aged.params.setFloat("age", age);
					registry.registerTestDefinition(aged);
					auto mesh = std::make_unique<renderer::TessellatedMesh>();
					if (!registry.buildMesh(defName, static_cast<uint32_t>(seed), *mesh)) {
						std::cerr << "error: " << defName << " at age " << age << " seed " << seed << " drew nothing\n";
						return kFailure;
					}
					std::cout << "  seed " << static_cast<uint32_t>(seed) << " age " << age << ": " << mesh->getTriangleCount() << " triangles, "
							  << mesh->getVertexCount() << " vertices\n";
					row.push_back(built.emplace_back(std::move(mesh)).get());
				}
			}
			registry.registerTestDefinition(original);
		}

		if (!initGl()) {
			std::cerr << "error: could not create an OpenGL context (need a GPU/display)\n";
			return kFailure;
		}
		const assets::RenderedImage image = assets::renderMeshSheet(rows, ppm, bg, silhouette);
		shutdownGl();
		if (image.pixels.empty() || !Renderer::writePngToFile(image.pixels.data(), image.width, image.height, out)) {
			std::cerr << "error: could not render " << defName << " to " << out << "\n";
			return kFailure;
		}
		std::cout << "wrote " << out << " (" << image.width << "x" << image.height << ")\n";
		return kOk;
	}

	int cmdRender(const std::vector<std::string>& args) {
		const auto defName = positional(args);
		if (!defName) {
			std::cerr << "error: render needs a defName\n";
			return kUsage;
		}
		const auto out = flagValue(args, "--out");
		if (!out) {
			std::cerr << "error: render needs --out <file.png>\n";
			return kUsage;
		}
		if (hasFlag(args, "--grid") || flagValue(args, "--age") || hasFlag(args, "--silhouette")) {
			return renderSheet(*defName, *out, args);
		}

		int w = 256;
		int h = 256;
		if (const auto size = flagValue(args, "--size"); size && !parseSize(*size, w, h)) {
			std::cerr << "error: --size must be WxH, e.g. 256x256\n";
			return kUsage;
		}
		const Foundation::Color bg = parseColor(flagValue(args, "--bg"));

		uint32_t seed = 0U;
		if (const auto seedArg = flagValue(args, "--seed")) {
			try {
				seed = static_cast<uint32_t>(std::stoul(*seedArg));
			} catch (const std::exception&) {
				std::cerr << "error: --seed must be a non-negative integer\n";
				return kUsage;
			}
		}

		int samples = 1;
		if (const auto samplesArg = flagValue(args, "--samples")) {
			try {
				samples = std::max(1, std::stoi(*samplesArg));
			} catch (const std::exception&) {
				std::cerr << "error: --samples must be an integer\n";
				return kUsage;
			}
		}

		if (!loadLibrary()) {
			return kFailure;
		}
		if (AssetRegistry::Get().getDefinition(*defName) == nullptr) {
			std::cerr << "error: no such defName: " << *defName << "\n";
			return kNotFound;
		}

		if (!initGl()) {
			std::cerr << "error: could not create an OpenGL context (need a GPU/display)\n";
			return kFailure;
		}

		bool ok = true;
		if (samples <= 1) {
			ok = assets::renderToPng(*defName, *out, w, h, bg, seed);
			if (ok) {
				std::cout << "wrote " << *out << "\n";
			}
		} else {
			for (int i = 0; i < samples; ++i) {
				const std::string path = insertSuffix(*out, i);
				if (assets::renderToPng(*defName, path, w, h, bg, seed + static_cast<uint32_t>(i))) {
					std::cout << "wrote " << path << "\n";
				} else {
					ok = false;
				}
			}
		}

		shutdownGl();
		return ok ? kOk : kFailure;
	}

} // namespace

int main(int argc, char** argv) {
	// Keep stdout clean for machine-readable output: route nothing below Error
	// through the logger (it writes to stdout). Results go through std::cout.
	foundation::Logger::initialize();
	for (int c = 0; c < static_cast<int>(foundation::LogCategory::Count); ++c) {
		foundation::Logger::setLevel(static_cast<foundation::LogCategory>(c), foundation::LogLevel::Error);
	}

	const std::vector<std::string> all(argv + 1, argv + argc);
	if (all.empty() || all[0] == "--help" || all[0] == "-h") {
		std::cout << kUsageText;
		return all.empty() ? kUsage : kOk;
	}

	const std::string			   command = all[0];
	const std::vector<std::string> args(all.begin() + 1, all.end());

	if (command == "list") {
		return cmdList(args);
	}
	if (command == "search") {
		return cmdSearch(args);
	}
	if (command == "inspect") {
		return cmdInspect(args);
	}
	if (command == "validate") {
		return cmdValidate(args);
	}
	if (command == "render") {
		return cmdRender(args);
	}

	std::cerr << "error: unknown command '" << command << "'\n\n" << kUsageText;
	return kUsage;
}
