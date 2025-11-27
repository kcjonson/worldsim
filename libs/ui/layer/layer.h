#pragma once

#include "input/input_types.h"
#include <concepts>
#include <cstdint>

// Layer concept definitions and handle type for UI framework
//
// See: /docs/technical/ui-framework/architecture.md for design rationale
// See: /docs/technical/resource-handles.md for handle pattern documentation

namespace UI {

// ============================================================================
// LayerHandle - Safe reference to a layer in the hierarchy
// ============================================================================
//
// Uses generational index pattern: 16-bit index + 16-bit generation
// When a layer is removed, generation increments, invalidating old handles.
// See: /docs/technical/resource-handles.md for detailed explanation.

struct LayerHandle {
	uint32_t value{kInvalidHandle};

	static constexpr uint32_t kInvalidHandle = 0xFFFFFFFF;

	[[nodiscard]] bool IsValid() const { return value != kInvalidHandle; }

	[[nodiscard]] uint16_t GetIndex() const {
		return static_cast<uint16_t>(value & 0xFFFF);
	}

	[[nodiscard]] uint16_t GetGeneration() const {
		return static_cast<uint16_t>(value >> 16);
	}

	static LayerHandle Make(uint16_t index, uint16_t generation) {
		return {(static_cast<uint32_t>(generation) << 16) | index};
	}

	static LayerHandle Invalid() { return {kInvalidHandle}; }

	bool operator==(const LayerHandle& other) const { return value == other.value; }
	bool operator!=(const LayerHandle& other) const { return value != other.value; }
};

// ============================================================================
// Layer Concept - All layers must implement lifecycle methods
// ============================================================================
//
// Every layer in the hierarchy (shapes and components) satisfies this concept.
// Shapes have no-op implementations for HandleInput/Update.
// Components have full implementations.
//
// Lifecycle order: HandleInput() -> Update(deltaTime) -> Render()

template <typename T>
concept Layer = requires(T& layer, float deltaTime) {
	{ layer.HandleInput() } -> std::same_as<void>;
	{ layer.Update(deltaTime) } -> std::same_as<void>;
	{ layer.Render() } -> std::same_as<void>;
};

// ============================================================================
// Focusable Concept - Layers that can receive keyboard focus
// ============================================================================
//
// Components that participate in Tab navigation and keyboard input implement this.
// The FocusManager uses std::visit to dispatch to focusable layers.
//
// Methods:
// - OnFocusGained(): Called when component receives focus (show cursor, etc.)
// - OnFocusLost(): Called when component loses focus (clear selection, etc.)
// - HandleKeyInput(): Process keyboard events (arrows, Enter, etc.)
// - HandleCharInput(): Process typed characters (Unicode codepoints)
// - CanReceiveFocus(): Return false to skip during Tab navigation

template <typename T>
concept Focusable = requires(T& component, engine::Key key, bool shift, bool ctrl, bool alt,
							 char32_t codepoint) {
	{ component.OnFocusGained() } -> std::same_as<void>;
	{ component.OnFocusLost() } -> std::same_as<void>;
	{ component.HandleKeyInput(key, shift, ctrl, alt) } -> std::same_as<void>;
	{ component.HandleCharInput(codepoint) } -> std::same_as<void>;
	{ component.CanReceiveFocus() } -> std::same_as<bool>;
};

} // namespace UI
