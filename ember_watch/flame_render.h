#ifndef FLAME_RENDER_H
#define FLAME_RENDER_H

#include <Adafruit_GFX.h>

enum EmberLevel {
  EMBER_NONE,      // nearest known fire is beyond SEARCH_RADIUS_KM
  EMBER_LOW,       // within radius, but distant and/or wind not carrying it your way
  EMBER_ELEVATED,  // in your area, or in your wider region with wind toward you
  EMBER_HIGH,      // close, and wind carrying it your way
  EMBER_EXTREME,   // very close and confirmed
  EMBER_UNKNOWN,   // no reading at all yet - cold boot, no WiFi, nothing cached
};

// Cold-boot/pre-fetch loading screen, shown immediately (before any network
// call) so the several-second WiFi+API wait doesn't look like a frozen
// blank screen.
void render_splash(GFXcanvas1 *canvas);

// One flame mascot (a generated icon, see ember_icons.h/gen_icons.py - size
// and escalation communicate danger before anyone reads a word) plus the
// nearest fire's details - distance_km/direction/size_word/conf_word are
// shown for EVERY level, including EMBER_NONE, so "all clear" always comes
// with "how far is the nearest one" rather than no information at all. tag
// is one of "(live)", "(cached)", "" - an honest-provenance label.
void render_flame(GFXcanvas1 *canvas, EmberLevel level, float distance_km,
                   const char *direction, const char *size_word,
                   const char *conf_word, bool wind_toward,
                   const String &message, const char *tag);

#endif  // !FLAME_RENDER_H
