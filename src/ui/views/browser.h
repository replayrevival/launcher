#pragma once
// The file browser as a message-box style panel (RmlUi markup). Actions (all start with "modal-" so the launcher's message-box rules
// apply: focus stays inside, Back closes): modal-br-open <entry index>, modal-br-up, modal-br-place <index>, modal-br-recent <index>,
// modal-br-pc, modal-br-use, modal-br-native, modal-br-cancel.
#include <string>

#include "core/file_browser.h"

namespace views {

// `nativeLabel`: text of the button that switches to the operating system's own dialog ("" = no such button).
std::string browserPanel(const FileBrowser& browser, const std::string& nativeLabel);

}  // namespace views
