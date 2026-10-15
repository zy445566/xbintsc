/*
 * xbintsc GUI engine — `<video>` / `<audio>` object API.
 *
 * These methods live on the shared node prototype (`g_node_proto`), so they are
 * callable on any element; a non-media element simply resolves no decoder and
 * the methods no-op / return the media defaults. The decoders themselves live
 * on the owning window (`gui_media.cpp`); this unit is the thin bridge from a
 * node handle to its `MediaPlayer`.
 */

#include "dom_api_internal.h"

#include <string>

namespace domapi {

/** The decoder already open for `node`, without opening one (getters must not
 * have the side effect of starting playback). */
static xtgui::MediaPlayer *openPlayer(XtGuiWindow *win, xtgui::Node *node) {
  if (win == nullptr || node == nullptr) return nullptr;
  auto found = win->media_players.find(node);
  return found == win->media_players.end() ? nullptr : found->second.player.get();
}

/** Set/clear a boolean attribute (`loop`, `muted`, `autoplay`) so the value
 * survives a later (re)open in `gui_media.cpp`. */
static void setBooleanAttribute(XtGuiWindow *win, xtgui::Node *node, const char *name, bool value) {
  if (value) {
    set_attribute_raw(node, name, "");
  } else {
    remove_attribute_raw(node, name);
  }
  mark_dirty(win);
}

#define METHOD(name) xt_value name(xt_value self, xt_value env, int32_t argc, xt_value *argv)

METHOD(node_play) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  xtgui::MediaPlayer *player = xt_gui_media_player(win, node);
  if (player != nullptr) player->play();
  return XT_UNDEFINED;
}

METHOD(node_pause) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  xtgui::MediaPlayer *player = openPlayer(win, node);
  if (player != nullptr) player->pause();
  return XT_UNDEFINED;
}

METHOD(node_load) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  /* Re-open from the current source, discarding any existing decoder. */
  xt_gui_media_forget(win, node);
  xt_gui_media_player(win, node);
  return XT_UNDEFINED;
}

METHOD(node_paused_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return (player == nullptr || player->paused()) ? XT_TRUE : XT_FALSE;
}

METHOD(node_ended_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return (player != nullptr && player->ended()) ? XT_TRUE : XT_FALSE;
}

METHOD(node_current_time_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return xt_number(player != nullptr ? player->current_time() : 0.0);
}

METHOD(node_current_time_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  xtgui::MediaPlayer *player = openPlayer(win, node);
  if (player != nullptr) player->seek(argc >= 1 ? xt_to_number(xt_arg(argc, argv, 0)) : 0.0);
  return XT_UNDEFINED;
}

METHOD(node_duration_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return xt_number(player != nullptr ? player->duration() : 0.0);
}

METHOD(node_volume_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return xt_number(player != nullptr ? player->volume() : 1.0);
}

METHOD(node_volume_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  double value = argc >= 1 ? xt_to_number(xt_arg(argc, argv, 0)) : 1.0;
  if (value < 0.0) value = 0.0;
  if (value > 1.0) value = 1.0;
  xtgui::MediaPlayer *player = openPlayer(win, node);
  if (player != nullptr) player->set_volume(value);
  return XT_UNDEFINED;
}

METHOD(node_muted_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return (player != nullptr && player->muted()) ? XT_TRUE : XT_FALSE;
}

METHOD(node_muted_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  bool value = argc >= 1 && value_truthy(xt_arg(argc, argv, 0));
  setBooleanAttribute(win, node, "muted", value);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  if (player != nullptr) player->set_muted(value);
  return XT_UNDEFINED;
}

METHOD(node_loop_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return (player != nullptr && player->loop()) ? XT_TRUE : XT_FALSE;
}

METHOD(node_loop_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  bool value = argc >= 1 && value_truthy(xt_arg(argc, argv, 0));
  setBooleanAttribute(win, node, "loop", value);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  if (player != nullptr) player->set_loop(value);
  return XT_UNDEFINED;
}

METHOD(node_autoplay_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return (player != nullptr && player->autoplay()) ? XT_TRUE : XT_FALSE;
}

METHOD(node_autoplay_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  bool value = argc >= 1 && value_truthy(xt_arg(argc, argv, 0));
  setBooleanAttribute(win, node, "autoplay", value);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  if (player != nullptr) player->set_autoplay(value);
  return XT_UNDEFINED;
}

METHOD(node_video_width_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return xt_number(player != nullptr ? player->frame_width() : 0);
}

METHOD(node_video_height_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  return xt_number(player != nullptr ? player->frame_height() : 0);
}

METHOD(node_ready_state_get) {
  (void)env;
  (void)argc;
  (void)argv;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  xtgui::MediaPlayer *player = openPlayer(win, node);
  /* HAVE_ENOUGH_DATA (4) once a player exists, HAVE_NOTHING (0) otherwise. */
  return xt_number(player != nullptr ? 4 : 0);
}

METHOD(node_media_src_get) {
  (void)env;
  (void)argc;
  (void)argv;
  xtgui::Node *node = resolve_node(self, nullptr);
  if (node == nullptr || !node->isElement()) return xt_string_from_cstr("");
  const std::string *src = node->attr("src");
  return xt_string_from_cstr(src != nullptr ? src->c_str() : "");
}

METHOD(node_media_src_set) {
  (void)env;
  XtGuiWindow *win = nullptr;
  xtgui::Node *node = resolve_node(self, &win);
  if (node == nullptr || !node->isElement()) return XT_UNDEFINED;
  std::string value = argc >= 1 ? to_string_value(xt_arg(argc, argv, 0)) : "";
  /* Changing the source discards the decoder; the frame loop reopens it. */
  xt_gui_media_forget(win, node);
  if (value.empty()) {
    remove_attribute_raw(node, "src");
    mark_dirty(win);
  } else {
    set_attribute(win, node, "src", value);
  }
  return XT_UNDEFINED;
}

#undef METHOD

}  // namespace domapi
