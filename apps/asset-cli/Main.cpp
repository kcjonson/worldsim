// asset-cli: headless command-line surface for the asset library.
//
// Commands: list, search, inspect, validate, render. Everything is
// machine-readable with --json. No window and no debug server, so many
// invocations can run in parallel (agent loops, CI matrices).

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/AssetRenderer.h"
#include "assets/AssetValidator.h"

#include "graphics/Color.h"
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
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using engine::assets::AssetDefinition;
using engine::assets::AssetRegistry;
using engine::assets::AssetType;
using engine::assets::Severity;
using engine::assets::ValidationReport;
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
		"  render <defName> --out <file.png> [--size WxH] [--bg r,g,b,a] [--seed N] [--samples N]\n";

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
		static const char* kValueFlags[] = {"--type", "--group", "--out", "--size", "--bg", "--seed", "--samples"};
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
		glewExperimental = GL_TRUE;
		if (glewInit() != GLEW_OK) {
			glfwDestroyWindow(g_window);
			glfwTerminate();
			g_window = nullptr;
			return false;
		}
		// Fail loudly if the uber shader isn't reachable, rather than letting the
		// render silently produce a blank (shader-less) image.
		if (Foundation::findResourceString("shaders/uber.vert").empty()) {
			std::cerr << "error: could not find shaders/ next to the executable; cannot render\n";
			glfwDestroyWindow(g_window);
			glfwTerminate();
			g_window = nullptr;
			return false;
		}
		Renderer::Primitives::init(nullptr);
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
				renderer::TessellatedMesh mesh;
				if (!AssetRegistry::Get().buildMesh(name, 0, mesh)) {
					report.add(Severity::Error, name, "render", "Asset produced no geometry (render smoke test)", "");
				}
			}
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
			ok = engine::assets::renderToPng(*defName, *out, w, h, bg, seed);
			if (ok) {
				std::cout << "wrote " << *out << "\n";
			}
		} else {
			for (int i = 0; i < samples; ++i) {
				const std::string path = insertSuffix(*out, i);
				if (engine::assets::renderToPng(*defName, path, w, h, bg, seed + static_cast<uint32_t>(i))) {
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
