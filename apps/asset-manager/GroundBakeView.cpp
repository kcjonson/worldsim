#include "GroundBakeView.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/AssetValidator.h>
#include <threading/TaskPool.h>
#include <ui/components/Button.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
#include <world/rendering/GroundTileBaker.h>
#include <world/rendering/GroundTileCache.h>
#include <world/rendering/GroundTileFilter.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <vector>

namespace asset_manager {

	namespace {

		namespace ground = worldview::ground;

		// Screen densities the zoom strip shows, px/m: the game's zoom runs 2 to 160, default 24.
		constexpr std::array<float, 8> kZoomDensities = {160.0F, 96.0F, 64.0F, 48.0F, 24.0F, 12.0F, 6.0F, 3.0F};
		// A panel shows the 2x2 of variants, two periods a side (8 m at the default 4), clipped to this.
		constexpr float kMaxPanel = 256.0F;

		const ui::Layout kKeepHeight{.flexShrink = 0.0F};
		const ui::Layout kHug{.flexShrink = 0.0F, .alignSelf = ui::AlignItems::Start};

		std::unordered_map<std::string, std::shared_ptr<worldview::GroundTileBake>>& cache() {
			static std::unordered_map<std::string, std::shared_ptr<worldview::GroundTileBake>> bakes;
			return bakes;
		}

		// The variants' paths and the filters' rows run here; the asset-manager has no other pool.
		foundation::TaskPool& pool() {
			static foundation::TaskPool workers;
			return workers;
		}

		const worldview::GroundTileCache& diskCache() {
			static const worldview::GroundTileCache bakes(worldview::GroundTileCache::defaultDirectory());
			return bakes;
		}

		std::vector<ground::LinearImage> decodeChain(const worldview::GroundVariantBake& variant) {
			std::vector<ground::LinearImage> chain;
			for (const worldview::GroundMip& mip : variant.mips) {
				chain.push_back(ground::decodeSrgb8({.width = mip.size, .height = mip.size, .rgba = mip.rgba}));
			}
			return chain;
		}

		std::string format(const char* pattern, auto... values) {
			char text[160];
			std::snprintf(text, sizeof(text), pattern, values...);
			return text;
		}

		int floorMod(int value, int divisor) {
			const int r = value % divisor;
			return r < 0 ? r + divisor : r;
		}

	} // namespace

	GroundBakeView::GroundBakeView(const Args& args)
		: defName(args.defName),
		  coordinateSystem(args.coordinateSystem) {}

	void GroundBakeView::clearCache() {
		cache().clear();
	}

	void GroundBakeView::onMount() {
		ui::Ref<ui::Component> header = addChild<ui::Component>({
			.layout = {
				.display = ui::Display::Flex,
				.flexDirection = ui::FlexDirection::Row,
				.gap = 12.0F,
				.alignItems = ui::AlignItems::Center,
				.flexShrink = 0.0F
			},
		});
		header.addChild<ui::Button>({
			.id = "am_bake_next_variant",
			.label = "Next variant",
			.controlSize = ui::ControlSize::Sm,
			.onPress = [this] {
				if (!baking && variantCount > 0) {
					shownVariant = (shownVariant + 1) % variantCount;
					showVariant();
				}
			},
		});
		status = header.addChild<ui::Text>({.id = "am_bake_status", .style = ui::styles::text::secondary});

		addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::label, .text = "MIPS, ONE TEXEL A PIXEL"});
		mips = addChild<TileImageStrip>({.id = "am_bake_mips", .layout = kHug});
		addChild<ui::Text>(
			{.layout = kKeepHeight, .style = ui::styles::label, .text = "HEIGHTS (ALPHA), BLACK LOW, WHITE HIGH, ONE TEXEL A PIXEL"}
		);
		heights = addChild<TileImageStrip>({.id = "am_bake_heights", .layout = kHug});
		addChild<ui::Text>(
			{.layout = kKeepHeight,
			 .style = ui::styles::label,
			 .text = "WHAT THE GAME SHOWS  -  VARIANTS 0 TO 3 IN A 2X2, LOD BIAS +0.25, TRILINEAR"}
		);
		zoom = addChild<TileImageStrip>({.id = "am_bake_zoom", .layout = kHug});
		addChild<ui::Text>({.layout = kKeepHeight, .style = ui::styles::label, .text = "METRICS"});
		report = addChild<ui::Text>({.id = "am_bake_report", .layout = kHug, .style = ui::styles::text::mono});

		const assets::AssetDefinition* def = assets::AssetRegistry::Get().getDefinition(defName);
		if (def == nullptr || def->role != assets::AssetRole::Ground) {
			status.get()->setText("Not a ground def");
			return;
		}
		// The game never bakes a def that fails validation, so neither does this.
		std::string errors;
		for (const assets::ValidationIssue& issue : assets::AssetRegistry::Get().getValidationReport().issues) {
			if (issue.defName == defName && issue.severity == assets::Severity::Error) {
				errors += "\n" + (issue.field.empty() ? std::string() : issue.field + ": ") + issue.message +
						  (issue.context.empty() ? std::string() : " (" + issue.context + ")");
			}
		}
		if (!errors.empty()) {
			status.get()->setText("Not baked: the def fails validation");
			report.get()->setText(errors.substr(1));
			return;
		}
		if (const auto cached = cache().find(defName); cached != cache().end()) {
			bake = cached->second;
			variantCount = bake->variantCount;
			showBake();
			return;
		}
		std::optional<worldview::GroundTileBake> begun = baker().begin(defName);
		if (!begun.has_value()) {
			status.get()->setText("Bake failed; the log says why");
			return;
		}
		bake = std::make_shared<worldview::GroundTileBake>(std::move(*begun));
		variantCount = bake->variantCount;
		if (bake->fromCache) {
			cache()[defName] = bake;
			showBake();
			return;
		}
		baking = true;
		status.get()->setText(format("Generating %u variants...", variantCount));
		requestUpdate();
	}

	worldview::GroundTileBaker GroundBakeView::baker() const {
		return worldview::GroundTileBaker({.restoreCoordinateSystem = coordinateSystem, .pool = &pool(), .cache = &diskCache()});
	}

	// Every variant's paths in one frame, then one variant's render a frame, so the window keeps drawing
	// between them.
	void GroundBakeView::update(float /*dt*/) {
		if (!baking) {
			return;
		}
		const worldview::GroundTileBaker groundBaker = baker();
		if (paths.empty()) {
			std::optional<std::vector<worldview::GroundVariantPaths>> built = groundBaker.buildPaths(*bake);
			if (!built.has_value()) {
				baking = false;
				status.get()->setText("Bake failed generating the variants; the log says why");
				return;
			}
			if (!groundBaker.bakeHeights(*bake, *built)) {
				baking = false;
				status.get()->setText("Bake failed drawing the heights; the log says why");
				return;
			}
			paths = std::move(*built);
			status.get()->setText(format("Baking variant 1 of %u...", variantCount));
			requestUpdate();
			return;
		}

		const size_t								next = bake->variants.size();
		std::optional<worldview::GroundVariantBake> variant = groundBaker.bakeVariant(*bake, paths[next]);
		if (!variant.has_value()) {
			baking = false;
			paths.clear();
			status.get()->setText(format("Bake failed at variant %zu; the log says why", next));
			return;
		}
		bake->variants.push_back(std::move(*variant));
		paths[next].paths = {};
		if (bake->variants.size() == 1) {
			showVariant();
		}
		if (bake->variants.size() < paths.size()) {
			status.get()->setText(format("Baking variant %zu of %u...", bake->variants.size() + 1, variantCount));
			requestUpdate();
			return;
		}

		baking = false;
		paths.clear();
		groundBaker.finish(*bake);
		cache()[defName] = bake;
		showBake();
	}

	void GroundBakeView::showBake() {
		showVariant();
		showZoomStrip();
		showReport();
	}

	void GroundBakeView::showVariant() {
		if (bake == nullptr || bake->variants.empty()) {
			return;
		}
		const size_t						index = std::min<size_t>(shownVariant, bake->variants.size() - 1);
		const worldview::GroundVariantBake& variant = bake->variants[index];
		std::vector<TileImageStrip::Image>	colorImages;
		std::vector<TileImageStrip::Image>	heightImages;
		for (size_t k = 0; k < variant.mips.size(); ++k) {
			const worldview::GroundMip& mip = variant.mips[k];
			TileImageStrip::Image		color{.width = mip.size, .height = mip.size, .rgba = mip.rgba, .caption = format("mip %zu\n%d px", k, mip.size)};
			TileImageStrip::Image		height{.width = mip.size, .height = mip.size, .rgba = mip.rgba, .caption = color.caption};
			for (size_t i = 0; i + 3 < mip.rgba.size(); i += 4) {
				color.rgba[i + 3] = 255; // alpha is the height, not coverage
				height.rgba[i] = height.rgba[i + 1] = height.rgba[i + 2] = mip.rgba[i + 3];
				height.rgba[i + 3] = 255;
			}
			colorImages.push_back(std::move(color));
			heightImages.push_back(std::move(height));
		}
		mips.get()->setImages(std::move(colorImages));
		heights.get()->setImages(std::move(heightImages));
		if (!baking) {
			status.get()->setText(format(
				"Variant %zu of %u, seed %08x  -  %.3g m at %u px/m  -  bake %.2f s",
				index + 1,
				variantCount,
				variant.seed,
				static_cast<double>(bake->periodMeters),
				bake->pixelsPerMeter,
				bake->totalMs / 1000.0
			));
		}
	}

	void GroundBakeView::showZoomStrip() {
		if (bake == nullptr || bake->variants.empty() || !(bake->periodMeters > 0.0F)) {
			return;
		}
		std::array<std::vector<ground::LinearImage>, 4> chains;
		for (size_t i = 0; i < chains.size(); ++i) {
			chains[i] = decodeChain(bake->variants[i % bake->variants.size()]);
		}
		const float texelsPerMeter = static_cast<float>(bake->pixelsPerMeter);
		const float period = bake->periodMeters;
		const int	levels = static_cast<int>(chains[0].size());

		std::vector<TileImageStrip::Image> images;
		for (const float density : kZoomDensities) {
			const float lod = ground::groundLod(texelsPerMeter, density, levels);
			const int	panel = static_cast<int>(std::min(2.0F * period * density, kMaxPanel));
			// Centered on the corner where the four variants meet, (period, period).
			const float origin = period - (0.5F * static_cast<float>(panel) / density);

			TileImageStrip::Image image{.width = panel, .height = panel, .rgba = {}, .caption = {}};
			image.rgba.resize(static_cast<size_t>(panel) * panel * 4);
			for (int py = 0; py < panel; ++py) {
				for (int px = 0; px < panel; ++px) {
					const float		wx = origin + ((static_cast<float>(px) + 0.5F) / density);
					const float		wy = origin + ((static_cast<float>(py) + 0.5F) / density);
					const int		tx = static_cast<int>(std::floor(wx / period));
					const int		ty = static_cast<int>(std::floor(wy / period));
					const size_t	chain = static_cast<size_t>(floorMod(tx, 2) + (2 * floorMod(ty, 2)));
					const float		u = (wx - (static_cast<float>(tx) * period)) * texelsPerMeter;
					const float		v = (wy - (static_cast<float>(ty) * period)) * texelsPerMeter;
					const glm::vec4 c = ground::sampleTrilinear(chains[chain], u, v, lod);
					uint8_t*		out = &image.rgba[((static_cast<size_t>(py) * panel) + static_cast<size_t>(px)) * 4];
					for (int ch = 0; ch < 3; ++ch) {
						out[ch] = static_cast<uint8_t>(std::lround(ground::linearToSrgb(c[ch]) * 255.0F));
					}
					out[3] = 255;
				}
			}
			const int lower = static_cast<int>(std::floor(lod));
			const int upper = std::min(lower + 1, levels - 1);
			image.caption = format(
				"%g px/m\nlod %.2f\n%s",
				static_cast<double>(density),
				static_cast<double>(lod),
				(upper == lower || lod == static_cast<float>(lower)) ? format("mip %d", lower).c_str()
																	 : format("mips %d+%d", lower, upper).c_str()
			);
			images.push_back(std::move(image));
		}
		zoom.get()->setImages(std::move(images));
	}

	void GroundBakeView::showReport() {
		if (bake == nullptr) {
			return;
		}
		report.get()->setText(worldview::GroundTileBaker::formatReport(*bake));
	}

} // namespace asset_manager
