#pragma once

// Test helpers that run PlanetGenerator to the end of a run.
//
// There is no wall-clock deadline. CI runners differ in speed by 1.5x or more
// between runs, so a fixed deadline can't tell a hang from a slow machine, and one
// that expired turned a slow runner into a null-world assertion. A hang is caught
// by the TIMEOUT on the test's ctest entry. Every kHeartbeat the wait prints the
// running stage, so a timed-out log shows whether the run was still advancing.

#include "worldgen/pipeline/PlanetGenerator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

namespace worldgen {

inline const char* generationStateName(GenerationProgress::State state) {
	switch (state) {
		case GenerationProgress::State::Idle:
			return "Idle";
		case GenerationProgress::State::Running:
			return "Running";
		case GenerationProgress::State::Complete:
			return "Complete";
		case GenerationProgress::State::Cancelled:
			return "Cancelled";
		case GenerationProgress::State::Failed:
			return "Failed";
	}
	return "?";
}

// Blocks while gen's run is Running, then takes its result. A run that ends in any
// state but Complete records a test failure naming the state and reason, and
// yields nullptr.
inline std::shared_ptr<const GeneratedWorld> waitForResult(PlanetGenerator& gen) {
	using Clock = std::chrono::steady_clock;
	constexpr auto kHeartbeat = std::chrono::seconds(120);

	const auto start = Clock::now();
	auto	   nextHeartbeat = start + kHeartbeat;
	auto	   prog = gen.progress();
	while (prog.state == GenerationProgress::State::Running) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		prog = gen.progress();
		const auto now = Clock::now();
		if (now >= nextHeartbeat) {
			const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
			printf("[PlanetGenerator] still running after %lld s: stage %d (%s) %.0f%%, total %.0f%%\n",
				   static_cast<long long>(secs), prog.stageIndex, prog.stageName ? prog.stageName : "-",
				   prog.stageFraction * 100.0f, prog.totalFraction * 100.0f);
			fflush(stdout);
			nextHeartbeat += kHeartbeat;
		}
	}

	if (prog.state != GenerationProgress::State::Complete) {
		ADD_FAILURE() << "generation ended " << generationStateName(prog.state) << ": "
					  << gen.failureReason();
		return nullptr;
	}
	return gen.takeResult();
}

// threadCount: 0 = hardware_concurrency - 1
inline std::shared_ptr<const GeneratedWorld> runToCompletion(const PlanetParams& params,
															 unsigned			 threadCount = 0) {
	PlanetGenerator gen(threadCount);
	gen.start(params);
	return waitForResult(gen);
}

} // namespace worldgen
