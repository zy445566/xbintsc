/*
 * xbintsc GUI engine — shared declarations for the layout translation units
 * (`layout.cpp`, `layout_flow.cpp`, `layout_inline.cpp`, `layout_flex.cpp`).
 */
#ifndef XT_GUI_LAYOUT_INTERNAL_H
#define XT_GUI_LAYOUT_INTERNAL_H

#include <unordered_map>

#include "layout.h"

namespace xtgui {

/** Lay out an already-built box tree starting at `root`. */
void xt_layout_run(LayoutBox *root, const std::unordered_map<const Node *, XtStyle> &styles,
                   float viewportWidth, float viewportHeight);

}  // namespace xtgui

#endif /* XT_GUI_LAYOUT_INTERNAL_H */
