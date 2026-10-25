// ==============================================================================
// NIX QT6 NATIVE GUI LIBRARY (libnix_qt)
// ------------------------------------------------------------------------------
// Complete C++ Qt6 GUI & 2D Canvas Engine for the Nix Programming Language.
// Supports:
//   - Immediate-mode 2D Canvas & Game Window API (<window>, <mac_window>)
//   - High-level Qt6 Widget & Layout API (<qt>, <gui>, <ui_engine>)
// ==============================================================================
#pragma once

#include "nix_runtime.hpp"

namespace nix {
namespace qt_gui {

// --- 1. Immediate-Mode 2D Canvas & Window API (<window> / <mac_window>) ---
Value window_open(const Value* a, size_t n, const Value& self);
Value window_is_open(const Value* a, size_t n, const Value& self);
Value window_poll_events(const Value* a, size_t n, const Value& self);
Value window_clear(const Value* a, size_t n, const Value& self);
Value window_draw_rect(const Value* a, size_t n, const Value& self);
Value window_draw_circle(const Value* a, size_t n, const Value& self);
Value window_draw_line(const Value* a, size_t n, const Value& self);
Value window_draw_text(const Value* a, size_t n, const Value& self);
Value window_present(const Value* a, size_t n, const Value& self);
Value window_close(const Value* a, size_t n, const Value& self);
Value window_set_title(const Value* a, size_t n, const Value& self);

// --- 2. High-Level Qt6 Widgets API (<qt> / <gui>) ---
Value qt_app(const Value* a, size_t n, const Value& self);
Value qt_window(const Value* a, size_t n, const Value& self);
Value qt_show(const Value* a, size_t n, const Value& self);
Value qt_hide(const Value* a, size_t n, const Value& self);
Value qt_close(const Value* a, size_t n, const Value& self);
Value qt_run(const Value* a, size_t n, const Value& self);
Value qt_quit(const Value* a, size_t n, const Value& self);

// Layouts & Containers
Value qt_vbox(const Value* a, size_t n, const Value& self);
Value qt_hbox(const Value* a, size_t n, const Value& self);
Value qt_frame(const Value* a, size_t n, const Value& self);
Value qt_groupbox(const Value* a, size_t n, const Value& self);

// Widgets
Value qt_label(const Value* a, size_t n, const Value& self);
Value qt_button(const Value* a, size_t n, const Value& self);
Value qt_input(const Value* a, size_t n, const Value& self);
Value qt_textarea(const Value* a, size_t n, const Value& self);
Value qt_checkbox(const Value* a, size_t n, const Value& self);
Value qt_slider(const Value* a, size_t n, const Value& self);
Value qt_progressbar(const Value* a, size_t n, const Value& self);
Value qt_combobox(const Value* a, size_t n, const Value& self);
Value qt_listwidget(const Value* a, size_t n, const Value& self);

// Properties, Styling & Events
Value qt_set_text(const Value* a, size_t n, const Value& self);
Value qt_get_text(const Value* a, size_t n, const Value& self);
Value qt_set_value(const Value* a, size_t n, const Value& self);
Value qt_get_value(const Value* a, size_t n, const Value& self);
Value qt_is_checked(const Value* a, size_t n, const Value& self);
Value qt_set_checked(const Value* a, size_t n, const Value& self);
Value qt_set_style(const Value* a, size_t n, const Value& self);
Value qt_set_title(const Value* a, size_t n, const Value& self);
Value qt_resize(const Value* a, size_t n, const Value& self);
Value qt_set_enabled(const Value* a, size_t n, const Value& self);
Value qt_add_item(const Value* a, size_t n, const Value& self);
Value qt_clear_items(const Value* a, size_t n, const Value& self);
Value qt_on_click(const Value* a, size_t n, const Value& self);
Value qt_on_change(const Value* a, size_t n, const Value& self);
Value qt_timer(const Value* a, size_t n, const Value& self);

// Dialogs
Value qt_msgbox(const Value* a, size_t n, const Value& self);
Value qt_confirm(const Value* a, size_t n, const Value& self);
Value qt_file_dialog(const Value* a, size_t n, const Value& self);
Value qt_color_dialog(const Value* a, size_t n, const Value& self);

// Automatic event loop runner if any Qt or Canvas window is still open at script end
void auto_exec_if_windows_open();

}  // namespace qt_gui
}  // namespace nix
