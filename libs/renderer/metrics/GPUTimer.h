#pragma once

// GPUTimer - GPU time of a span of GL commands, measured with a pair of
// GL_TIMESTAMP queries. Timestamps nest and overlap freely (only one
// GL_TIME_ELAPSED query can be active at a time), so a pass can be timed inside
// the frame's own span. Each span's result is read kFrames spans later, long
// after the GPU finished it, so reading never stalls the CPU.

#include "gl/GLQuery.h"
#include <array>

namespace Renderer {

class GPUTimer {
  public:
	GPUTimer() = default;
	~GPUTimer() = default;

	GPUTimer(const GPUTimer&) = delete;
	GPUTimer& operator=(const GPUTimer&) = delete;
	GPUTimer(GPUTimer&&) noexcept = default;
	GPUTimer& operator=(GPUTimer&&) noexcept = default;

	/// Start a span. Needs a current GL context; the queries are created on first use.
	void begin();

	/// End the span begun last.
	void end();

	/// GPU milliseconds of the latest span whose result has arrived, 0 until one has.
	[[nodiscard]] float getTimeMs() const { return lastTimeMs; }

  private:
	static constexpr int kFrames = 4;

	struct Span {
		GLQuery start;
		GLQuery stop;
		bool	pending = false;
	};

	std::array<Span, kFrames> spans;
	int						  current = 0;
	bool					  created = false;
	bool					  open	  = false;
	float					  lastTimeMs = 0.0F;
};

} // namespace Renderer
