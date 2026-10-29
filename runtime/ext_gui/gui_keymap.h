/*
 * xbintsc GUI engine — SDL key event to browser KeyboardEvent names.
 *
 * The DOM exposes the browser spellings `KeyboardEvent.key` (the produced
 * character / action name) and `code` (the physical key), while SDL reports a
 * scancode and keycode. A page written for a browser matches on `code`
 * ("KeyW", "ArrowUp", …), so translating `SDL_GetScancodeName` directly
 * ("W", "Up") is not enough.
 */
#ifndef XT_GUI_KEYMAP_H
#define XT_GUI_KEYMAP_H

#include <SDL3/SDL.h>

#include <string>

namespace guidev {

/** Fill `key` and `code` with the browser names for one physical key press.
 *
 * `scancode` selects the physical `code`; `keycode` and the modifier state
 * select the produced `key` (letters honour Shift and Caps Lock, digits and
 * punctuation use the US layout, which matches SDL's default key names).
 * Unrecognised keys fall back to SDL's own names rather than an empty string,
 * so a page that only logs events still sees something useful. */
void xt_gui_key_names(SDL_Scancode scancode, SDL_Keycode keycode, bool shift, bool caps_lock,
                      std::string *key, std::string *code);

}  // namespace guidev

#endif /* XT_GUI_KEYMAP_H */
