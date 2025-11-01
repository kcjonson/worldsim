#pragma once

// Demo interface for UI Sandbox component testing
// Each demo showcases different UI components and rendering features

namespace demo {

	// Initialize the demo (called once at startup)
	void Init();

	// Render the demo frame
	void Render();

	// Cleanup demo resources
	void Shutdown();

} // namespace demo
