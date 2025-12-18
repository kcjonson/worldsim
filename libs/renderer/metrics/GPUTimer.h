#pragma once

// GPUTimer - OpenGL GPU timing via GL_TIME_ELAPSED queries.
// Uses double-buffering since GPU results are only available after the frame completes.

#include <cstdint>

namespace Renderer {

/// GPU timer using OpenGL timer queries.
/// Results are double-buffered - you get the previous frame's time.
class GPUTimer {
  public:
	GPUTimer();
	~GPUTimer();

	// Non-copyable
	GPUTimer(const GPUTimer&) = delete;
	GPUTimer& operator=(const GPUTimer&) = delete;

	/// Enable/disable GPU timing (disabled by default to avoid driver overhead)
	void setEnabled(bool enabled) { m_enabled = enabled; }
	[[nodiscard]] bool isEnabled() const { return m_enabled; }

	/// Begin timing (call before rendering) - no-op if disabled or unsupported
	void begin();

	/// End timing (call after rendering) - no-op if disabled or unsupported
	void end();

	/// Get the GPU time in milliseconds (from previous frame)
	/// Returns 0.0 until at least one frame has completed
	[[nodiscard]] float getTimeMs() const { return m_lastTimeMs; }

	/// Check if GPU timer queries are supported on this platform
	[[nodiscard]] bool isSupported() const { return m_supported; }

  private:
	static constexpr int kQueryCount = 2; // Double-buffered

	uint32_t m_queries[kQueryCount]{};
	int m_currentQuery{0};
	float m_lastTimeMs{0.0F};
	bool m_supported{false};
	bool m_enabled{false}; // Disabled by default to avoid driver overhead
	bool m_inQuery{false};
	bool m_hasResult{false};
};

} // namespace Renderer
