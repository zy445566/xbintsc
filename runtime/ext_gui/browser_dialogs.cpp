/*
 * xbintsc GUI engine — `window.confirm` / `window.prompt`.
 *
 * A compiled page has no browser chrome, so a blocking dialog is opt-in: with
 * `XT_GUI_DIALOGS=1` and a live window the engine shows a native SDL message
 * box. Otherwise (and always for `prompt`, which would need a text input) the
 * message is written to stderr and the documented default is returned — the
 * arguments are never silently dropped. `confirm` answers false and `prompt`
 * answers `defaultValue` (or "") when no dialog is shown.
 */

#include "gui_engine.h"

#include <stdio.h>
#include <string>

namespace {

std::string message_of(int32_t argc, xt_value *argv) {
  if (argc < 1) return std::string();
  const char *data = xt_string_data(xt_to_string(xt_arg(argc, argv, 0)));
  return data != nullptr ? data : "";
}

bool dialogs_enabled() {
  const char *value = SDL_getenv("XT_GUI_DIALOGS");
  return value != nullptr && value[0] == '1';
}

bool show_message(XtGuiWindow *win, const std::string &message, bool with_cancel) {
  if (win == nullptr || win->window == nullptr) return false;
  SDL_MessageBoxButtonData buttons[2];
  SDL_zero(buttons);
  int count = 1;
  buttons[0].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
  buttons[0].buttonID = 1;
  buttons[0].text = "OK";
  if (with_cancel) {
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[1].buttonID = 0;
    buttons[1].text = "Cancel";
    count = 2;
  }
  SDL_MessageBoxData data;
  SDL_zero(data);
  data.flags = SDL_MESSAGEBOX_INFORMATION;
  data.window = win->window;
  data.title = "xbintsc";
  data.message = message.c_str();
  data.numbuttons = count;
  data.buttons = buttons;
  int pressed = -1;
  if (!SDL_ShowMessageBox(&data, &pressed)) return false;
  return pressed == 1;
}

}  // namespace

xt_value xt_browser_confirm(XtGuiWindow *win, int32_t argc, xt_value *argv) {
  std::string message = message_of(argc, argv);
  if (dialogs_enabled() && show_message(win, message, true)) return XT_TRUE;
  fprintf(stderr, "confirm: %s\n", message.c_str());
  return XT_FALSE;
}

xt_value xt_browser_prompt(XtGuiWindow *win, int32_t argc, xt_value *argv) {
  std::string message = message_of(argc, argv);
  std::string fallback;
  if (argc >= 2) {
    const char *data = xt_string_data(xt_to_string(xt_arg(argc, argv, 1)));
    if (data != nullptr) fallback = data;
  }
  if (dialogs_enabled()) show_message(win, message, false);
  fprintf(stderr, "prompt: %s\n", message.c_str());
  return xt_string_from_cstr(fallback.c_str());
}
