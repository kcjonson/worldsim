import { useState } from "react";
import type { ReactNode } from "react";
import type { ScreenProps } from "../types";
import {
	Avatar,
	Badge,
	Button,
	Divider,
	Icon,
	KeyCap,
	Meter,
	Panel,
	SegmentedControl,
	Slider,
	Stat,
	Tabs,
	Tooltip,
} from "../../design-system";
import type { TabItem } from "../../design-system";
import styles from "./Components.module.css";

// ---- local state types --------------------------------------------------------

type ViewMode = "grid" | "list" | "map";
type TabId = "overview" | "detail" | "log";

// ---- section wrapper ----------------------------------------------------------

function Section({ title, kicker, children }: { title: string; kicker?: string; children: ReactNode }) {
	return (
		<Panel title={title} kicker={kicker} corners accent="accent">
			{children}
		</Panel>
	);
}

// ==============================================================================

export function Components(_props: ScreenProps) {
	const [sliderA, setSliderA] = useState(62);
	const [sliderB, setSliderB] = useState(0.4);
	const [viewMode, setViewMode] = useState<ViewMode>("grid");
	const [activeTab, setActiveTab] = useState<TabId>("overview");

	const tabs: TabItem<TabId>[] = [
		{ value: "overview", label: "Overview", icon: "globe" },
		{ value: "detail", label: "Detail", icon: "search" },
		{ value: "log", label: "Log", icon: "list" },
	];

	return (
		<div className={styles.root}>
			<div className={styles.column}>

				{/* PAGE HEADER */}
				<header className={styles.pageHead}>
					<div className={styles.pageKicker}>// Design System</div>
					<h1 className={styles.pageTitle}>Component Gallery</h1>
					<p className={styles.pageSubtitle}>
						Living style guide for the World-Sim UI. Switch themes from the left rail to see every token adapt.
						All three themes (deepspace / imperial / cockpit) share the same token names.
					</p>
				</header>

				{/* ================================================================
				  * 1. COLOR TOKENS
				  * ============================================================== */}
				<Section title="Color Tokens" kicker="// tokens.css">
					<div className={styles.tokenGroups}>

						<div className={styles.tokenGroup}>
							<div className={styles.tokenGroupLabel}>Accent</div>
							<div className={styles.swatchRow}>
								{[
									["--accent", "accent"],
									["--accent-bright", "accent-bright"],
									["--accent-dim", "accent-dim"],
									["--accent-glow", "accent-glow"],
								].map(([token, name]) => (
									<div key={token} className={styles.swatch}>
										<div className={styles.swatchChip} style={{ background: `var(${token})` }} />
										<code className={styles.swatchLabel}>{name}</code>
									</div>
								))}
							</div>
						</div>

						<div className={styles.tokenGroup}>
							<div className={styles.tokenGroupLabel}>Data</div>
							<div className={styles.swatchRow}>
								{[
									["--data", "data"],
									["--data-bright", "data-bright"],
									["--data-dim", "data-dim"],
									["--data-glow", "data-glow"],
								].map(([token, name]) => (
									<div key={token} className={styles.swatch}>
										<div className={styles.swatchChip} style={{ background: `var(${token})` }} />
										<code className={styles.swatchLabel}>{name}</code>
									</div>
								))}
							</div>
						</div>

						<div className={styles.tokenGroup}>
							<div className={styles.tokenGroupLabel}>Status</div>
							<div className={styles.swatchRow}>
								{[
									["--status-ok", "status-ok"],
									["--status-warn", "status-warn"],
									["--status-crit", "status-crit"],
									["--status-info", "status-info"],
								].map(([token, name]) => (
									<div key={token} className={styles.swatch}>
										<div className={styles.swatchChip} style={{ background: `var(${token})` }} />
										<code className={styles.swatchLabel}>{name}</code>
									</div>
								))}
							</div>
						</div>

						<div className={styles.tokenGroup}>
							<div className={styles.tokenGroupLabel}>Backgrounds</div>
							<div className={styles.swatchRow}>
								{[
									["--bg-void", "bg-void"],
									["--bg-base", "bg-base"],
									["--bg-panel", "bg-panel"],
									["--bg-panel-raised", "bg-panel-raised"],
									["--bg-inset", "bg-inset"],
									["--bg-hover", "bg-hover"],
									["--bg-active", "bg-active"],
								].map(([token, name]) => (
									<div key={token} className={styles.swatch}>
										<div
											className={styles.swatchChip}
											style={{ background: `var(${token})`, border: "1px solid var(--line-edge)" }}
										/>
										<code className={styles.swatchLabel}>{name}</code>
									</div>
								))}
							</div>
						</div>

						<div className={styles.tokenGroup}>
							<div className={styles.tokenGroupLabel}>Text</div>
							<div className={styles.swatchRow}>
								{[
									["--text-bright", "text-bright"],
									["--text", "text"],
									["--text-dim", "text-dim"],
									["--text-faint", "text-faint"],
								].map(([token, name]) => (
									<div key={token} className={styles.swatch}>
										<div className={styles.swatchChip} style={{ background: `var(${token})` }} />
										<code className={styles.swatchLabel}>{name}</code>
									</div>
								))}
							</div>
						</div>

						<div className={styles.tokenGroup}>
							<div className={styles.tokenGroupLabel}>Lines</div>
							<div className={styles.swatchRow}>
								{[
									["--line-hairline", "line-hairline"],
									["--line-edge", "line-edge"],
									["--line-strong", "line-strong"],
								].map(([token, name]) => (
									<div key={token} className={styles.swatch}>
										<div
											className={styles.swatchChip}
											style={{ background: `var(${token})`, border: "1px solid var(--line-hairline)" }}
										/>
										<code className={styles.swatchLabel}>{name}</code>
									</div>
								))}
							</div>
						</div>

					</div>
				</Section>

				{/* ================================================================
				  * 2. TYPOGRAPHY
				  * ============================================================== */}
				<Section title="Typography" kicker="// fonts + scale">
					<div className={styles.typeGroups}>

						<div className={styles.typeGroup}>
							<div className={styles.tokenGroupLabel}>Families</div>
							<div className={styles.typeFamilies}>
								<div>
									<div className={styles.typeFamilyName}>--font-display</div>
									<div style={{ fontFamily: "var(--font-display)", fontSize: "var(--fs-xl)", color: "var(--text-bright)" }}>
										Chakra Petch — HUD Signage
									</div>
								</div>
								<div>
									<div className={styles.typeFamilyName}>--font-ui</div>
									<div style={{ fontFamily: "var(--font-ui)", fontSize: "var(--fs-md)", color: "var(--text)" }}>
										Barlow — Body &amp; Labels
									</div>
								</div>
								<div>
									<div className={styles.typeFamilyName}>--font-mono</div>
									<div style={{ fontFamily: "var(--font-mono)", fontSize: "var(--fs-sm)", color: "var(--data)" }}>
										JetBrains Mono — Data Readouts
									</div>
								</div>
							</div>
						</div>

						<Divider label="Type Scale" />

						<div className={styles.typeScale}>
							{([
								["--fs-3xl", "3xl", "38px"],
								["--fs-2xl", "2xl", "28px"],
								["--fs-xl", "xl", "22px"],
								["--fs-lg", "lg", "18px"],
								["--fs-md", "md", "15px"],
								["--fs-base", "base", "13px"],
								["--fs-sm", "sm", "12px"],
								["--fs-xs", "xs", "11px"],
								["--fs-2xs", "2xs", "10px"],
							] as const).map(([token, name, px]) => (
								<div key={token} className={styles.typeScaleRow}>
									<code className={styles.typeToken}>{name}</code>
									<span className={styles.typePx}>{px}</span>
									<span style={{ fontSize: `var(${token})`, color: "var(--text-bright)", lineHeight: 1.2 }}>
										Aa — used-future
									</span>
								</div>
							))}
						</div>

					</div>
				</Section>

				{/* ================================================================
				  * 3. BUTTONS
				  * ============================================================== */}
				<Section title="Buttons" kicker="// Button">

					<div className={styles.btnSection}>
						<div className={styles.tokenGroupLabel}>Variants × Sizes</div>
						<div className={styles.btnMatrix}>
							{(["primary", "secondary", "ghost", "danger", "data"] as const).map((variant) => (
								<div key={variant} className={styles.btnRow}>
									<code className={styles.btnVariantLabel}>{variant}</code>
									{(["sm", "md", "lg"] as const).map((size) => (
										<Button key={size} variant={variant} size={size}>
											{variant} {size}
										</Button>
									))}
								</div>
							))}
						</div>
					</div>

					<Divider />

					<div className={styles.btnSection}>
						<div className={styles.tokenGroupLabel}>Special</div>
						<div className={styles.btnSpecialRow}>
							<Button variant="primary" icon="rocket" size="md">
								Icon Left
							</Button>
							<Button variant="secondary" iconRight="arrowRight" size="md">
								Icon Right
							</Button>
							<Button variant="ghost" icon="gear" iconOnly size="md" aria-label="Settings" />
							<Button variant="data" icon="crosshair" iconOnly size="lg" aria-label="Target" />
							<Button variant="primary" stencil size="md">
								Stencil
							</Button>
							<Button variant="secondary" size="md" disabled>
								Disabled
							</Button>
						</div>
					</div>

					<Divider />

					<div className={styles.btnSection}>
						<div className={styles.tokenGroupLabel}>Block</div>
						<Button variant="primary" icon="play" block size="lg">
							Begin Expedition
						</Button>
					</div>

				</Section>

				{/* ================================================================
				  * 4. PANELS
				  * ============================================================== */}
				<Section title="Panels" kicker="// Panel">
					<div className={styles.panelGrid}>

						<Panel variant="panel" accent="accent" title="Panel / Accent" kicker="variant=panel">
							<p className={styles.panelBody}>Default panel surface with amber accent hairline.</p>
						</Panel>

						<Panel variant="raised" accent="accent" title="Raised / Accent" kicker="variant=raised">
							<p className={styles.panelBody}>Elevated surface — use for modals or floating cards.</p>
						</Panel>

						<Panel variant="inset" accent="accent" title="Inset / Accent" kicker="variant=inset">
							<p className={styles.panelBody}>Sunken surface — use for code blocks or data wells.</p>
						</Panel>

						<Panel variant="panel" accent="data" title="Panel / Data" kicker="accent=data">
							<p className={styles.panelBody}>Same panel, teal data accent instead.</p>
						</Panel>

						<Panel variant="raised" accent="data" title="No Corners" kicker="corners=false" corners={false}>
							<p className={styles.panelBody}>Corner tick marks suppressed.</p>
						</Panel>

						<Panel variant="panel" accent="none" title="Accent None" kicker="accent=none">
							<p className={styles.panelBody}>No accent color; bare line-edge border only.</p>
						</Panel>

						<Panel
							variant="panel"
							accent="accent"
							title="Scanlines + Glow"
							kicker="scanlines glow"
							scanlines
							glow
						>
							<p className={styles.panelBody}>Texture overlay + accent glow on the border.</p>
						</Panel>

						<Panel
							variant="panel"
							accent="data"
							title="With Actions"
							kicker="actions slot"
							actions={
								<>
									<Button variant="ghost" icon="refresh" iconOnly size="sm" aria-label="Refresh" />
									<Button variant="ghost" icon="close" iconOnly size="sm" aria-label="Close" />
								</>
							}
						>
							<p className={styles.panelBody}>Actions slot in the panel header.</p>
						</Panel>

					</div>
				</Section>

				{/* ================================================================
				  * 5. DATA DISPLAY
				  * ============================================================== */}
				<Section title="Data Display" kicker="// Stat Meter Slider Badge KeyCap Avatar Tooltip">

					{/* STATS */}
					<div className={styles.subHead}>Stat</div>
					<div className={styles.statGrid}>
						<Stat label="HULL INTEGRITY" value="94" unit="%" tone="ok" size="lg" />
						<Stat label="SHIELD" value="41" unit="%" tone="warn" size="lg" />
						<Stat label="REACTOR" value="12" unit="%" tone="crit" size="lg" />
						<Stat label="CREW" value="1,204" tone="data" size="md" />
						<Stat label="FOOD" value="38.2" unit="t" tone="accent" size="md" />
						<Stat label="WATER" value="61.7" unit="kL" tone="default" size="sm" align="right" />
					</div>

					<Divider label="Meter" />

					<div className={styles.meterGroup}>
						<Meter value={0.82} label="Hull Integrity" valueText="82%" tone="accent" />
						<Meter value={0.55} label="Shield Output" valueText="55%" tone="data" />
						<Meter value={0.91} label="Life Support" valueText="91%" tone="ok" />
						<Meter value={0.48} label="Fuel Reserve" valueText="48%" tone="warn" />
						<Meter value={0.15} label="Reactor Core" valueText="15%" tone="crit" />
						<Meter value={0.15} label="Auto (crit)" valueText="15%" tone="auto" />
						<Meter value={0.48} label="Auto (warn)" valueText="48%" tone="auto" />
						<Meter value={0.82} label="Auto (ok)" valueText="82%" tone="auto" />
						<Meter value={0.6} label="Segmented" valueText="60%" tone="accent" segmented size="sm" />
					</div>

					<Divider label="Slider" />

					<div className={styles.sliderGroup}>
						<Slider
							label="Population Cap"
							value={sliderA}
							min={0}
							max={100}
							step={1}
							unit="%"
							detent={0.5}
							onChange={setSliderA}
						/>
						<Slider
							label="Gravity Scale"
							value={sliderB}
							min={0}
							max={2}
							step={0.01}
							detent={0.5}
							format={(v) => `${v.toFixed(2)}g`}
							onChange={setSliderB}
						/>
					</div>

					<Divider label="SegmentedControl + Tabs" />

					<div className={styles.controlsRow}>
						<div>
							<div className={styles.subLabel}>SegmentedControl — tone=accent</div>
							<SegmentedControl<ViewMode>
								value={viewMode}
								onChange={setViewMode}
								options={[
									{ value: "grid", label: "Grid", icon: "layers" },
									{ value: "list", label: "List", icon: "list" },
									{ value: "map", label: "Map", icon: "map" },
								]}
								tone="accent"
							/>
						</div>
						<div>
							<div className={styles.subLabel}>SegmentedControl — tone=data, size=sm</div>
							<SegmentedControl<ViewMode>
								value={viewMode}
								onChange={setViewMode}
								options={[
									{ value: "grid", label: "Grid" },
									{ value: "list", label: "List" },
									{ value: "map", label: "Map" },
								]}
								tone="data"
								size="sm"
							/>
						</div>
					</div>

					<div className={styles.tabsWrap}>
						<div className={styles.subLabel}>Tabs</div>
						<Tabs<TabId>
							tabs={tabs}
							value={activeTab}
							onChange={setActiveTab}
						/>
						<div className={styles.tabContent}>
							{activeTab === "overview" && <span>Overview panel content</span>}
							{activeTab === "detail" && <span>Detail panel content</span>}
							{activeTab === "log" && <span>Activity log content</span>}
						</div>
					</div>

					<Divider label="Badge" />

					<div className={styles.badgeRow}>
						<Badge tone="default">default</Badge>
						<Badge tone="accent">accent</Badge>
						<Badge tone="data">data</Badge>
						<Badge tone="ok">ok</Badge>
						<Badge tone="warn">warn</Badge>
						<Badge tone="crit">crit</Badge>
						<Badge tone="outline">outline</Badge>
						<Badge tone="accent" dot>dot</Badge>
						<Badge tone="ok" icon="check">icon</Badge>
						<Badge tone="crit" icon="alert">alert</Badge>
					</div>

					<Divider label="KeyCap" />

					<div className={styles.keyRow}>
						<KeyCap>ESC</KeyCap>
						<KeyCap>TAB</KeyCap>
						<KeyCap>SPACE</KeyCap>
						<KeyCap>ENTER</KeyCap>
						<KeyCap>W</KeyCap>
						<KeyCap>A</KeyCap>
						<KeyCap>S</KeyCap>
						<KeyCap>D</KeyCap>
						<KeyCap>F1</KeyCap>
						<KeyCap>CTRL</KeyCap>
					</div>

					<Divider label="Divider" />

					<Divider />
					<Divider label="SECTION BREAK" />
					<Divider />

					<Divider label="Tooltip" />

					<div className={styles.tooltipRow}>
						<Tooltip content="This is a tooltip — top (default)" side="top">
							<Button variant="secondary" size="sm">Hover me (top)</Button>
						</Tooltip>
						<Tooltip content="Tooltip on the right" side="right">
							<Button variant="ghost" size="sm">Hover me (right)</Button>
						</Tooltip>
						<Tooltip content="Tooltip on the bottom" side="bottom">
							<Button variant="ghost" size="sm">Hover me (bottom)</Button>
						</Tooltip>
						<Tooltip content="Tooltip on the left" side="left">
							<Button variant="data" size="sm">Hover me (left)</Button>
						</Tooltip>
					</div>

					<Divider label="Avatar" />

					<div className={styles.avatarGrid}>
						{[
							{ seed: "Kai Okafor", mood: 0.9 },
							{ seed: "Zara Vance", mood: 0.6 },
							{ seed: "Dmitri Sol", mood: 0.4 },
							{ seed: "Noa Harbin", mood: 0.15 },
							{ seed: "Iris Tane", mood: undefined },
							{ seed: "Rex Odin", mood: 0.8 },
							{ seed: "Mira Chen", mood: 0.5, selected: true },
							{ seed: "Vale Ford", mood: 0.72 },
						].map(({ seed, mood, selected }) => (
							<div key={seed} className={styles.avatarCell}>
								<Avatar seed={seed} size={48} mood={mood} selected={selected} />
								<span className={styles.avatarName}>{seed}</span>
								{mood !== undefined && (
									<span className={styles.avatarMood} style={{
										color: mood < 0.3 ? "var(--status-crit)" : mood < 0.55 ? "var(--status-warn)" : "var(--status-ok)"
									}}>
										{Math.round(mood * 100)}%
									</span>
								)}
							</div>
						))}
					</div>

				</Section>

				{/* ================================================================
				  * 6. ICONS
				  * ============================================================== */}
				<Section title="Icons" kicker="// Icon — all glyphs">
					<div className={styles.iconGrid}>
						{([
							"play","pause","fast","veryFast","plus","minus","close","gear","menu",
							"chevronLeft","chevronRight","chevronUp","chevronDown","arrowRight","check",
							"alert","info","globe","crosshair","user","users","heart","food","water",
							"energy","rest","hammer","box","leaf","search","lock","dice","sprout",
							"mountain","temp","rain","map","home","rocket","star","skull","refresh",
							"eye","clock","list","layers","save","bolt",
						] as const).map((name) => (
							<div key={name} className={styles.iconCell}>
								<Icon name={name} size={20} />
								<code className={styles.iconName}>{name}</code>
							</div>
						))}
					</div>
				</Section>

				{/* FOOTER */}
				<footer className={styles.pageFooter}>
					<code>// World-Sim Design System — all tokens sourced from tokens.css</code>
				</footer>

			</div>
		</div>
	);
}
