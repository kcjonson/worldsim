#include "theme/InterfaceScale.h"

#include <application/App.h>

#include <format>

namespace theme {

	app::DevServer::Registration registerUiScaleView(app::App& owner) {
		return owner.devServer().stateProvider("uiscale", [&owner] {
			return std::format(
				R"({{"factor":{},"layoutScale":{},"pixelScale":{}}})", owner.uiScale(), owner.uiLayoutScale(), owner.uiPixelScale()
			);
		});
	}

} // namespace theme
