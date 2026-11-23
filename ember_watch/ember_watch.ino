// Ember Watch - a standalone wildfire proximity display. No relay, no computer,
// no LLM: once flashed with real WiFi credentials (secrets.h) this device
// talks directly to three free, keyless public APIs every wake (or on a
// button press) and decides, on-device, whether an active wildfire is close
// enough to care about:
//
//   1. ipinfo.io           - approximates "where am I" from this network's
//                            public IP (whatever WiFi it's joined to), or
//                            OVERRIDE_LAT/LON in ember_config.h if set.
//   2. kanari.io/api/events - global near-real-time wildfire detections
//                             (satellite + AI-verified witness reports),
//                             free and open, no key. No location filter on
//                             their side, so this device downloads the
//                             (filtered-down) global feed and finds the
//                             nearest fire itself.
//   3. api.open-meteo.com   - live wind direction at this location, to work
//                             out whether that fire's smoke is actually
//                             blowing this way.
//
// classify_danger() below is a small deterministic rule engine - not an
// LLM, "reliability over flavour" - combining distance,
// Kanari's own confidence rating (passed through unchanged, never
// re-derived) and wind alignment into one of five levels. The nearest
// fire's distance/direction/size/confidence are always shown, even under
// ALL CLEAR - "clear" describes the danger level, not an absence of info.
//
// IMPORTANT: this is a hobby awareness tool, not an emergency alert system.
// It has no relationship with any official fire service. Always follow
// local/official guidance over anything shown here - see README.md.
//
// Board: Waveshare ESP32-S3-ePaper-1.54 (V2).

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <math.h>

#include "user_config.h"
#include "secrets.h"
#include "ember_config.h"
#include "flame_render.h"

#include "src/display/epaper_driver_bsp.h"
#include "src/power/board_power_bsp.h"

struct Verdict {
  EmberLevel level;
  String message;
};

// Survives deep sleep so a WiFi/API miss falls back to the last good
// reading instead of a blank "no data" flame every single time.
RTC_DATA_ATTR static int cached_level = EMBER_UNKNOWN;
RTC_DATA_ATTR static float cached_distance = 0;
RTC_DATA_ATTR static char cached_direction[8] = "";
RTC_DATA_ATTR static char cached_size_word[24] = "";
RTC_DATA_ATTR static char cached_conf_word[16] = "";
RTC_DATA_ATTR static bool cached_wind_toward = false;
RTC_DATA_ATTR static char cached_message[96] = "";
RTC_DATA_ATTR static bool has_cached = false;

static board_power_bsp_t board_power(EPD_PWR_PIN, Audio_PWR_PIN, VBAT_PWR_PIN);
static epaper_driver_display *driver = nullptr;
static GFXcanvas1 *canvas = nullptr;
static bool wifi_ok = false;

static void connect_wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Connecting to WiFi '%s'", WIFI_SSID);
  unsigned long start = millis();
  // Widened from an initial 8s after repeated live failures on a real phone
  // hotspot (one success in several attempts) suggested the handshake
  // sometimes just needs more time, not that it was permanently unreachable.
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  wifi_ok = (WiFi.status() == WL_CONNECTED);
  if (wifi_ok) {
    Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.printf("No WiFi (status=%d) - falling back to cached reading, if any\n", (int)WiFi.status());
  }
}

// ---- geometry -------------------------------------------------------

static double haversine_km(double lat1, double lon1, double lat2, double lon2) {
  const double R = 6371.0;
  double p1 = radians(lat1), p2 = radians(lat2);
  double dphi = radians(lat2 - lat1);
  double dl = radians(lon2 - lon1);
  double a = sin(dphi / 2) * sin(dphi / 2) + cos(p1) * cos(p2) * sin(dl / 2) * sin(dl / 2);
  return 2 * R * asin(sqrt(a));
}

// Initial bearing from (lat1,lon1) to (lat2,lon2), degrees, 0-360.
static double bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  double p1 = radians(lat1), p2 = radians(lat2);
  double dl = radians(lon2 - lon1);
  double x = sin(dl) * cos(p2);
  double y = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double deg = degrees(atan2(x, y));
  return fmod(deg + 360.0, 360.0);
}

static const char *compass_from_deg(double deg) {
  static const char *POINTS[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                  "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
  int idx = ((int)round(deg / 22.5)) % 16;
  if (idx < 0) idx += 16;
  return POINTS[idx];
}

// Terse on purpose - these share a tight line with conf_word on-screen
// (see render_flame). "fire"/"megafire-scale" suffixes were trimmed after
// live testing showed the combined line needed to wrap even at the
// display's larger text size.
static const char *size_word_from_frp(double frp) {
  if (frp < 20) return "small";
  if (frp < 100) return "moderate";
  if (frp < 500) return "large";
  if (frp < 1500) return "very large";
  return "extreme";
}

static const char *conf_word_from_kanari(const String &c) {
  if (c == "corrobore") return "confirmed";
  if (c == "probable") return "likely";
  return "unconfirmed";
}

// Deterministic rule engine, most-urgent-first - not the LLM. distance_km
// and confidence come straight from Kanari; wind_toward comes from
// comparing Open-Meteo's live reading against the fire->you bearing. See
// ember_config.h for the distance-band thresholds. A fire beyond
// SEARCH_RADIUS_KM is EMBER_NONE ("all clear") - its distance is still
// shown (see render_flame), just not treated as a danger.
// Messages kept short on purpose - they render in a small on-screen block
// under the stats (see render_flame) and the full-length versions this
// started with ("Very close and confirmed - follow local emergency
// guidance now") were shown to overflow it in live testing.
static Verdict classify_danger(double distance_km, bool wind_toward, bool likely_confirmed) {
  if (distance_km > SEARCH_RADIUS_KM) {
    return {EMBER_NONE, "For reference only"};
  }
  if (distance_km <= DIST_BAND_VERY_CLOSE_KM && likely_confirmed) {
    return {EMBER_EXTREME, "Follow official guidance now"};
  }
  if (distance_km <= DIST_BAND_VERY_CLOSE_KM) {
    return {EMBER_HIGH, "Unconfirmed - stay alert"};
  }
  if (distance_km <= DIST_BAND_CLOSE_KM && wind_toward && likely_confirmed) {
    return {EMBER_HIGH, "Wind blowing your way"};
  }
  if (distance_km <= DIST_BAND_CLOSE_KM) {
    return {EMBER_ELEVATED, "Worth monitoring"};
  }
  if (distance_km <= DIST_BAND_REGIONAL_KM && wind_toward && likely_confirmed) {
    return {EMBER_ELEVATED, "Wind blowing your way"};
  }
  if (distance_km <= DIST_BAND_REGIONAL_KM) {
    return {EMBER_LOW, "No immediate concern"};
  }
  return {EMBER_LOW, "For awareness only"};
}

// ---- network ----------------------------------------------------------

// ipinfo.io: HTTPS, no key needed for a basic lookup (free tier, rate
// limited but ample for a device waking every 15 min). Originally
// ip-api.com's plain-HTTP endpoint - switched after finding, live, that a
// phone hotspot connected outbound HTTPS fine but refused this device's
// plain HTTP (port 80) request outright (HTTPClient error -1,
// CONNECTION_REFUSED) - not uncommon for mobile hotspots/carrier NAT, which
// often restrict 80 while leaving 443 open. ipinfo.io's "loc" field is a
// single "lat,lon" string rather than separate fields - split on the comma.
static bool fetch_location(double &lat, double &lon) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(GEO_TIMEOUT_MS);
  if (!http.begin(client, "https://ipinfo.io/json")) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getStream())) {
      String loc = String((const char *)(doc["loc"] | ""));
      int comma = loc.indexOf(',');
      if (comma > 0) {
        lat = loc.substring(0, comma).toDouble();
        lon = loc.substring(comma + 1).toDouble();
        ok = true;
        Serial.printf("[geo] %s, %s (%.3f, %.3f)\n",
                      (const char *)(doc["city"] | ""), (const char *)(doc["country"] | ""), lat, lon);
      }
    }
  } else {
    Serial.printf("[geo] GET failed, HTTP %d\n", code);
  }
  http.end();
  return ok;
}

// kanari.io: HTTPS, no key. Their /events feed is global with no location
// filter server-side (~550KB for 2000 events with full witness post text) -
// buffered via getString() (see chunked-encoding note below), then parsed
// into a FILTERED document (keeps only the handful of fields this app
// needs, so the unused witness-post text never lands in the parsed
// document even though it's briefly in the raw buffer), then scanned in
// C++ for the single nearest event - GLOBALLY, not gated by
// SEARCH_RADIUS_KM, so the headline view always has a real nearest
// distance to show even under "all clear" (classify_danger decides what
// that distance MEANS; this function just finds it). Returns false only on
// a genuine fetch/parse failure, not on "far away."
static bool fetch_kanari_nearest(double my_lat, double my_lon, double &out_dist_km,
                                  double &out_bearing, double &out_frp, String &out_confidence) {
  WiFiClientSecure client;
  client.setInsecure();  // no CA bundle carried on-device; accepted trade-off for a read-only public feed
  HTTPClient http;
  http.setTimeout(KANARI_TIMEOUT_MS);
  if (!http.begin(client, "https://kanari.io/api/events?hours=6")) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[kanari] GET failed, HTTP %d\n", code);
    http.end();
    return false;
  }

  // kanari.io serves this endpoint with Transfer-Encoding: chunked and no
  // Content-Length (confirmed live via curl) - getStream() hands back the
  // RAW transport including chunk-size framing bytes, which can fool a JSON
  // parser reading it directly (a chunk-size line that happens to be pure
  // hex digits can parse as a stray top-level JSON number, "succeeding"
  // with an empty/wrong document instead of erroring - this is exactly
  // what happened in live testing: "0 events scanned", no parse error).
  // getString() is HTTPClient's own battle-tested dechunking path - costs
  // buffering the ~550KB body in RAM instead of streaming, an accepted
  // trade for correctness given 8MB PSRAM is available.
  String payload = http.getString();
  http.end();
  Serial.printf("[kanari] payload length=%d bytes\n", payload.length());

  JsonDocument filter;
  filter["events"][0]["centroid"] = true;
  filter["events"][0]["maxFrp"] = true;
  filter["events"][0]["confidence"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[kanari] parse failed: %s\n", err.c_str());
    return false;
  }

  bool found = false;
  double best_dist = 1e18;
  for (JsonObject e : doc["events"].as<JsonArray>()) {
    double elon = e["centroid"][0] | 0.0;
    double elat = e["centroid"][1] | 0.0;
    double d = haversine_km(my_lat, my_lon, elat, elon);
    if (d < best_dist) {
      best_dist = d;
      out_bearing = bearing_deg(my_lat, my_lon, elat, elon);
      out_frp = e["maxFrp"] | 0.0;
      out_confidence = String((const char *)(e["confidence"] | ""));
      found = true;
    }
  }
  if (found) out_dist_km = best_dist;
  Serial.printf("[kanari] %d events scanned, nearest %s\n",
                doc["events"].as<JsonArray>().size(), found ? "found" : "not found (empty feed?)");
  return found;
}

// api.open-meteo.com: HTTPS, no key. wind_direction_10m is the direction
// the wind is blowing FROM (meteorological convention) - converted to
// "blowing toward" in setup() before comparing against the fire's bearing.
static bool fetch_wind(double lat, double lon, double &wind_from_deg) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(WEATHER_TIMEOUT_MS);
  char url[160];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=wind_direction_10m",
           lat, lon);
  if (!http.begin(client, url)) return false;
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    JsonDocument doc;
    if (!deserializeJson(doc, http.getStream())) {
      if (doc["current"]["wind_direction_10m"].is<double>()) {
        wind_from_deg = doc["current"]["wind_direction_10m"].as<double>();
        ok = true;
      }
    }
  } else {
    Serial.printf("[wind] GET failed, HTTP %d\n", code);
  }
  http.end();
  return ok;
}

// ---- display plumbing (standard for this board) --

static void commit_canvas() {
  for (int y = 0; y < EPD_HEIGHT; y++) {
    for (int x = 0; x < EPD_WIDTH; x++) {
      bool drawn = canvas->getPixel(x, y);
      driver->EPD_DrawColorPixel((uint16_t)x, (uint16_t)y, drawn ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE);
    }
  }
  driver->EPD_Display();
}

static void go_to_deep_sleep() {
  board_power.POWEER_EPD_OFF();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_US);

  // A button press also forces an immediate refresh - either button, no
  // distinction needed (there's only one view).
  const uint64_t wake_mask = (1ULL << PWR_BUTTON_PIN) | (1ULL << BOOT_BUTTON_PIN);
  esp_sleep_enable_ext1_wakeup_io(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);
  rtc_gpio_pullup_en((gpio_num_t)PWR_BUTTON_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)PWR_BUTTON_PIN);
  rtc_gpio_pullup_en((gpio_num_t)BOOT_BUTTON_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)BOOT_BUTTON_PIN);

  rtc_gpio_hold_en((gpio_num_t)VBAT_PWR_PIN);

  Serial.println("Going to sleep - next check in 15 min, or press either button now.");
  Serial.flush();
  esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(300);

  gpio_hold_dis((gpio_num_t)VBAT_PWR_PIN);
  board_power.VBAT_POWER_ON();

  pinMode(PWR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  const char *wake_reason = (cause == ESP_SLEEP_WAKEUP_TIMER) ? "timer"
                           : (cause == ESP_SLEEP_WAKEUP_EXT1)  ? "button"
                                                                : "cold boot";
  Serial.printf("[wake] %s\n", wake_reason);

  board_power.POWEER_EPD_ON();

  custom_lcd_spi_t epd_cfg = {};
  epd_cfg.cs = EPD_CS_PIN;
  epd_cfg.dc = EPD_DC_PIN;
  epd_cfg.rst = EPD_RST_PIN;
  epd_cfg.busy = EPD_BUSY_PIN;
  epd_cfg.mosi = EPD_MOSI_PIN;
  epd_cfg.scl = EPD_SCK_PIN;
  epd_cfg.spi_host = EPD_SPI_NUM;
  epd_cfg.buffer_len = 5000;

  driver = new epaper_driver_display(EPD_WIDTH, EPD_HEIGHT, epd_cfg);
  driver->EPD_Init();
  driver->EPD_Clear();
  driver->EPD_Display();

  canvas = new GFXcanvas1(EPD_WIDTH, EPD_HEIGHT);

  // Splash first, before any network call - the WiFi connect + API fetches
  // can take 10-20+ seconds combined, and a blank screen for that long
  // looks broken rather than busy.
  render_splash(canvas);
  commit_canvas();

  connect_wifi();

  bool live = false;
  EmberLevel level = EMBER_UNKNOWN;
  double distance_km = 0;
  const char *direction = "";
  const char *size_word = "";
  const char *conf_word = "";
  bool wind_toward = false;
  String message = "";

  if (wifi_ok) {
    double lat, lon;
    bool have_location;
    if (OVERRIDE_LOCATION) {
      lat = OVERRIDE_LAT;
      lon = OVERRIDE_LON;
      have_location = true;
      Serial.printf("[geo] using OVERRIDE_LOCATION (%.3f, %.3f)\n", lat, lon);
    } else {
      have_location = fetch_location(lat, lon);
    }
    if (have_location) {
      double f_dist, f_bearing, f_frp;
      String f_confidence;
      if (fetch_kanari_nearest(lat, lon, f_dist, f_bearing, f_frp, f_confidence)) {
        double fire_to_loc = fmod(f_bearing + 180.0, 360.0);
        double wind_from_deg;
        bool have_wind = fetch_wind(lat, lon, wind_from_deg);
        // If wind's unreachable, we proceed without it (treated as "not
        // confirmed toward you") rather than blocking the whole reading -
        // distance/confidence alone still gives a useful, if less precise,
        // answer.
        if (have_wind) {
          double wind_towards_deg = fmod(wind_from_deg + 180.0, 360.0);
          double diff = fabs(fmod((wind_towards_deg - fire_to_loc) + 540.0, 360.0) - 180.0);
          wind_toward = diff <= WIND_ALIGN_TOLERANCE_DEG;
        }
        bool likely_confirmed = (f_confidence == "probable" || f_confidence == "corrobore");
        Verdict v = classify_danger(f_dist, wind_toward, likely_confirmed);
        level = v.level;
        message = v.message;
        distance_km = f_dist;
        direction = compass_from_deg(f_bearing);
        size_word = size_word_from_frp(f_frp);
        conf_word = conf_word_from_kanari(f_confidence);
        live = true;
      }
    }
  }

  const char *tag;
  if (live) {
    cached_level = level;
    cached_distance = (float)distance_km;
    strncpy(cached_direction, direction, sizeof(cached_direction) - 1);
    strncpy(cached_size_word, size_word, sizeof(cached_size_word) - 1);
    strncpy(cached_conf_word, conf_word, sizeof(cached_conf_word) - 1);
    cached_wind_toward = wind_toward;
    message.toCharArray(cached_message, sizeof(cached_message));
    has_cached = true;
    tag = "(live)";
  } else if (has_cached) {
    level = (EmberLevel)cached_level;
    distance_km = cached_distance;
    direction = cached_direction;
    size_word = cached_size_word;
    conf_word = cached_conf_word;
    wind_toward = cached_wind_toward;
    message = String(cached_message);
    tag = "(cached)";
  } else {
    level = EMBER_UNKNOWN;
    message = "No reading yet - check WiFi and try a button press.";
    tag = "";
  }

  Serial.printf("[ember] (%s) level=%d dist=%.1fkm dir=%s %s\n",
                tag[0] ? tag : "no data", (int)level, distance_km, direction, message.c_str());
  render_flame(canvas, level, (float)distance_km, direction, size_word, conf_word, wind_toward, message, tag);
  commit_canvas();

  go_to_deep_sleep();
}

void loop() {}  // never reached - setup() always ends in deep sleep
