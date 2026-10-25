#include "IClipboard.h"

#include <utils/Log.h>

#include <stdexcept>

namespace UI {

	IClipboard* IClipboard::instance = nullptr;

	IClipboard& IClipboard::Get() {
		if (instance == nullptr) {
			LOG_ERROR(UI, "IClipboard::Get() called before a clipboard was registered");
			throw std::runtime_error("IClipboard not initialized");
		}
		return *instance;
	}

	void IClipboard::setInstance(IClipboard* newInstance) {
		instance = newInstance;
		LOG_INFO(UI, "Clipboard singleton instance set");
	}

	IClipboard::~IClipboard() {
		if (instance == this) {
			instance = nullptr;
		}
	}

} // namespace UI
