#pragma once

// The App's clipboard as the ui's, until UI::IClipboard moves into salal's ui and app implements it.

#include <clipboard/Clipboard.h>
#include <clipboard/IClipboard.h>

#include <string>

namespace engine {

	class UiClipboard final : public UI::IClipboard {
	  public:
		explicit UiClipboard(app::Clipboard& appClipboard)
			: clipboard(appClipboard) {}

		std::string getText() const override { return clipboard.getText(); }
		void		setText(const std::string& text) override { clipboard.setText(text); }

	  private:
		app::Clipboard& clipboard;
	};

} // namespace engine
