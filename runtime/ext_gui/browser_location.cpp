/*
 * xbintsc GUI engine — the `location` object.
 *
 * Split out of `browser.cpp` to keep that unit within the repository's
 * file-length budget. The interesting part is `XT_GUI_URL`: a page addresses
 * itself through `location.search`/`location.hash` (`?bench=1`, `?q=low`,
 * `#shot`), and a compiled program has no browser address bar to supply them.
 */

#include "gui_engine.h"

#include <string>

namespace {

xt_value string_of(const char *text) { return xt_string_from_cstr(text != NULL ? text : ""); }

xt_value fn_of(void *fn) { return xt_closure_new(fn, 0, NULL); }

void set_prop(xt_value target, const char *name, xt_value value) {
  xt_object_set(target, xt_string_from_cstr(name), value);
}

void set_method(xt_value target, const char *name, void *fn) { set_prop(target, name, fn_of(fn)); }

xt_value noop(xt_value self, xt_value env, int32_t argc, xt_value *argv) {
  (void)self;
  (void)env;
  (void)argc;
  (void)argv;
  return XT_UNDEFINED;
}

}  // namespace

xt_value xt_gui_location_build(void) {
  xt_value location = xt_object_new();
  std::string pathname = "/";
  std::string search;
  std::string hash;
  /* `XT_GUI_URL` may be a bare `?query`/`#hash` or a `path?query#hash`; the
   * origin stays `xbintsc://localhost`. Unset keeps the historical empty URL. */
  const char *configured = SDL_getenv("XT_GUI_URL");
  if (configured != NULL && configured[0] != '\0') {
    std::string rest(configured);
    const size_t scheme = rest.find("://");
    if (scheme != std::string::npos) {
      const size_t slash = rest.find('/', scheme + 3);
      rest = slash == std::string::npos ? std::string() : rest.substr(slash);
    }
    const size_t hash_at = rest.find('#');
    if (hash_at != std::string::npos) {
      hash = rest.substr(hash_at);
      rest.erase(hash_at);
    }
    const size_t query_at = rest.find('?');
    if (query_at != std::string::npos) {
      search = rest.substr(query_at);
      rest.erase(query_at);
    }
    if (!rest.empty()) pathname = rest[0] == '/' ? rest : "/" + rest;
  }
  const std::string href = "xbintsc://localhost" + pathname + search + hash;
  set_prop(location, "href", string_of(href.c_str()));
  set_prop(location, "protocol", string_of("xbintsc:"));
  set_prop(location, "host", string_of("localhost"));
  set_prop(location, "hostname", string_of("localhost"));
  set_prop(location, "port", string_of(""));
  set_prop(location, "pathname", string_of(pathname.c_str()));
  set_prop(location, "search", string_of(search.c_str()));
  set_prop(location, "hash", string_of(hash.c_str()));
  set_prop(location, "origin", string_of("xbintsc://localhost"));
  set_method(location, "reload", (void *)noop);
  set_method(location, "assign", (void *)noop);
  set_method(location, "replace", (void *)noop);
  set_prop(location, "toString", fn_of((void *)noop));
  return location;
}
