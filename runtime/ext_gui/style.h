/*
 * xbintsc GUI engine — computed style (cascade + inheritance).
 *
 * `XtStyle` is the resolved style a layout pass consumes. Lengths keep their
 * unit here (%), em, ...) because resolving them needs the containing block and
 * font size, which only layout knows. `xt_style_compute` fills a map of
 * element → computed style from the UA sheet, the author sheet and `style=""`
 * attributes, in cascade order.
 */
#ifndef XT_GUI_STYLE_H
#define XT_GUI_STYLE_H

#include <string>
#include <unordered_map>
#include <vector>

#include "css.h"
#include "dom.h"
#include "transform.h"

namespace xtgui {

enum class Display { None, Block, Inline, InlineBlock, Flex, Grid, ListItem };
enum class BoxSizing { ContentBox, BorderBox };
enum class Position { Static, Relative, Absolute, Fixed };
enum class FlexDirection { Row, RowReverse, Column, ColumnReverse };
enum class FlexWrap { NoWrap, Wrap, WrapReverse };
enum class JustifyContent { FlexStart, FlexEnd, Center, SpaceBetween, SpaceAround, SpaceEvenly };
enum class AlignItems { Stretch, FlexStart, FlexEnd, Center, Baseline };
enum class AlignContent { Stretch, FlexStart, FlexEnd, Center, SpaceBetween, SpaceAround };
enum class FloatSide { None, Left, Right };
enum class Clear { None, Left, Right, Both };
enum class TextAlign { Start, Center, End, Justify };
enum class WhiteSpace { Normal, NoWrap, Pre, PreWrap, PreLine };
enum class Overflow { Visible, Hidden, Scroll, Auto };
enum class BorderStyle { None, Solid, Dashed, Dotted };
enum class FontStyle { Normal, Italic };
enum class TimingFunction { Linear, Ease, EaseIn, EaseOut, EaseInOut };

/** One `transition` entry: which property, how long, how it eases. */
struct TransitionSpec {
  std::string property = "all";
  float duration = 0.0f;  // seconds
  float delay = 0.0f;     // seconds
  TimingFunction timing = TimingFunction::Ease;
};

/** One track of a `grid-template-columns` / `grid-template-rows` list.
 * `min`/`max` keep their CSS unit (`auto` for an intrinsic bound); `fr` is the
 * flex fraction of a `minmax(..., <n>fr)` or `<n>fr` track. */
struct GridTrack {
  Length min{0.0f, Unit::Auto};
  Length max{0.0f, Unit::Auto};
  float fr = 0.0f;
};

struct BackgroundGradientStop {
  float offset = 0.0f;
  Color color;
};

/** A linear `background-image` gradient. Multiple background layers and radial
 * gradients are not modelled (the first linear layer is the base colour of
 * every panel/bar in an application shell; decorative radial layers are
 * skipped). Painted as a small number of solid strips. */
struct BackgroundGradient {
  bool active = false;
  float angle = 180.0f;  // CSS degrees: 0 = to top, 90 = to right, 180 = to bottom
  std::vector<BackgroundGradientStop> stops;
};

struct Edges {
  Length top;
  Length right;
  Length bottom;
  Length left;

  void set(Length value) { top = right = bottom = left = value; }
};

struct XtStyle {
  Display display = Display::Inline;
  BoxSizing box_sizing = BoxSizing::ContentBox;
  /* CSS custom properties (`--name`) are inherited and substituted into the
   * values of the other declarations during the cascade. */
  std::unordered_map<std::string, std::string> variables;
  Position position = Position::Static;
  Edges margin;
  Edges padding;
  Edges border_width;
  BorderStyle border_style = BorderStyle::None;
  Color border_color;
  bool has_border_color = false;
  float border_radius = 0.0f;

  Length width;
  Length height;
  Length min_width;
  Length min_height;
  Length max_width;
  Length max_height;
  Length top;
  Length right;
  Length bottom;
  Length left;

  bool visible = true;           // inherited `visibility`
  bool pointer_events = true;    // inherited `pointer-events`
  Color color;             // inherited; defaults to black
  Color background;        // transparent by default
  bool has_background = false;
  BackgroundGradient background_gradient;

  float font_size = 16.0f;             // inherited
  int font_weight = 400;               // inherited
  std::string font_family = "sans-serif";  // inherited
  FontStyle font_style = FontStyle::Normal;  // inherited
  Length line_height;                  // Auto = normal; Number = multiplier

  TextAlign text_align = TextAlign::Start;  // inherited
  WhiteSpace white_space = WhiteSpace::Normal;  // inherited

  FlexDirection flex_direction = FlexDirection::Row;
  FlexWrap flex_wrap = FlexWrap::NoWrap;
  JustifyContent justify_content = JustifyContent::FlexStart;
  AlignItems align_items = AlignItems::Stretch;
  AlignContent align_content = AlignContent::Stretch;
  /* Grid (a focused subset: explicit/implicit rows and columns, gap, item
   * alignment and simple line placement). */
  std::vector<GridTrack> grid_columns;
  std::vector<GridTrack> grid_rows;
  AlignItems justify_items = AlignItems::Stretch;
  bool has_justify_self = false;
  AlignItems justify_self = AlignItems::Stretch;
  bool has_align_self = false;
  AlignItems align_self = AlignItems::Stretch;
  int grid_column_start = 0;
  int grid_column_end = 0;
  int grid_row_start = 0;
  int grid_row_end = 0;
  Length gap;
  Length row_gap;
  Length column_gap;
  float flex_grow = 0.0f;
  float flex_shrink = 1.0f;
  Length flex_basis;

  float opacity = 1.0f;
  Overflow overflow = Overflow::Visible;
  int z_index = 0;
  bool has_z_index = false;

  FloatSide float_side = FloatSide::None;
  Clear clear = Clear::None;

  /** `transform` (not inherited) and `transform-origin` (defaults to the centre
   * of the border box). Layout resolves the list into one matrix per box. */
  std::vector<TransformOp> transform;
  Length transform_origin_x{50.0f, Unit::Percent};
  Length transform_origin_y{50.0f, Unit::Percent};

  /** `transition` specs; not inherited. Applied by `XtDocument`. */
  std::vector<TransitionSpec> transitions;
};

/** Initial (non-inherited) style; inherited fields are overridden from the parent. */
XtStyle xt_style_initial();

/** Cascade UA + author + inline declarations into `out` for the document root.
 * `state` supplies `:hover`/`:focus` for dynamic selector matching. */
void xt_style_compute(const Node *root, const StyleSheet &sheet,
                      std::unordered_map<const Node *, XtStyle> &out, float viewportWidth,
                      float viewportHeight, const MatchState &state = MatchState());

/** Format a computed property for diagnostics/tests (`computedStyle(sel, prop)`). */
std::string xt_style_property_to_string(const XtStyle &style, const std::string &property);

/** Format a length the way `computedStyle` does (`4px`, `50%`, `1.5`). */
std::string xt_style_length_to_string(const Length &length);

}  // namespace xtgui

#endif /* XT_GUI_STYLE_H */
