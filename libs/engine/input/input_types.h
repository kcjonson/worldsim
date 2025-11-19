#pragma once

// Input abstraction types
// Provides platform-independent input constants that isolate GLFW from higher-level code.
// This allows libs/ui and other modules to handle input without depending on GLFW directly.

namespace engine {

	// Keyboard key codes (platform-independent)
	enum class Key {
		// Printable keys
		Space,
		Apostrophe, // '
		Comma,		// ,
		Minus,		// -
		Period,		// .
		Slash,		// /

		// Numbers
		Num0,
		Num1,
		Num2,
		Num3,
		Num4,
		Num5,
		Num6,
		Num7,
		Num8,
		Num9,

		// Letters
		A,
		B,
		C,
		D,
		E,
		F,
		G,
		H,
		I,
		J,
		K,
		L,
		M,
		N,
		O,
		P,
		Q,
		R,
		S,
		T,
		U,
		V,
		W,
		X,
		Y,
		Z,

		// Special keys
		Escape,
		Enter,
		Tab,
		Backspace,
		Insert,
		Delete,
		Home,
		End,
		PageUp,
		PageDown,

		// Arrow keys
		Right,
		Left,
		Down,
		Up,

		// Function keys
		F1,
		F2,
		F3,
		F4,
		F5,
		F6,
		F7,
		F8,
		F9,
		F10,
		F11,
		F12,

		// Modifiers
		LeftShift,
		LeftControl,
		LeftAlt,
		LeftSuper,
		RightShift,
		RightControl,
		RightAlt,
		RightSuper,

		// Keypad
		Kp0,
		Kp1,
		Kp2,
		Kp3,
		Kp4,
		Kp5,
		Kp6,
		Kp7,
		Kp8,
		Kp9,
		KpDecimal,
		KpDivide,
		KpMultiply,
		KpSubtract,
		KpAdd,
		KpEnter,
		KpEqual
	};

	// Mouse button codes (platform-independent)
	enum class MouseButton {
		Left = 0,
		Right = 1,
		Middle = 2,
		Button4 = 3,
		Button5 = 4,
		Button6 = 5,
		Button7 = 6,
		Button8 = 7
	};

} // namespace engine
