/*
 * xbintsc GUI engine — browser KeyboardEvent names from SDL key events.
 *
 * `code` is the *physical* key, which is what the DOM's `KeyboardEvent.code`
 * reports and what games normally bind to. `key` is the produced value, so it
 * follows Shift/Caps Lock for letters and Shift for the US punctuation row.
 * The mapping is intentionally the US layout: SDL's key names are US-based,
 * and a page that needs another layout should use `code` anyway.
 */

#include "gui_keymap.h"

#include <cstdio>

namespace guidev {

namespace {

void set_names(std::string *key, std::string *code, const char *key_name, const char *code_name) {
  *key = key_name;
  *code = code_name;
}

/** Digits and their shifted symbols, indexed 0..9 for keys 1..0. */
const char *const kDigitKeys = "1234567890";
const char *const kDigitShifted = "!@#$%^&*()";

/** Keypad digits, indexed 0..9 for KP_1..KP_0. */
const char *const kKeypadDigits = "1234567890";

}  // namespace

void xt_gui_key_names(SDL_Scancode scancode, SDL_Keycode keycode, bool shift, bool caps_lock,
                      std::string *key, std::string *code) {
  key->clear();
  code->clear();

  /* Letters: SDL numbers A..Z consecutively. Caps Lock and Shift cancel out. */
  if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z) {
    char letter = static_cast<char>('A' + (scancode - SDL_SCANCODE_A));
    *code = "Key";
    code->push_back(letter);
    bool upper = shift != caps_lock;
    key->push_back(upper ? letter : static_cast<char>(letter + ('a' - 'A')));
    return;
  }

  /* Number row. */
  if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
    int index = static_cast<int>(scancode - SDL_SCANCODE_1);
    *code = "Digit";
    code->push_back(kDigitKeys[index]);
    key->push_back(shift ? kDigitShifted[index] : kDigitKeys[index]);
    return;
  }

  /* Function keys: F1..F12 and F13..F24 are two contiguous runs; the gap
   * between F12 and F13 contains PrintScreen, arrows and the keypad. */
  if ((scancode >= SDL_SCANCODE_F1 && scancode <= SDL_SCANCODE_F12) ||
      (scancode >= SDL_SCANCODE_F13 && scancode <= SDL_SCANCODE_F24)) {
    int number;
    if (scancode <= SDL_SCANCODE_F12) {
      number = static_cast<int>(scancode - SDL_SCANCODE_F1) + 1;
    } else {
      number = static_cast<int>(scancode - SDL_SCANCODE_F13) + 13;
    }
    char name[8];
    std::snprintf(name, sizeof(name), "F%d", number);
    *key = name;
    *code = name;
    return;
  }

  /* Keypad digits, same 1..9,0 order. */
  if (scancode >= SDL_SCANCODE_KP_1 && scancode <= SDL_SCANCODE_KP_0) {
    int index = static_cast<int>(scancode - SDL_SCANCODE_KP_1);
    *code = "Numpad";
    code->push_back(kKeypadDigits[index]);
    key->push_back(kKeypadDigits[index]);
    return;
  }

  switch (scancode) {
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_RETURN2:
      set_names(key, code, "Enter", "Enter");
      return;
    case SDL_SCANCODE_ESCAPE:
      set_names(key, code, "Escape", "Escape");
      return;
    case SDL_SCANCODE_BACKSPACE:
      set_names(key, code, "Backspace", "Backspace");
      return;
    case SDL_SCANCODE_TAB:
      set_names(key, code, "Tab", "Tab");
      return;
    case SDL_SCANCODE_SPACE:
      set_names(key, code, " ", "Space");
      return;
    case SDL_SCANCODE_CAPSLOCK:
      set_names(key, code, "CapsLock", "CapsLock");
      return;
    case SDL_SCANCODE_PRINTSCREEN:
      set_names(key, code, "PrintScreen", "PrintScreen");
      return;
    case SDL_SCANCODE_SCROLLLOCK:
      set_names(key, code, "ScrollLock", "ScrollLock");
      return;
    case SDL_SCANCODE_PAUSE:
      set_names(key, code, "Pause", "Pause");
      return;
    case SDL_SCANCODE_INSERT:
      set_names(key, code, "Insert", "Insert");
      return;
    case SDL_SCANCODE_HOME:
      set_names(key, code, "Home", "Home");
      return;
    case SDL_SCANCODE_PAGEUP:
      set_names(key, code, "PageUp", "PageUp");
      return;
    case SDL_SCANCODE_DELETE:
      set_names(key, code, "Delete", "Delete");
      return;
    case SDL_SCANCODE_END:
      set_names(key, code, "End", "End");
      return;
    case SDL_SCANCODE_PAGEDOWN:
      set_names(key, code, "PageDown", "PageDown");
      return;
    case SDL_SCANCODE_RIGHT:
      set_names(key, code, "ArrowRight", "ArrowRight");
      return;
    case SDL_SCANCODE_LEFT:
      set_names(key, code, "ArrowLeft", "ArrowLeft");
      return;
    case SDL_SCANCODE_DOWN:
      set_names(key, code, "ArrowDown", "ArrowDown");
      return;
    case SDL_SCANCODE_UP:
      set_names(key, code, "ArrowUp", "ArrowUp");
      return;
    case SDL_SCANCODE_NUMLOCKCLEAR:
      set_names(key, code, "NumLock", "NumLock");
      return;
    case SDL_SCANCODE_KP_ENTER:
      set_names(key, code, "Enter", "NumpadEnter");
      return;
    case SDL_SCANCODE_KP_DIVIDE:
      set_names(key, code, "/", "NumpadDivide");
      return;
    case SDL_SCANCODE_KP_MULTIPLY:
      set_names(key, code, "*", "NumpadMultiply");
      return;
    case SDL_SCANCODE_KP_MINUS:
      set_names(key, code, "-", "NumpadSubtract");
      return;
    case SDL_SCANCODE_KP_PLUS:
      set_names(key, code, "+", "NumpadAdd");
      return;
    case SDL_SCANCODE_KP_PERIOD:
    case SDL_SCANCODE_KP_DECIMAL:
      set_names(key, code, ".", "NumpadDecimal");
      return;
    case SDL_SCANCODE_KP_EQUALS:
      set_names(key, code, "=", "NumpadEqual");
      return;
    case SDL_SCANCODE_KP_COMMA:
      set_names(key, code, ",", "NumpadComma");
      return;
    case SDL_SCANCODE_APPLICATION:
    case SDL_SCANCODE_MENU:
      set_names(key, code, "ContextMenu", "ContextMenu");
      return;
    case SDL_SCANCODE_LCTRL:
      set_names(key, code, "Control", "ControlLeft");
      return;
    case SDL_SCANCODE_RCTRL:
      set_names(key, code, "Control", "ControlRight");
      return;
    case SDL_SCANCODE_LSHIFT:
      set_names(key, code, "Shift", "ShiftLeft");
      return;
    case SDL_SCANCODE_RSHIFT:
      set_names(key, code, "Shift", "ShiftRight");
      return;
    case SDL_SCANCODE_LALT:
      set_names(key, code, "Alt", "AltLeft");
      return;
    case SDL_SCANCODE_RALT:
      set_names(key, code, "Alt", "AltRight");
      return;
    case SDL_SCANCODE_LGUI:
      set_names(key, code, "Meta", "MetaLeft");
      return;
    case SDL_SCANCODE_RGUI:
      set_names(key, code, "Meta", "MetaRight");
      return;
    default:
      break;
  }

  /* Remaining US punctuation, keyed by scancode. */
  switch (scancode) {
    case SDL_SCANCODE_MINUS:
      set_names(key, code, shift ? "_" : "-", "Minus");
      return;
    case SDL_SCANCODE_EQUALS:
      set_names(key, code, shift ? "+" : "=", "Equal");
      return;
    case SDL_SCANCODE_LEFTBRACKET:
      set_names(key, code, shift ? "{" : "[", "BracketLeft");
      return;
    case SDL_SCANCODE_RIGHTBRACKET:
      set_names(key, code, shift ? "}" : "]", "BracketRight");
      return;
    case SDL_SCANCODE_BACKSLASH:
    case SDL_SCANCODE_NONUSHASH:
      set_names(key, code, shift ? "|" : "\\", "Backslash");
      return;
    case SDL_SCANCODE_SEMICOLON:
      set_names(key, code, shift ? ":" : ";", "Semicolon");
      return;
    case SDL_SCANCODE_APOSTROPHE:
      set_names(key, code, shift ? "\"" : "'", "Quote");
      return;
    case SDL_SCANCODE_GRAVE:
      set_names(key, code, shift ? "~" : "`", "Backquote");
      return;
    case SDL_SCANCODE_COMMA:
      set_names(key, code, shift ? "<" : ",", "Comma");
      return;
    case SDL_SCANCODE_PERIOD:
      set_names(key, code, shift ? ">" : ".", "Period");
      return;
    case SDL_SCANCODE_SLASH:
      set_names(key, code, shift ? "?" : "/", "Slash");
      return;
    case SDL_SCANCODE_NONUSBACKSLASH:
      set_names(key, code, "\\", "IntlBackslash");
      return;
    default:
      break;
  }

  /* Unknown key: keep SDL's human-readable names. `key` comes from the
   * keycode so a consumer matching "Up" still sees it on a layout whose
   * scancode name differs. */
  const char *key_name = SDL_GetKeyName(keycode);
  const char *code_name = SDL_GetScancodeName(scancode);
  set_names(key, code, key_name != nullptr ? key_name : "",
            code_name != nullptr ? code_name : "");
}

}  // namespace guidev
