#include "ClipboardManager.h"

namespace engine {

ClipboardManager::ClipboardManager(GLFWwindow* window)
	: window(window) {
}

std::string ClipboardManager::getText() const {
	if (window == nullptr) {
		return "";
	}

	const char* text = glfwGetClipboardString(window);
	if (text == nullptr) {
		return "";
	}

	return std::string(text);
}

void ClipboardManager::setText(const std::string& text) {
	if (window == nullptr) {
		return;
	}

	glfwSetClipboardString(window, text.c_str());
}

} // namespace engine
