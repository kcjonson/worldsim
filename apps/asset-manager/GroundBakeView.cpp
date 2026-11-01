#include "GroundBakeView.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <threading/TaskPool.h>
#include <ui/components/Button.h>
#include <ui/shapes/Text.h>
#include <ui/theme/Tokens.h>
#include <utils/Log.h>
#include <world/rendering/GroundTileBaker.h>
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

		constexpr ui::Layout kKeepHeight{.flexShrink = 0.0F};
		constexpr ui::Layout kHug{.flexShrink = 0.0F, .alignSelf = ui::AlignItems::Start};

		std::unordered_map<std::string, std::shared_ptr<worldview::GroundTileBake>>& cache() {
			static std::unordered_map<std::string, std::shared_ptr<worldview::GroundTileBake>> bakes;
			return bakes;
		}

		// The filters' rows run here; the asset-manager has no other pool.
		foundation::TaskPool& pool() {
			static foundation::TaskPool workers;
			return workers;
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
		variantCount = std::max(1U, def->variantCount);
		if (const auto cached = cache().find(defName); cached != cache().end()) {
			bake = cached->second;
			showVariant();
			showZoomStrip();
			showReport();
			return;
		}
		bake = std::make_shared<worldview::GroundTileBake>();
		bake->defName = defName;
		bake->periodMeters = def->ground.periodMeters;
		bake->pixelsPerMeter = def->ground.pixelsPerMeter;
		bake->layerSize = worldview::GroundTileBaker::layerSize(*def);
		baking = true;
		status.get()->setText(format("Baking variant 1 of %u...", variantCount));
		requestUpdate();
	}

	// One variant a frame, so the window keeps drawing between them.
	void GroundBakeView::update(float /*dt*/) {
		if (!baking) {
			return;
		}
		const worldview::GroundTileBaker			baker({.restoreCoordinateSystem = coordinateSystem, .pool = &pool()});
		const auto									next = static_cast<uint32_t>(bake->variants.size());
		std::optional<worldview::GroundVariantBake> variant = baker.bakeVariant(defName, next);
		if (!variant.has_value()) {
			baking = false;
			status.get()->setText(format("Bake failed at variant %u; the log says why", next));
			return;
		}
		bake->variants.push_back(std::move(*variant));
		if (bake->variants.size() == 1) {
			showVariant();
		}
		if (bake->variants.size() < variantCount) {
			status.get()->setText(format("Baking variant %zu of %u...", bake->variants.size() + 1, variantCount));
			requestUpdate();
			return;
		}

		baking = false;
		bake->metrics = worldview::GroundTileBaker::measure(bake->variants);
		bake->totalMs = 0.0;
		for (const worldview::GroundVariantBake& v : bake->variants) {
			bake->totalMs += v.totalMs;
		}
		cache()[defName] = bake;
		const std::string text = worldview::GroundTileBaker::formatReport(*bake);
		for (size_t begin = 0; begin < text.size();) {
			const size_t end = std::min(text.find('\n', begin), text.size());
			LOG_INFO(Engine, "%s", text.substr(begin, end - begin).c_str());
			begin = end + 1;
		}
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
		std::vector<TileImageStrip::Image>	images;
		for (size_t k = 0; k < variant.mips.size(); ++k) {
			const worldview::GroundMip& mip = variant.mips[k];
			images.push_back({.width = mip.size, .height = mip.size, .rgba = mip.rgba, .caption = format("mip %zu\n%d px", k, mip.size)});
		}
		mips.get()->setImages(std::move(images));
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
						const float straight = c.a > 0.0F ? c[ch] / c.a : 0.0F;
						out[ch] = static_cast<uint8_t>(std::lround(ground::linearToSrgb(straight) * 255.0F));
					}
					out[3] = static_cast<uint8_t>(std::lround(std::clamp(c.a, 0.0F, 1.0F) * 255.0F));
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
