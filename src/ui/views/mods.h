#pragma once
// The Mods page as RmlUi markup. Shared by the launcher (App) and the planned in-game overlay (docs/OVERLAY.md). No window,
// no SDL; actions are `data-action` attributes (mods-*, mod-up, mod-down, mod-toggle, mod-remove) the host handles.
#include <map>
#include <string>
#include <vector>

#include "core/launcher_model.h"
#include "core/mod_patch.h"

namespace views {

// What the page shows besides the model: background work and the last note.
struct ModsViewState {
    bool busy = false;        // a mod is being installed
    bool patchBusy = false;   // patched files are being prepared
    std::string busyName;     // name of the mod being installed
    std::string note;         // result of the last install
    const std::map<std::string, std::vector<ModPatchReport>>* patchReports = nullptr;  // mod id -> state of its patches
};

std::string modsPage(const LauncherModel& model, const ModsViewState& state);

}  // namespace views
