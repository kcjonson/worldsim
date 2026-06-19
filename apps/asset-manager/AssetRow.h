#pragma once

// Tree rows for the asset manager's left panel: a selectable asset row
// (thumbnail + def name) and a collapsible category header. Both are IComponent
// leaves added into a LayoutContainer inside a ScrollContainer.

#include "AssetThumbnail.h"

#include <component/Component.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>

namespace asset_manager {

	class AssetRow : public UI::IComponent {
	  public:
		struct Args {
			std::string								defName;
			std::function<void(const std::string&)> onSelect;
			float									width = 250.0F;
		};
		explicit AssetRow(const Args& args);

		void							 setSelected(bool selected) { m_selected = selected; }
		[[nodiscard]] const std::string& defName() const { return m_defName; }

		void				render() override;
		bool				handleEvent(UI::InputEvent& event) override;
		[[nodiscard]] bool	containsPoint(Foundation::Vec2 point) const override;
		[[nodiscard]] float getWidth() const override { return m_width; }
		[[nodiscard]] float getHeight() const override { return kRowHeight + margin; }
		void				setPosition(float x, float y) override;

	  private:
		std::string								m_defName;
		std::function<void(const std::string&)> m_onSelect;
		AssetThumbnail							m_thumb;
		UI::Text								m_label;
		Foundation::Vec2						m_pos{0.0F, 0.0F};
		float									m_width = 250.0F;
		bool									m_selected = false;

		static constexpr float kRowHeight = 36.0F;
		static constexpr float kThumbSize = 26.0F;
		static constexpr float kIndent = 24.0F;
	};

	class GroupHeaderRow : public UI::IComponent {
	  public:
		struct Args {
			std::string			  label;
			bool				  expanded = true;
			std::function<void()> onToggle;
			float				  width = 250.0F;
		};
		explicit GroupHeaderRow(const Args& args);

		void				render() override;
		bool				handleEvent(UI::InputEvent& event) override;
		[[nodiscard]] bool	containsPoint(Foundation::Vec2 point) const override;
		[[nodiscard]] float getWidth() const override { return m_width; }
		[[nodiscard]] float getHeight() const override { return kRowHeight + margin; }
		void				setPosition(float x, float y) override;

	  private:
		std::function<void()> m_onToggle;
		UI::Text			  m_label;
		Foundation::Vec2	  m_pos{0.0F, 0.0F};
		float				  m_width = 250.0F;

		static constexpr float kRowHeight = 26.0F;
	};

} // namespace asset_manager
