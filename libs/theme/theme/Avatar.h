#pragma once

// Worldsim's Avatar (docs/design/ui/design-system/components.md, Avatar) on salal's ui: a colonist's
// portrait framed like a dossier photo. The seed, the colonist's name, hashes to the disc's and the
// initials' hue, so a name always looks the same with nothing stored; the frame's ring takes the mood's
// colour and glows in it. Square, at its width (44 when auto). It reads worldsim's tokens, so its
// scene's theme loads theme::tokenTable().

#include <ui/shapes/Rectangle.h>

#include <cstdint>
#include <optional>
#include <string>

namespace theme {

	class Avatar : public ui::Rectangle {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			 seed;
			std::optional<float> mood; // 0 to 1; none is a neutral ring with no glow
			bool				 selected = false;
		};

		explicit Avatar(const Args& args);

		// Tints again, so a theme change reaches the ring.
		void setStyle(const ui::Style& value) override;
		void setMood(std::optional<float> value);
		void setSelected(bool value);

		[[nodiscard]] const char* typeName() const override { return "theme::Avatar"; }

		// FNV-1a over the seed, as the prototype's Avatar.tsx hashes it.
		[[nodiscard]] static std::uint32_t hash(const std::string& seed);
		// The first letters of the seed's first two words, upper case: "Kai Okafor" is "KO".
		[[nodiscard]] static std::string initials(const std::string& seed);

	  protected:
		void onMount() override;

	  private:
		void tint();

		std::string			   seed;
		std::optional<float>   mood;
		bool				   selected;
		ui::Ref<ui::Rectangle> outline;
	};

} // namespace theme
