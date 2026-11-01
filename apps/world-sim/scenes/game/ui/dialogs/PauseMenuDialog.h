#pragma once

// PauseMenuDialog - the game's menu, a modal ui::Dialog opened from the top bar's Menu button or Escape:
// Resume, Save, Load, Main Menu and Quit Game, one row each. Save and Load stay disabled until saving
// exists. The scene holds the world still while it is open.

#include <ui/components/Dialog.h>

#include <functional>

namespace world_sim {

	class PauseMenuDialog {
	  public:
		struct Args {
			// The player closed it: Resume, its close button, cancel or a press on the scrim. Not called by
			// close().
			std::function<void()> onClose;
			std::function<void()> onMainMenu;
			std::function<void()> onQuitGame;
		};

		explicit PauseMenuDialog(const Args& args);

		// Its rows' callbacks hold it.
		PauseMenuDialog(const PauseMenuDialog&) = delete;
		PauseMenuDialog& operator=(const PauseMenuDialog&) = delete;
		PauseMenuDialog(PauseMenuDialog&&) = delete;
		PauseMenuDialog& operator=(PauseMenuDialog&&) = delete;
		~PauseMenuDialog() = default;

		// Opens it from `opener`; already open, it stays as it is.
		void open(ui::Component& opener, int player = 0);
		void close();

		[[nodiscard]] bool isOpen() const { return dialog.get() != nullptr; }

	  private:
		void build(ui::Dialog& shown);

		std::function<void()> onClose;
		std::function<void()> onMainMenu;
		std::function<void()> onQuitGame;

		ui::Ref<ui::Dialog> dialog;
	};

} // namespace world_sim
