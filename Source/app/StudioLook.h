#pragma once
// "Studio": the askp look, shared by every askp plugin and the askp Hub, plus the theme engine
// (themes, light/dark, live reload). The canonical code is shared/look/AskpLook.h in this repo;
// Source/app/look/ is a copy made by tools/sync-look.sh, like the copies in each plugin.
#define ASKP_LOOK_NS hub::ui
#define ASKP_LOOK_LEGACY_FOLDER "askp Hub"
#include "look/AskpLook.h"
