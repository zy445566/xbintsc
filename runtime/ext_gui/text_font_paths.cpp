/*
 * xbintsc GUI engine — platform font locations (see text_font_paths.h).
 */

#include "text_font_paths.h"

#include <cstdlib>

namespace xtgui {
namespace {

const std::vector<std::string> &sansPaths() {
  static const std::vector<std::string> paths = {
      /* macOS: CJK-capable faces first, then the Latin families. */
      "/System/Library/Fonts/PingFang.ttc",
      "/System/Library/Fonts/STHeiti Light.ttc",
      "/System/Library/Fonts/STHeiti Medium.ttc",
      "/System/Library/Fonts/Hiragino Sans GB.ttc",
      "/System/Library/Fonts/Helvetica.ttc",
      "/System/Library/Fonts/HelveticaNeue.ttc",
      "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
      "/System/Library/Fonts/Supplemental/Arial Italic.ttf",
      "/System/Library/Fonts/Supplemental/Arial Bold Italic.ttf",
      "/System/Library/Fonts/Supplemental/Verdana.ttf",
      "/System/Library/Fonts/Supplemental/Verdana Bold.ttf",
      "/System/Library/Fonts/Supplemental/Verdana Italic.ttf",
      "/System/Library/Fonts/Supplemental/Verdana Bold Italic.ttf",
      "/System/Library/Fonts/SFNS.ttf",
      "/System/Library/Fonts/SFNSItalic.ttf",
      "/Library/Fonts/Arial.ttf",
      /* Windows. */
      "C:\\Windows\\Fonts\\msyh.ttc",
      "C:\\Windows\\Fonts\\msyhbd.ttc",
      "C:\\Windows\\Fonts\\msyhl.ttc",
      "C:\\Windows\\Fonts\\msyh.ttf",
      "C:\\Windows\\Fonts\\Deng.ttf",
      "C:\\Windows\\Fonts\\Dengb.ttf",
      "C:\\Windows\\Fonts\\simsun.ttc",
      "C:\\Windows\\Fonts\\segoeui.ttf",
      "C:\\Windows\\Fonts\\segoeuib.ttf",
      "C:\\Windows\\Fonts\\segoeuii.ttf",
      "C:\\Windows\\Fonts\\segoeuiz.ttf",
      "C:\\Windows\\Fonts\\tahoma.ttf",
      "C:\\Windows\\Fonts\\tahomabd.ttf",
      "C:\\Windows\\Fonts\\arial.ttf",
      "C:\\Windows\\Fonts\\arialbd.ttf",
      "C:\\Windows\\Fonts\\ariali.ttf",
      "C:\\Windows\\Fonts\\arialbi.ttf",
      /* Linux. */
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
      "/usr/share/fonts/opentype/noto/NotoSansCJK-Light.ttc",
      "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
      "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
      "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Bold.ttc",
      "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
      "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-Oblique.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans-BoldOblique.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Italic.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-BoldItalic.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationSans-Bold.ttf",
  };
  return paths;
}

const std::vector<std::string> &serifPaths() {
  static const std::vector<std::string> paths = {
      "/System/Library/Fonts/Supplemental/Times New Roman.ttf",
      "/System/Library/Fonts/Supplemental/Times New Roman Bold.ttf",
      "/System/Library/Fonts/Supplemental/Times New Roman Italic.ttf",
      "/System/Library/Fonts/Supplemental/Times New Roman Bold Italic.ttf",
      "/System/Library/Fonts/Supplemental/Georgia.ttf",
      "/System/Library/Fonts/Supplemental/Georgia Bold.ttf",
      "/System/Library/Fonts/Supplemental/Georgia Italic.ttf",
      "/System/Library/Fonts/Supplemental/Georgia Bold Italic.ttf",
      "/System/Library/Fonts/Palatino.ttc",
      "/System/Library/Fonts/NewYork.ttf",
      "/System/Library/Fonts/NewYorkItalic.ttf",
      "/System/Library/Fonts/Songti.ttc",
      "C:\\Windows\\Fonts\\times.ttf",
      "C:\\Windows\\Fonts\\timesbd.ttf",
      "C:\\Windows\\Fonts\\timesi.ttf",
      "C:\\Windows\\Fonts\\timesbi.ttf",
      "C:\\Windows\\Fonts\\georgia.ttf",
      "C:\\Windows\\Fonts\\georgiab.ttf",
      "C:\\Windows\\Fonts\\georgiai.ttf",
      "C:\\Windows\\Fonts\\georgiaz.ttf",
      "C:\\Windows\\Fonts\\constan.ttf",
      "C:\\Windows\\Fonts\\constanb.ttf",
      "C:\\Windows\\Fonts\\simsun.ttc",
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSerif-BoldItalic.ttf",
      "/usr/share/fonts/TTF/DejaVuSerif.ttf",
      "/usr/share/fonts/dejavu/DejaVuSerif.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-Italic.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSerif-BoldItalic.ttf",
      "/usr/share/fonts/opentype/noto/NotoSerifCJK-Regular.ttc",
      "/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc",
      "/usr/share/fonts/truetype/noto/NotoSerifCJK-Regular.ttc",
      "/usr/share/fonts/truetype/noto/NotoSerifCJK-Bold.ttc",
  };
  return paths;
}

const std::vector<std::string> &monoPaths() {
  static const std::vector<std::string> paths = {
      "/System/Library/Fonts/Menlo.ttc",
      "/System/Library/Fonts/Courier.ttc",
      "/System/Library/Fonts/Supplemental/Courier New.ttf",
      "/System/Library/Fonts/Supplemental/Courier New Bold.ttf",
      "/System/Library/Fonts/Supplemental/Courier New Italic.ttf",
      "/System/Library/Fonts/Supplemental/Courier New Bold Italic.ttf",
      "/System/Library/Fonts/SFNSMono.ttf",
      "/Library/Fonts/Courier New.ttf",
      "C:\\Windows\\Fonts\\consola.ttf",
      "C:\\Windows\\Fonts\\consolab.ttf",
      "C:\\Windows\\Fonts\\consolai.ttf",
      "C:\\Windows\\Fonts\\consolaz.ttf",
      "C:\\Windows\\Fonts\\cour.ttf",
      "C:\\Windows\\Fonts\\courbd.ttf",
      "C:\\Windows\\Fonts\\couri.ttf",
      "C:\\Windows\\Fonts\\courbi.ttf",
      "C:\\Windows\\Fonts\\lucon.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Oblique.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-BoldOblique.ttf",
      "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
      "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-Italic.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationMono-BoldItalic.ttf",
      "/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSansMono-Bold.ttf",
  };
  return paths;
}

const std::vector<std::string> &cursivePaths() {
  static const std::vector<std::string> paths = {
      "/System/Library/Fonts/Supplemental/Apple Chancery.ttf",
      "/System/Library/Fonts/Supplemental/SnellRoundhand.ttc",
      "C:\\Windows\\Fonts\\segoesc.ttf",
      "C:\\Windows\\Fonts\\comic.ttf",
      "C:\\Windows\\Fonts\\comicbd.ttf",
      "/usr/share/fonts/truetype/urw-base35/Z003-MediumItalic.ttf",
      "/usr/share/fonts/type1/urw-base35/Z003-MediumItalic.t1",
  };
  return paths;
}

const std::vector<std::string> &fantasyPaths() {
  static const std::vector<std::string> paths = {
      "/System/Library/Fonts/Supplemental/Papyrus.ttc",
      "/System/Library/Fonts/Supplemental/Chalkduster.ttf",
      "/System/Library/Fonts/Supplemental/Impact.ttf",
      "C:\\Windows\\Fonts\\papyrus.ttf",
      "C:\\Windows\\Fonts\\impact.ttf",
      "C:\\Windows\\Fonts\\comic.ttf",
      "/usr/share/fonts/truetype/urw-base35/Z003-MediumItalic.ttf",
  };
  return paths;
}

}  // namespace

const std::vector<std::string> &xt_font_default_paths(FontGeneric kind) {
  switch (kind) {
    case FontGeneric::Serif:
      return serifPaths();
    case FontGeneric::Monospace:
      return monoPaths();
    case FontGeneric::Cursive:
      return cursivePaths();
    case FontGeneric::Fantasy:
      return fantasyPaths();
    case FontGeneric::Sans:
      break;
  }
  return sansPaths();
}

const std::vector<std::string> &xt_font_symbol_paths() {
  static const std::vector<std::string> paths = {
      "C:\\Windows\\Fonts\\seguisym.ttf",
      "C:\\Windows\\Fonts\\arialuni.ttf",
      "/System/Library/Fonts/Apple Symbols.ttf",
      "/System/Library/Fonts/CJKSymbolsFallback.ttc",
      "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
      "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSansSymbols-Regular.ttf",
      "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
  };
  return paths;
}

std::vector<std::string> xt_font_search_directories() {
  std::vector<std::string> dirs;
  auto add = [&dirs](const std::string &path) {
    if (path.empty()) return;
    for (const std::string &entry : dirs) {
      if (entry == path) return;
    }
    dirs.push_back(path);
  };
  const char *home = std::getenv("HOME");
  if (home == nullptr || home[0] == '\0') home = std::getenv("USERPROFILE");

  if (std::getenv("WINDIR") != nullptr || std::getenv("SystemRoot") != nullptr) {
    const char *windir = std::getenv("WINDIR");
    if (windir == nullptr || windir[0] == '\0') windir = std::getenv("SystemRoot");
    if (windir != nullptr && windir[0] != '\0') add(std::string(windir) + "\\Fonts");
    const char *local = std::getenv("LOCALAPPDATA");
    if (local != nullptr && local[0] != '\0') add(std::string(local) + "\\Microsoft\\Windows\\Fonts");
  } else {
    add("/System/Library/Fonts");
    add("/System/Library/Fonts/Supplemental");
    add("/Library/Fonts");
    if (home != nullptr && home[0] != '\0') add(std::string(home) + "/Library/Fonts");
    add("/usr/share/fonts");
    add("/usr/local/share/fonts");
    if (home != nullptr && home[0] != '\0') {
      add(std::string(home) + "/.fonts");
      add(std::string(home) + "/.local/share/fonts");
    }
    const char *xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && xdg[0] != '\0') add(std::string(xdg) + "/fonts");
  }
  return dirs;
}

}  // namespace xtgui
