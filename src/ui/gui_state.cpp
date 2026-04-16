/* gui_state.cpp — GuiState helper methods. */

#include "ui/gui_state.h"
#include "ui/layout.h"

#include <utility>

namespace ui {

void GuiState::SetStatus(std::string msg, StatusKind kind, bool sticky) {
    status_msg    = std::move(msg);
    status_kind   = kind;
    status_timer  = layout::kStatusMsgSeconds;
    status_sticky = sticky;
}

void GuiState::MarkInteracted() {
    user_interacted = true;
    show_help       = false;
}

} /* namespace ui */
