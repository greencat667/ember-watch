#ifndef EMBER_CONFIG_H
#define EMBER_CONFIG_H

// How far out to look for active fires. Kanari's own event feed is global
// with no location filter, so this is applied client-side after fetching.
#define SEARCH_RADIUS_KM 300.0

// A fire's "wind blowing toward you" bearing must land within this many
// degrees of the true fire->you bearing to count as "toward" rather than
// crosswind/away. 45 degrees = one compass-rose step either side.
#define WIND_ALIGN_TOLERANCE_DEG 45.0

// Distance bands (km) used by classify_danger() in ember_watch.ino - see
// that function for the actual cascade. Kept here so the thresholds are in
// one obvious place rather than scattered through the logic.
#define DIST_BAND_VERY_CLOSE_KM 10.0
#define DIST_BAND_CLOSE_KM      50.0
#define DIST_BAND_REGIONAL_KM   150.0

// No point checking more often than Kanari recomputes (every 2-5 minutes
// per their fair-use notice) or wearing down a battery for a slow-moving
// hazard - 15 minutes is a reasonable middle ground. A button press always
// forces an immediate refresh regardless of this timer.
#define WAKE_INTERVAL_US (15ULL * 60ULL * 1000000ULL)

// IP geolocation reflects your ISP's routing, not your building, and can
// be off by tens or hundreds of km (or fail on some mobile hotspots). To skip
// the ipinfo.io lookup and always use fixed coordinates, set
// OVERRIDE_LOCATION to 1 and replace the placeholder latitude/longitude
// below with your own (decimal degrees; 3 decimal places is plenty for a
// 300 km search radius).
#define OVERRIDE_LOCATION 0
#define OVERRIDE_LAT 0.0000  // PLACEHOLDER - replace with your latitude
#define OVERRIDE_LON 0.0000  // PLACEHOLDER - replace with your longitude

#define GEO_TIMEOUT_MS    8000
#define WEATHER_TIMEOUT_MS 8000
// Kanari's /events response is the whole global feed (500-600KB, no
// location filter available server-side) - buffered with getString() and
// parsed through an ArduinoJson filter; needs a generous timeout on a slow
// connection.
#define KANARI_TIMEOUT_MS 25000

#endif  // !EMBER_CONFIG_H
