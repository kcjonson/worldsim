#pragma once

// Tree rows for the asset manager's left panel, as salal's ui controls: an asset row (its thumbnail
// and definition name), selected with ListRow's accent bar, and a category header with a chevron that
// collapses and expands its rows.

#include "AssetThumbnail.h"

#include <ui/components/Control.h>
#include <ui/shapes/Icon.h>

#include <functional>
#include <string>

namespace asset_manager {

	class AssetRow : public ui::Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string								defName;
			bool									selected = false;
			std::function<void(const std::string&)> onSelect;
		};

		explicit AssetRow(const Args& args);

		void setSelected(bool value);

		[[nodiscard]] const char* typeName() const override { return "AssetRow"; }
		[[nodiscard]] ui::StyleId themeStyle() const override;

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		std::string								defName;
		bool									selectedAtStart;
		std::function<void(const std::string&)> onSelect;
		ui::Ref<ui::Rectangle>					bar;
	};

	class GroupHeaderRow : public ui::Control {
	  public:
		struct Args {
			UI_COMMON_ARGS
			std::string			  label;
			bool				  expanded = true;
			std::function<void()> onToggle;
		};

		explicit GroupHeaderRow(const Args& args);

		[[nodiscard]] const char* typeName() const override { return "GroupHeaderRow"; }
		[[nodiscard]] ui::StyleId themeStyle() const override;

	  protected:
		void onMount() override;
		void activate() override;

	  private:
		std::string			  label;
		bool				  expanded;
		std::function<void()> onToggle;
	};

} // namespace asset_manager
