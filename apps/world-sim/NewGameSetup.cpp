#include "NewGameSetup.h"

namespace world_sim {

NewGameSetup NewGameSetup::s_instance;

NewGameSetup& NewGameSetup::Get() {
	return s_instance;
}

void NewGameSetup::Reset() {
	s_instance = {};
}

} // namespace world_sim
