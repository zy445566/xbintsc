/*
 * xbintsc GUI engine — the DOM object model exposed to TypeScript.
 *
 * Node/document handles are plain runtime objects with a shared prototype,
 * exactly like the window handles in `window.cpp`. Each handle carries hidden
 * properties:
 *
 *   __xt_gui_win   the owning window object (resolved with xt_gui_window_from_this)
 *   __xt_gui_gen   the document generation it was created for
 *   __xt_gui_node  an index into the window's node table (element handles)
 *   __xt_gui_doc   true for the singleton `document` handle
 *
 * A DOM mutation marks the document dirty; the frame loop restyles/relayouts
 * before the next paint.
 *
 * This unit owns the shared prototypes, the generic helpers and the public
 * handle entry points. Event machinery lives in `dom_api_events.cpp`, the
 * prototype table in `dom_api_proto.cpp`, and the element methods in the
 * remaining `dom_api_*.cpp` units.
 */

#include "dom_api_internal.h"

#include <cctype>
#include <string>
#include <vector>

namespace domapi {

xt_value g_node_proto = XT_UNDEFINED;
xt_value g_document_proto = XT_UNDEFINED;
xt_value g_event_proto = XT_UNDEFINED;
xt_value g_class_list_proto = XT_UNDEFINED;
xt_value g_style_proto = XT_UNDEFINED;

/* -- small helpers -------------------------------------------------------- */

static const char *to_cstr(xt_value value, const char *fallback = "") {
  const char *data = xt_string_data(xt_to_string(value));
  return data != nullptr ? data : fallback;
}

std::string to_string_value(xt_value value) { return std::string(to_cstr(value)); }

bool value_truthy(xt_value value) { return xt_truthy(value) != 0; }

/* -- handle plumbing ------------------------------------------------------ */

XtGuiWindow *resolve_window(xt_value self) {
  return xt_gui_window_from_this(xt_object_get_cstr(self, "__xt_gui_win"));
}

/** Resolve a node handle to its node, or NULL when it is stale/foreign. */
xtgui::Node *resolve_node(xt_value self, XtGuiWindow **winOut) {
  XtGuiWindow *win = resolve_window(self);
  if (winOut != nullptr) *winOut = win;
  if (win == nullptr || win->document == nullptr) return nullptr;
  xt_value gen = xt_object_get_cstr(self, "__xt_gui_gen");
  if (!XT_IS_NUMBER(gen) || xt_to_number(gen) != win->doc_generation) return nullptr;
  if (value_truthy(xt_object_get_cstr(self, "__xt_gui_doc"))) {
    return const_cast<xtgui::Node *>(win->document->root());
  }
  xt_value index = xt_object_get_cstr(self, "__xt_gui_node");
  if (!XT_IS_NUMBER(index)) return nullptr;
  int slot = (int)xt_to_number(index);
  if (slot < 0 || slot >= (int)win->node_order.size()) return nullptr;
  /* A recycled slot must not answer for the handle that used to own it. */
  xt_value generation = xt_object_get_cstr(self, "__xt_gui_slot_gen");
  if (!XT_IS_NUMBER(generation) ||
      (int)xt_to_number(generation) != win->node_order[(size_t)slot].generation) {
    return nullptr;
  }
  return const_cast<xtgui::Node *>(win->node_order[(size_t)slot].node);
}

void mark_dirty(XtGuiWindow *win) {
  if (win != nullptr && win->document != nullptr) win->document->invalidate();
  if (win != nullptr) win->struct_dirty = 1;
}

/* -- attribute / class helpers -------------------------------------------- */

void set_attribute_raw(xtgui::Node *node, const std::string &name, const std::string &value) {
  for (xtgui::Attr &attr : node->attrs) {
    if (attr.name == name) {
      attr.value = value;
      return;
    }
  }
  node->attrs.push_back({name, value});
}

void remove_attribute_raw(xtgui::Node *node, const std::string &name) {
  for (auto it = node->attrs.begin(); it != node->attrs.end(); ++it) {
    if (it->name == name) {
      node->attrs.erase(it);
      return;
    }
  }
}

void set_attribute(XtGuiWindow *win, xtgui::Node *node, const std::string &name,
                   const std::string &value) {
  set_attribute_raw(node, name, value);
  mark_dirty(win);
}

std::string class_name_of(const xtgui::Node *node) {
  const std::string *value = node->attr("class");
  return value != nullptr ? *value : std::string();
}

void set_class_name(XtGuiWindow *win, xtgui::Node *node, const std::string &value) {
  if (value.empty()) {
    remove_attribute_raw(node, "class");
    mark_dirty(win);
  } else {
    set_attribute(win, node, "class", value);
  }
}

}  // namespace domapi

xt_value xt_gui_node_handle(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return XT_UNDEFINED;
  auto cached = win->node_handles.find(node);
  if (cached != win->node_handles.end()) return cached->second;
  int slot;
  auto found = win->node_index.find(node);
  if (found != win->node_index.end()) {
    slot = found->second;
  } else if (!win->free_node_slots.empty()) {
    /* Reuse a forgotten slot instead of growing the table forever. The
     * generation bump below makes the handle that used to own it inert. */
    slot = win->free_node_slots.back();
    win->free_node_slots.pop_back();
    win->node_order[(size_t)slot].node = node;
    win->node_index[node] = slot;
  } else {
    slot = (int)win->node_order.size();
    XtGuiNodeSlot entry;
    entry.node = node;
    win->node_order.push_back(entry);
    win->node_index[node] = slot;
  }
  xt_value handle = xt_object_new_with_proto(domapi::node_proto());
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_win"), win->object);
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_gen"), xt_number(win->doc_generation));
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_node"), xt_number((double)slot));
  xt_object_set(handle, xt_string_from_cstr("__xt_gui_slot_gen"),
                xt_number((double)win->node_order[(size_t)slot].generation));
  win->node_handles[node] = handle;
  return handle;
}

void xt_gui_node_handle_forget(XtGuiWindow *win, const xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return;
  /* The node is about to be destroyed (a removal, or `innerHTML = …` dropping
   * the old children). Drop its slot as well as its handle: the slot is what
   * `resolve_node` reads, and leaving a freed pointer there is a
   * use-after-free the next time any handle is resolved. Existing handles keep
   * resolving to "stale" and no-op, which is the documented tombstone
   * behaviour — the generation bump makes the recycled slot unavailable to
   * them. */
  auto index = win->node_index.find(node);
  if (index != win->node_index.end()) {
    int slot = index->second;
    if (slot >= 0 && slot < (int)win->node_order.size()) {
      win->node_order[(size_t)slot].node = nullptr;
      win->node_order[(size_t)slot].generation++;
      win->free_node_slots.push_back(slot);
    }
    win->node_index.erase(index);
  }
  win->node_handles.erase(node);
}

/** Forget the handles of `node` and every descendant. Runs before the subtree
 * is freed, so the walk still reads live children. */
static void forget_subtree_handles(XtGuiWindow *win, const xtgui::Node *node) {
  xt_gui_node_handle_forget(win, node);
  for (const std::unique_ptr<xtgui::Node> &child : node->children) {
    forget_subtree_handles(win, child.get());
  }
}

void domapi::discard_subtree(XtGuiWindow *win, xtgui::Node *node) {
  if (win == nullptr || node == nullptr || win->document == nullptr) return;
  forget_subtree_handles(win, node);
  win->document->discard(node);
}

xt_value xt_gui_document_handle(XtGuiWindow *win) {
  if (win == nullptr) return XT_UNDEFINED;
  if (!XT_IS_OBJECT(win->document_object)) {
    xt_value handle = xt_object_new_with_proto(domapi::document_proto());
    xt_object_set(handle, xt_string_from_cstr("__xt_gui_win"), win->object);
    xt_object_set(handle, xt_string_from_cstr("__xt_gui_doc"), XT_TRUE);
    win->document_object = handle;
  }
  xt_object_set(win->document_object, xt_string_from_cstr("__xt_gui_gen"),
                xt_number(win->doc_generation));
  return win->document_object;
}

void xt_gui_handles_reset(XtGuiWindow *win) {
  if (win == nullptr) return;
  win->doc_generation += 1.0;
  win->node_order.clear();
  win->free_node_slots.clear();
  win->node_index.clear();
  win->node_handles.clear();
  win->node_listeners.clear();
  win->struct_dirty = 0;
  if (XT_IS_OBJECT(win->document_object)) {
    xt_object_set(win->document_object, xt_string_from_cstr("__xt_gui_gen"),
                  xt_number(win->doc_generation));
  }
}
