/*
 * xbintsc GUI engine — the Pointer Lock API.
 *
 * First-person games aim with raw relative motion instead of the cursor: a
 * click calls `element.requestPointerLock()`, the host hides and captures the
 * pointer, and every `mousemove` carries `movementX`/`movementY`. SDL3 calls
 * that mode "relative mouse", so the two states map one-to-one:
 *
 *   requestPointerLock()   -> SDL_SetWindowRelativeMouseMode(window, true)
 *   exitPointerLock()      -> SDL_SetWindowRelativeMouseMode(window, false)
 *   document.pointerLockElement
 *   pointerlockchange / pointerlockerror
 *
 * Without it a game silently falls back to drag-to-look and receives
 * `movementX === 0`, which reads as "the mouse does nothing".
 */

#include "dom_api_internal.h"
#include "gui_engine.h"
#include "gui_internal.h"

namespace {

/** Deliver `pointerlockchange` / `pointerlockerror` to `document` listeners
 * (the document handle resolves to the root node) and then to `window`
 * listeners, like an ordinary DOM event. */
void emit_pointer_lock_event(XtGuiWindow *win, const char *type) {
  if (win == nullptr) return;
  xt_value payload = xt_object_new();
  xt_object_set(payload, xt_string_from_cstr("type"), xt_string_from_cstr(type));
  if (win->document != nullptr) {
    const xtgui::Node *root = win->document->root();
    if (root != nullptr) {
      xt_value legacy = xt_object_new();
      xt_object_set(legacy, xt_string_from_cstr("type"), xt_string_from_cstr(type));
      /* In a browser the target is `document`; the engine's document handle
       * resolves to this root node, so the listener path is the same. */
      xt_object_set(legacy, xt_string_from_cstr("target"), xt_string_from_cstr("#document"));
      xt_gui_emit_dom_event(win, type, root, legacy);
    }
  }
  xt_gui_emit_payload(win, type, payload);
}

}  // namespace

bool xt_gui_set_pointer_lock(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || win->window == nullptr) return false;
  if (win->pointer_lock_node == node) return true;
  if (node == nullptr) {
    if (win->pointer_lock_node == nullptr) return true;
    win->pointer_lock_node = nullptr;
    SDL_SetWindowRelativeMouseMode(win->window, false);
    emit_pointer_lock_event(win, "pointerlockchange");
    return true;
  }
  if (win->document == nullptr || win->document->root() == nullptr ||
      !win->document->root()->contains(node)) {
    return false;
  }
  if (!SDL_SetWindowRelativeMouseMode(win->window, true)) return false;
  win->pointer_lock_node = node;
  emit_pointer_lock_event(win, "pointerlockchange");
  return true;
}

void xt_gui_pointer_lock_release(XtGuiWindow *win) {
  if (win == nullptr) return;
  if (win->window != nullptr && win->pointer_lock_node != nullptr) {
    SDL_SetWindowRelativeMouseMode(win->window, false);
  }
  win->pointer_lock_node = nullptr;
}

void xt_gui_pointer_lock_error(XtGuiWindow *win) {
  emit_pointer_lock_event(win, "pointerlockerror");
}

namespace domapi {

namespace {

xt_value pointer_lock_error(const char *message) {
  xt_value text = xt_string_from_cstr(message);
  return xt_error_ctor_by_name(xt_string_from_cstr("Error"), 1, &text);
}

const char *pointer_lock_failure(void) {
  const char *error = SDL_GetError();
  return error != nullptr && error[0] != '\0' ? error : "pointer lock is not available";
}

}  // namespace

/** `element.requestPointerLock()`: enter relative mouse mode and resolve once
 * the lock is active. A failed request fires `pointerlockerror` and rejects. */
xt_value node_request_pointer_lock(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (win == nullptr || node == nullptr || win->window == nullptr) {
    return xt_promise_reject(pointer_lock_error("the element is not in a document"));
  }
  if (!xt_gui_set_pointer_lock(win, node)) {
    xt_gui_pointer_lock_error(win);
    return xt_promise_reject(pointer_lock_error(pointer_lock_failure()));
  }
  return xt_promise_resolve(XT_UNDEFINED);
}

/** `document.exitPointerLock()`: leave relative mouse mode. A no-op when no
 * element holds the lock. */
xt_value doc_exit_pointer_lock(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  xt_gui_set_pointer_lock(resolve_window(self), nullptr);
  return XT_UNDEFINED;
}

/** `document.pointerLockElement`: the locked element, or null. */
xt_value doc_pointer_lock_element_get(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = resolve_window(self);
  if (win == nullptr || win->document == nullptr || win->pointer_lock_node == nullptr) {
    return XT_NULL;
  }
  const xtgui::Node *root = win->document->root();
  if (root == nullptr || !root->contains(win->pointer_lock_node)) return XT_NULL;
  return xt_gui_node_handle(win, win->pointer_lock_node);
}

}  // namespace domapi
