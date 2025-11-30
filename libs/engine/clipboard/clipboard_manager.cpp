#include "clipboard_manager.h"
#include <stdexcept>
#include <utils/log.h>

namespace engine {

// Static member initialization
ClipboardManager* ClipboardManager::s_instance = nullptr;

ClipboardManager& ClipboardManager::Get() {
	if (s_instance == nullptr) {
		LOG_ERROR(Engine, "ClipboardManager::Get() called before ClipboardManager was created");
		throw std::runtime_error("ClipboardManager not initialized");
	}
	return *s_instance;
}

void ClipboardManager::SetInstance(ClipboardManager* instance) {
	s_instance = instance;
	LOG_INFO(Engine, "ClipboardManager singleton instance set");
}

ClipboardManager::ClipboardManager(GLFWwindow* window)
	: window(window) {
}

ClipboardManager::~ClipboardManager() {
	if (s_instance == this) {
		s_instance = nullptr;
	}
}

std::string ClipboardManager::GetText() const {
	if (window == nullptr) {
		return "";
	}

	const char* text = glfwGetClipboardString(window);
	if (text == nullptr) {
		return "";
	}

	return std::string(text);
}

void ClipboardManager::SetText(const std::string& text) {
	if (window == nullptr) {
		return;
	}

	glfwSetClipboardString(window, text.c_str());
}

bool ClipboardManager::HasText() const {
	if (window == nullptr) {
		return false;
	}

	const char* text = glfwGetClipboardString(window);
	return text != nullptr && text[0] != '\0';
}

} // namespace engine
