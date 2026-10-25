#pragma once

#include <clipboard/IClipboard.h>

#include <GLFW/glfw3.h>

#include <string>

namespace engine {

	/**
	 * ClipboardManager
	 *
	 * GLFW-backed UI::IClipboard. Owned by Application, which registers it via
	 * UI::IClipboard::setInstance().
	 */
	class ClipboardManager final : public UI::IClipboard {
	  public:
		explicit ClipboardManager(GLFWwindow* window);

		ClipboardManager(const ClipboardManager&) = delete;
		ClipboardManager& operator=(const ClipboardManager&) = delete;
		ClipboardManager(ClipboardManager&&) = delete;
		ClipboardManager& operator=(ClipboardManager&&) = delete;

		std::string getText() const override;
		void		setText(const std::string& text) override;

	  private:
		GLFWwindow* window;
	};

} // namespace engine
