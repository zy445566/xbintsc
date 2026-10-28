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

#include <memory>
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

enum class BackgroundKind { Linear, Radial, Image };

/** How a radial layer's radii are chosen when it has no explicit lengths. */
enum class RadialSize { ClosestSide, FarthestSide, ClosestCorner, FarthestCorner, Explicit };

/** One `background-image` layer. Layers are painted bottom-up: the first one in
 * the declaration is the topmost, as in CSS. A gradient is approximated with
 * solid bands through the rectangle pipeline (there is no gradient shader). */
struct BackgroundLayer {
  BackgroundKind kind = BackgroundKind::Linear;
  /* linear */
  float angle = 180.0f;  // CSS degrees: 0 = to top, 90 = to right, 180 = to bottom
  /* radial */
  bool circle = false;
  bool has_shape = false;
  RadialSize size = RadialSize::FarthestCorner;
  Length center_x{50.0f, Unit::Percent};
  Length center_y{50.0f, Unit::Percent};
  Length radius_x{0.0f, Unit::Auto};  // Explicit radii only
  Length radius_y{0.0f, Unit::Auto};
  /** `BackgroundKind::Image`: the `url(...)` source (a path or data URL). */
  std::string image_src;
  std::vector<BackgroundGradientStop> stops;
};

/** `clip-path` shapes. Lengths stay unresolved until paint knows the box. */
struct ClipPathSpec {
  enum class Type { None, Inset, Circle, Ellipse, Polygon } type = Type::None;
  Length inset_top;
  Length inset_right;
  Length inset_bottom;
  Length inset_left;
  Length inset_radius{0.0f, Unit::Px};
  Length center_x{50.0f, Unit::Percent};
  Length center_y{50.0f, Unit::Percent};
  Length radius_x{50.0f, Unit::Percent};
  Length radius_y{50.0f, Unit::Percent};
  bool radius_explicit = false;
  /** Flat `x0, y0, x1, y1, …` list, resolved against the border box. */
  std::vector<Length> polygon;
};

enum class FilterKind {
  Blur,
  Brightness,
  Contrast,
  Grayscale,
  Sepia,
  Invert,
  Saturate,
  HueRotate,
  Opacity,
  DropShadow
};

/** One `filter` / `backdrop-filter` function. */
struct FilterOp {
  FilterKind kind = FilterKind::Brightness;
  float amount = 1.0f;
  Length dx;
  Length dy;
  Length blur;
  Color color;
  bool has_color = false;
};

/** One `text-shadow` entry. */
struct TextShadow {
  Length dx;
  Length dy;
  Length blur;
  Color color;  // `has_color == false` means currentColor
  bool has_color = false;
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
  /** `background-image` layers, in declaration order (first = topmost). */
  std::vector<BackgroundLayer> background_layers;
  /** `clip-path`; `Type::None` leaves the box unclipped. */
  ClipPathSpec clip_path;
  /** `filter` / `backdrop-filter` function lists (not inherited). */
  std::vector<FilterOp> filters;
  std::vector<FilterOp> backdrop_filters;
  /** `text-shadow` entries (inherited, as in CSS). */
  std::vector<TextShadow> text_shadow;
  /** Generated `content` of a `::before`/`::after` box. The cascade resolves
   * `attr()` and string escapes before layout; `has_content` distinguishes
   * `content: ""` from no generated box. */
  std::string content;
  bool has_content = false;

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

/** Owns the synthetic `::before`/`::after` nodes of one style computation. The
 * arena is rebuilt on every restyle, so generated content always reflects the
 * latest DOM and CSS. */
class PseudoNodeSet {
 public:
  /** Release the previous generation (the caller must already have cleared the
   * element → pseudo pointers it handed out). */
  void clear();
  Node *create(Node *origin, PseudoElement side);
  size_t size() const { return nodes_.size(); }

 private:
  std::vector<std::unique_ptr<Node>> nodes_;
};

/** Cascade UA + author + inline declarations into `out` for the document root.
 * `state` supplies `:hover`/`:focus` for dynamic selector matching. When
 * `pseudos` is non-null, `::before`/`::after` styles are computed too and each
 * element's transient `pseudo_before`/`pseudo_after` pointers are published for
 * layout. When `hasTransitions` is non-null it is OR-ed with whether any
 * computed style declares a `transition` (inline `style="…"` included). */
void xt_style_compute(const Node *root, const StyleSheet &sheet,
                      std::unordered_map<const Node *, XtStyle> &out, float viewportWidth,
                      float viewportHeight, const MatchState &state = MatchState(),
                      PseudoNodeSet *pseudos = nullptr, bool *hasTransitions = nullptr);

/** Format a computed property for diagnostics/tests (`computedStyle(sel, prop)`). */
std::string xt_style_property_to_string(const XtStyle &style, const std::string &property);

/** Format a length the way `computedStyle` does (`4px`, `50%`, `1.5`). */
std::string xt_style_length_to_string(const Length &length);

}  // namespace xtgui

#endif /* XT_GUI_STYLE_H */
