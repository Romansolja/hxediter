/* app_state.h — top-level application mode enum */

#pragma once

enum class AppState {
    StartScreen,   /* no file loaded — branded landing view */
    HexView        /* core present, hex grid shown */
};
