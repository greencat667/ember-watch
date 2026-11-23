// Ember Watch's mascot: a generated flame icon (see tools/gen_icons.py -
// an AI image generator, autocropped and thresholded to 1bpp, packed into
// ember_icons.h) whose SEVERITY escalates from a calm checkmark through a
// small campfire to a raging inferno - "shape first, numbers second". The
// icons are AI-generated art rather than hand-drawn primitives.
//
// Two icon sizes, not one - STATUS_ICON_SIZE for these six severity
// mascots (kept smaller so the info text below has real room) and a
// bigger SPLASH_ICON_SIZE for the one-time boot screen. Both generated at
// exactly the size drawn here via drawBitmap() - deliberately not one size
// scaled at another,.
#include "flame_render.h"
#include "ember_config.h"
#include "ember_icons.h"
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <vector>

#define CANVAS_SIZE 200

// ---- text layout --------------------------------------------------------

static std::vector<String> wrap_lines(GFXcanvas1 *c, const String &text, int text_size, int max_width) {
  c->setFont(nullptr);
  c->setTextSize(text_size);
  std::vector<String> lines;
  String line;
  int start = 0, len = text.length();
  while (start < len) {
    int space_idx = text.indexOf(' ', start);
    String word = (space_idx == -1) ? text.substring(start) : text.substring(start, space_idx);
    String candidate = line.length() ? (line + " " + word) : word;
    int16_t x1, y1;
    uint16_t w, h;
    c->getTextBounds(candidate, 0, 0, &x1, &y1, &w, &h);
    if (w > (uint16_t)max_width && line.length()) {
      lines.push_back(line);
      line = word;
    } else {
      line = candidate;
    }
    start = (space_idx == -1) ? len : space_idx + 1;
  }
  if (line.length()) lines.push_back(line);
  return lines;
}

static void draw_lines_centered(GFXcanvas1 *c, const std::vector<String> &lines, int text_size,
                                 int top_y, int line_height) {
  c->setFont(nullptr);
  c->setTextSize(text_size);
  int cursor_top = top_y;
  for (auto &ln : lines) {
    int16_t x1, y1;
    uint16_t w, h;
    c->getTextBounds(ln, 0, 0, &x1, &y1, &w, &h);
    int cx = (CANVAS_SIZE - (int)w) / 2 - x1;
    c->setCursor(cx, cursor_top - y1);
    c->print(ln);
    cursor_top += line_height;
  }
}

// Renders each paragraph at a FIXED text size (wrapping individually as
// needed rather than shrinking the whole block if any one paragraph is
// long - "extreme, megafire-scale"-length words used to drag every line
// down to the smallest size even though most lines were short enough on
// their own). Returns the y position just after the last line, so a
// caller can stack another block beneath it.
static int draw_fixed_stack(GFXcanvas1 *c, const String paragraphs[], int count, int text_size, int top_y) {
  int line_height = (text_size == 2) ? 15 : 10;
  int y = top_y;
  for (int i = 0; i < count; i++) {
    if (paragraphs[i].length() == 0) continue;
    auto lines = wrap_lines(c, paragraphs[i], text_size, CANVAS_SIZE - 16);
    draw_lines_centered(c, lines, text_size, y, line_height);
    y += lines.size() * line_height;
  }
  return y;
}

// One paragraph, adaptively sized: try textSize 2 first, fall back to 1
// only if it genuinely doesn't fit in [top_y, bottom_y] - for supplementary
// text (the danger message) where the fixed stats above already guarantee
// legible size regardless.
static void draw_adaptive(GFXcanvas1 *c, const String &text, int top_y, int bottom_y) {
  if (text.length() == 0) return;
  for (int size = 2; size >= 1; size--) {
    int line_height = (size == 2) ? 15 : 10;
    auto lines = wrap_lines(c, text, size, CANVAS_SIZE - 16);
    if ((int)lines.size() * line_height <= (bottom_y - top_y) || size == 1) {
      draw_lines_centered(c, lines, size, top_y, line_height);
      return;
    }
  }
}

static void draw_icon(GFXcanvas1 *c, const uint8_t *bitmap, int size, int top_y) {
  int left = (CANVAS_SIZE - size) / 2;
  c->drawBitmap(left, top_y, bitmap, size, size, 1);
}

void render_splash(GFXcanvas1 *canvas) {
  canvas->fillScreen(0);
  canvas->setTextColor(1);
  canvas->setTextWrap(false);

  draw_icon(canvas, ICON_SPLASH, SPLASH_ICON_SIZE, 6);

  static const char *title = "EMBER WATCH";
  static const GFXfont *TITLE_FONTS[] = {&FreeSansBold18pt7b, &FreeSansBold12pt7b};
  int16_t x1, y1;
  uint16_t w, h;
  const GFXfont *title_font = TITLE_FONTS[1];
  for (int i = 0; i < 2; i++) {
    canvas->setFont(TITLE_FONTS[i]);
    canvas->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
    if ((int)w <= CANVAS_SIZE - 16) {
      title_font = TITLE_FONTS[i];
      break;
    }
  }
  canvas->setFont(title_font);
  canvas->getTextBounds(title, 0, 0, &x1, &y1, &w, &h);
  canvas->setCursor((CANVAS_SIZE - (int)w) / 2 - x1, 166 - y1);
  canvas->print(title);
  canvas->setFont(nullptr);

  auto lines = wrap_lines(canvas, "checking for wildfires...", 1, CANVAS_SIZE - 16);
  draw_lines_centered(canvas, lines, 1, 186, 10);
}

void render_flame(GFXcanvas1 *canvas, EmberLevel level, float distance_km,
                   const char *direction, const char *size_word,
                   const char *conf_word, bool wind_toward,
                   const String &message, const char *tag) {
  canvas->fillScreen(0);
  canvas->setTextColor(1);
  canvas->setTextWrap(false);

  static const char *BAND_WORDS[] = {"ALL CLEAR", "LOW", "ELEVATED", "HIGH", "EXTREME", "NO DATA"};
  const char *band = BAND_WORDS[level];
  static const GFXfont *TITLE_FONTS[] = {&FreeSansBold18pt7b, &FreeSansBold12pt7b, &FreeSansBold9pt7b};
  int16_t x1, y1;
  uint16_t w, h;
  const GFXfont *title_font = TITLE_FONTS[2];
  for (int i = 0; i < 3; i++) {
    canvas->setFont(TITLE_FONTS[i]);
    canvas->getTextBounds(band, 0, 0, &x1, &y1, &w, &h);
    if ((int)w <= CANVAS_SIZE - 16) {
      title_font = TITLE_FONTS[i];
      break;
    }
  }
  canvas->setFont(title_font);
  canvas->getTextBounds(band, 0, 0, &x1, &y1, &w, &h);
  canvas->setCursor((CANVAS_SIZE - (int)w) / 2 - x1, 20 - y1);
  canvas->print(band);
  canvas->setFont(nullptr);

  static const uint8_t *ICONS[] = {ICON_ALL_CLEAR, ICON_LOW, ICON_ELEVATED, ICON_HIGH, ICON_EXTREME, ICON_UNKNOWN};
  // Derived from the banner's own rendered height (not a fixed constant) -
  // a short word like "NO DATA" gets the largest banner font (it easily
  // fits the width budget), so it's taller than most other band words and
  // was found live to crowd the icon when a flat gap was used instead.
  const int icon_top = 20 + (int)h + 8;
  draw_icon(canvas, ICONS[level], STATUS_ICON_SIZE, icon_top);

  int y = icon_top + STATUS_ICON_SIZE + 6;
  if (level == EMBER_UNKNOWN) {
    draw_adaptive(canvas, message, y, 198);
  } else {
    char line1[40];
    snprintf(line1, sizeof(line1), "%.0f km %s", distance_km, direction);
    char line2[48];
    snprintf(line2, sizeof(line2), "%s, %s", size_word, conf_word);
    const char *wind_str = wind_toward ? "wind: toward you" : "wind: away";
    String stats[3] = {String(line1), String(line2), String(wind_str)};
    y = draw_fixed_stack(canvas, stats, 3, 2, y);
    draw_adaptive(canvas, message, y + 2, 198);
  }

  if (tag[0]) {
    canvas->setFont(nullptr);
    canvas->setTextSize(1);
    canvas->getTextBounds(tag, 0, 0, &x1, &y1, &w, &h);
    canvas->setCursor(CANVAS_SIZE - (int)w - 4, CANVAS_SIZE - (int)h - 4);
    canvas->print(tag);
  }
}
