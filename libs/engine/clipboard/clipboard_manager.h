#pragma once

#include <GLFW/glfw3.h>
#include <string>

namespace engine {

/**
 * ClipboardManager
 *
 * Centralized clipboard handling system that abstracts platform clipboard access.
 * Uses singleton pattern for global access (like InputManager).
 *
 * Responsibilities:
 * - Provide platform-agnostic clipboard get/set API
 * - Abstract GLFW clipboard calls
 */
class ClipboardManager {
  public:
	// Singleton access
	static ClipboardManager& Get();
	static void				 SetInstance(ClipboardManager* instance);

	explicit ClipboardManager(GLFWwindow* window);
	~ClipboardManager();

	// Disable copy/move
	ClipboardManager(const ClipboardManager&) = delete;
	ClipboardManager& operator=(const ClipboardManager&) = delete;
	ClipboardManager(ClipboardManager&&) = delete;
	ClipboardManager& operator=(ClipboardManager&&) = delete;

	// Clipboard API
	std::string GetText() const;
	void		SetText(const std::string& text);
	bool		HasText() const;

  private:
	// Singleton instance pointer
	static ClipboardManager* s_instance;

	// GLFW window reference for clipboard access
	GLFWwindow* window;
};

} // namespace engine
