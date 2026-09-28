/*
 * xbintsc GUI engine — AOT `<script>` registry.
 *
 * xbintsc has no runtime interpreter, so `<script>` bodies are compiled ahead
 * of time. The `.html` asset loader (see `src/extensions/gui/html.ts`) extracts
 * each inline body, wraps it in a function `(window, document) => { ... }` and
 * calls the `__registerScript(id, fn)` builtin at module initialization. This
 * file keeps that registry and runs the matching functions once the document
 * has been parsed.
 */

#include "gui_engine.h"

#include <string>
#include <unordered_map>
#include <vector>

/** Script id -> AOT-compiled function value. Lives for the whole process (the
 * runtime arena never reclaims, so storing `xt_value`s is safe). */
static std::unordered_map<std::string, xt_value> &script_registry() {
  static std::unordered_map<std::string, xt_value> registry;
  return registry;
}

extern "C" xt_value xt_register_script(int32_t argc, xt_value *argv) {
  if (argc >= 2) {
    xt_value id_value = xt_to_string(xt_arg(argc, argv, 0));
    const char *id = xt_string_data(id_value);
    xt_value fn = xt_arg(argc, argv, 1);
    if (id != nullptr && XT_IS_FUNCTION(fn)) {
      script_registry()[std::string(id)] = fn;
    }
  }
  return XT_UNDEFINED;
}

/** Collect `data-xt-id` markers from `<script>` elements, in document order. */
static void collect_script_ids(const xtgui::Node *node, std::vector<std::string> &ids) {
  if (node == nullptr) return;
  if (node->isElement() && node->isTag("script")) {
    const std::string *id = node->attr("data-xt-id");
    if (id != nullptr && !id->empty()) ids.push_back(*id);
    return;
  }
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    collect_script_ids(child.get(), ids);
  }
}

void xt_gui_run_scripts(XtGuiWindow *win) {
  if (win == nullptr || win->document == nullptr) return;
  std::vector<std::string> ids;
  collect_script_ids(win->document->root(), ids);
  if (ids.empty()) return;
  xt_value window_handle = win->object;
  xt_value document_handle = xt_gui_document_handle(win);
  for (const std::string &id : ids) {
    auto it = script_registry().find(id);
    if (it == script_registry().end()) continue;
    xt_value args[2] = {window_handle, document_handle};
    xt_call_with_this(it->second, XT_UNDEFINED, 2, args);
  }
}
