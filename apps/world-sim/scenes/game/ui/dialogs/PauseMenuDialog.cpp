#include "PauseMenuDialog.h"

#include <ui/Context.h>
#include <ui/components/ListRow.h>

#include <string>
#include <utility>

namespace world_sim {

	namespace {

		constexpr float kDialogWidth = 420.0F;
		constexpr float kRowHeight = 44.0F;

	} // namespace

	PauseMenuDialog::PauseMenuDialog(const Args& args)
		: onClose(args.onClose),
		  onMainMenu(args.onMainMenu),
		  onQuitGame(args.onQuitGame) {}

	void PauseMenuDialog::open(ui::Component& opener, int player) {
		if (isOpen()) {
			return;
		}
		dialog = ui::Dialog::open(
			opener,
			{
				.id = "pause_menu",
				.title = "Menu",
				.kicker = "Paused",
				.panelSize = {kDialogWidth, ui::kAuto},
				.onClose =
					[this] {
						dialog = {};
						if (onClose) {
							onClose();
						}
					},
			},
			player
		);
		if (ui::Dialog* shown = dialog.get()) {
			build(*shown);
		}
	}

	void PauseMenuDialog::close() {
		if (ui::Dialog* shown = dialog.get()) {
			ui::Context&			  context = *shown->getContext();
			const ui::Ref<ui::Dialog> entry = dialog;
			dialog = {};
			context.closeOverlay(entry);
		}
	}

	void PauseMenuDialog::build(ui::Dialog& shown) {
		const ui::Ref<ui::Component> rows = shown.content().addChild<ui::Component>({
			.layout = {.display = ui::Display::Flex, .flexDirection = ui::FlexDirection::Column, .gap = 2.0F},
		});
		// A row with no action is disabled, and dimmed, since ListRow draws disabled the same as enabled.
		const auto row = [&rows](const char* id, const char* label, const char* hint, const char* icon, std::function<void()> action) {
			const bool available = action != nullptr;
			rows.addChild<ui::ListRow>({
				.id = id,
				.enabled = available,
				.size = {ui::percent(100.0F), kRowHeight},
				.label = label,
				.trailing = hint,
				.icon = icon,
				.dim = !available,
				.onPress = std::move(action),
			});
		};
		// First, so the focus the dialog moves in lands here: Space, the pause key, resumes rather than
		// pressing Main Menu.
		row("pause_menu_resume", "Resume", "Back to the colony", "play", [this] {
			if (ui::Dialog* shown = dialog.get()) {
				shown->close();
			}
		});
		row("pause_menu_save", "Save", "Not available yet", "save", nullptr);
		row("pause_menu_load", "Load", "Not available yet", "list", nullptr);
		row("pause_menu_main_menu", "Main Menu", "Unsaved progress is lost", "home", onMainMenu);
		row("pause_menu_quit", "Quit Game", "Quit to desktop", "close", onQuitGame);
	}

} // namespace world_sim
