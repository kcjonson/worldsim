#pragma once

// A row of images drawn one texel to one pixel, each over its caption: the ground bake view's mip chain
// and zoom strip. Sizes itself to the row. The images are textures it owns, sampled nearest.

#include <Texture.h>
#include <graphics/Rect.h>
#include <ui/Component.h>

#include <cstdint>
#include <string>
#include <vector>

namespace asset_manager {

	class TileImageStrip : public ui::Component {
	  public:
		struct Image {
			int					 width = 0;
			int					 height = 0;
			std::vector<uint8_t> rgba;	  // straight-alpha sRGB, rows top to bottom
			std::string			 caption; // lines split on '\n'
		};

		struct Args {
			UI_COMMON_ARGS
		};

		explicit TileImageStrip(const Args& args);

		void setImages(std::vector<Image> images);

		[[nodiscard]] const char* typeName() const override { return "TileImageStrip"; }

	  protected:
		void render() override;

	  private:
		struct Shown {
			Renderer::Texture		 texture;
			Foundation::Rect		 bounds;
			std::vector<std::string> captionLines;
		};

		std::vector<Shown> shown;
	};

} // namespace asset_manager
