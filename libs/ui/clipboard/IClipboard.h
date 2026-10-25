#pragma once

#include <string>

namespace UI {

	/**
	 * IClipboard
	 *
	 * Text clipboard service used by UI components (TextInput). ui defines the
	 * contract; the app layer supplies the platform implementation and registers
	 * it at startup (engine::Application registers engine::ClipboardManager).
	 * Singleton access mirrors FocusManager.
	 */
	class IClipboard {
	  public:
		static IClipboard& Get();
		static void		   setInstance(IClipboard* newInstance);

		IClipboard() = default;

		// Unregisters itself if it is the current instance, so implementations and
		// test fakes never leave a dangling pointer behind.
		virtual ~IClipboard();

		IClipboard(const IClipboard&) = delete;
		IClipboard& operator=(const IClipboard&) = delete;
		IClipboard(IClipboard&&) = delete;
		IClipboard& operator=(IClipboard&&) = delete;

		virtual std::string getText() const = 0;
		virtual void		setText(const std::string& text) = 0;

	  private:
		static IClipboard* instance;
	};

} // namespace UI
