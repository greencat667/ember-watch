#!/usr/bin/env python3
"""Desktop check of Ember Watch's decision logic against real live data.
Not part of the firmware (the ESP32 does all of this itself): it mirrors the
same haversine/bearing/wind/classification logic so you can sanity-check the
device's answer. Thresholds here are hardcoded copies of ember_config.h."""
import json, math, urllib.request, sys

def fetch_json(url, timeout=20):
    req = urllib.request.Request(url, headers={"Accept": "application/json", "User-Agent": "ember-watch-check/1"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode("utf-8"))

def haversine_km(lat1, lon1, lat2, lon2):
    R = 6371.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi/2)**2 + math.cos(p1)*math.cos(p2)*math.sin(dlambda/2)**2
    return 2*R*math.asin(math.sqrt(a))

def bearing_deg(lat1, lon1, lat2, lon2):
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dl = math.radians(lon2 - lon1)
    x = math.sin(dl)*math.cos(p2)
    y = math.cos(p1)*math.sin(p2) - math.sin(p1)*math.cos(p2)*math.cos(dl)
    return (math.degrees(math.atan2(x, y)) + 360) % 360

COMPASS = ["N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW"]
def compass(deg):
    return COMPASS[round(deg / 22.5) % 16]

def size_word(frp):
    if frp < 20: return "small, localized"
    if frp < 100: return "moderate-sized"
    if frp < 500: return "large"
    if frp < 1500: return "very large / major"
    return "extreme, megafire-scale"

def conf_word(c):
    return {"possible": "unconfirmed", "probable": "likely", "corrobore": "confirmed"}.get(c, c)

RADIUS_KM = 300

def classify_danger(distance_km, wind_toward, confidence):
    likely = confidence in ("corrobore", "probable")
    if distance_km <= 10 and likely:
        return "EXTREME", "Very close and confirmed - follow local emergency guidance now"
    if distance_km <= 10:
        return "HIGH", "Very close but unconfirmed - stay alert"
    if distance_km <= 50 and wind_toward and likely:
        return "HIGH", "Close, wind carrying it your way - stay alert"
    if distance_km <= 50:
        return "ELEVATED", "Fire in your area - monitor conditions"
    if distance_km <= 150 and wind_toward and likely:
        return "ELEVATED", "In your wider region, wind blowing your way"
    if distance_km <= 150:
        return "LOW", "Detected in your wider region, no immediate concern"
    return "LOW", "Distant, within monitored radius - for awareness only"

def main():
    # 1. auto-detect current location via IP geolocation
    try:
        geo = fetch_json("https://ipinfo.io/json", timeout=10)
        lat, lon = (float(v) for v in geo["loc"].split(","))
        place = f"{geo.get('city')}, {geo.get('country')}"
    except Exception as e:
        print("geolocation failed:", e); sys.exit(1)
    print(f"Detected location: {place} ({lat:.3f}, {lon:.3f})")

    # 2. fetch Kanari fire events, filter to radius
    events = fetch_json("https://kanari.io/api/events?hours=6", timeout=25)["events"]
    print(f"Fetched {len(events)} global events, filtering to {RADIUS_KM} km radius...")
    nearby = []
    for e in events:
        elon, elat = e["centroid"]
        d = haversine_km(lat, lon, elat, elon)
        if d <= RADIUS_KM:
            nearby.append((d, e))
    nearby.sort(key=lambda t: t[0])
    print(f"{len(nearby)} fire(s) within {RADIUS_KM} km")

    if not nearby:
        print("ALL CLEAR - no active fires detected within", RADIUS_KM, "km")
        return

    d, fire = nearby[0]
    elon, elat = fire["centroid"]
    brg = bearing_deg(lat, lon, elat, elon)          # location -> fire
    fire_to_loc_brg = (brg + 180) % 360               # fire -> location (direction smoke travels to reach us)

    # 3. wind at our location
    wx = fetch_json(f"https://api.open-meteo.com/v1/forecast?latitude={lat}&longitude={lon}&current=wind_speed_10m,wind_direction_10m&wind_speed_unit=kmh", timeout=15)
    wind_from = wx["current"]["wind_direction_10m"]
    wind_speed = wx["current"]["wind_speed_10m"]
    wind_towards = (wind_from + 180) % 360
    diff = abs((wind_towards - fire_to_loc_brg + 180) % 360 - 180)
    wind_toward_us = diff <= 45

    level, msg = classify_danger(d, wind_toward_us, fire["confidence"])

    print()
    print(f"Nearest fire: {d:.1f} km {compass(brg)} of you, near {fire.get('social',{}).get('place','unknown')}")
    print(f"  size: {size_word(fire['maxFrp'])} (FRP {fire['maxFrp']:.0f} MW)")
    print(f"  confidence: {conf_word(fire['confidence'])}")
    print(f"  wind: {wind_speed:.0f} km/h from {compass(wind_from)}, blowing toward {compass(wind_towards)} -> {'TOWARD you' if wind_toward_us else 'not toward you'} (diff {diff:.0f} deg)")
    print(f"  DANGER LEVEL: {level} - {msg}")

if __name__ == "__main__":
    main()
