# Ember Watch

**A wildfire proximity display on a 1.54" e-paper screen.** A single flame icon whose size escalates from a calm checkmark to a raging inferno, with the distance and direction to the nearest known wildfire underneath. Once flashed it talks directly to three free, keyless public APIs: no server, no phone app, no account, no computer left on.

> **Not an emergency alert system.** This is a hobby awareness tool built on public satellite and witness-report data, with no official standing. It can be wrong, stale, or miss a fire entirely. Always follow official local guidance (fire service, government alerts) over anything shown here.

## Buy the hardware

Ember Watch runs on the **Waveshare ESP32-S3-ePaper-1.54** board:

**https://www.waveshare.com/esp32-s3-epaper-1.54.htm?sku=32298**

You also need a USB-C cable that carries data (many charge-only cables don't). A small LiPo battery is optional; check the Waveshare page for the right connector. You need a 2.4 GHz WiFi network.

## The quickest way: point your agent at this page

If you use an AI coding agent (Claude Code, Codex, Cursor, or similar), you don't have to do the steps below yourself. Plug the board into your computer and tell the agent:

> Read https://github.com/greencat667/ember-watch and flash Ember Watch onto the ESP32 board plugged into this computer. Ask me for my WiFi name and password and put them in secrets.h; never commit that file.

The agent can clone the repo, install the tools, compile and flash. The only things it can't do are physical: you may need to hold the BOOT button while plugging the board in (see [Download mode](#download-mode)).

## What it shows

| Level | Icon | Meaning |
|---|---|---|
| ALL CLEAR | checkmark in a circle | nearest known fire is beyond 300 km |
| LOW | small, calm campfire | within 300 km, but distant and/or wind not carrying it your way |
| ELEVATED | flame with a couple of sparks | within 50 km, or within 150 km with wind blowing your way (and the report likely/confirmed) |
| HIGH | larger flame, several sparks | within 10 km but unconfirmed, or within 50 km with wind towards you and a likely/confirmed report |
| EXTREME | huge raging inferno | within 10 km **and** likely/confirmed by Kanari |
| NO DATA | flame with a `?` | cold boot, no WiFi, nothing cached yet |

Below the icon: distance and compass direction to the nearest fire *anywhere in Kanari's feed* (not limited to the 300 km radius, so even "all clear" shows how far the nearest one is), its rough size, Kanari's own confidence word, whether the wind is blowing towards you, and a short verdict. Any button press forces an immediate refresh; otherwise it wakes every 15 minutes.

A splash screen (a lookout tower and "checking for wildfires...") shows at wake, before any network call, because WiFi plus three fetches can take 10-20+ seconds and a blank screen looks broken.

The icons are AI-generated art (see [Regenerating the icons](#regenerating-the-icons)).

## How it gets its data

1. **Where am I?** `GET https://ipinfo.io/json` (free, no key, HTTPS). This approximates location from the public IP of the WiFi network the device is on; the board has no GPS. Move it to another network and the location follows. IP geolocation reflects your ISP's routing rather than your building and can be tens or hundreds of km off, so `ember_config.h` has `OVERRIDE_LOCATION` / `OVERRIDE_LAT` / `OVERRIDE_LON` to use fixed coordinates instead (off by default; the coordinates in the file are placeholders). An earlier version used a plain-HTTP geolocation service, which a phone hotspot refused outright while HTTPS worked, hence HTTPS.
2. **What's on fire?** `GET https://kanari.io/api/events?hours=6` - [Kanari](https://kanari.io)'s global wildfire feed (satellite detections cross-checked against AI-verified witness reports), free, no key. There is no location filter, so the device downloads the whole feed (~550 KB, capped at 2000 events) and finds the nearest event itself. It keeps only `centroid`, `maxFrp` and `confidence` from each event.
3. **Which way's the wind?** `GET https://api.open-meteo.com/v1/forecast` - current wind direction at your location from [Open-Meteo](https://open-meteo.com).

**The chunked-encoding gotcha.** Kanari serves the events feed with `Transfer-Encoding: chunked` and no `Content-Length`. Parsing `http.getStream()` directly lets chunk-size framing bytes confuse the JSON parser: a hex chunk-size line can parse as a stray top-level JSON number and "succeed" with an empty document instead of erroring. The code uses `HTTPClient::getString()`, which de-chunks properly, at the cost of buffering ~550 KB in PSRAM. If you reuse this pattern against a chunked endpoint, do the same.

**TLS trade-off.** `WiFiClientSecure::setInsecure()` is used for the HTTPS calls: no CA bundle is carried on the device, so the server certificate is not verified. That is acceptable for read-only public feeds with no login and nothing sensitive in the response, but it means a man-in-the-middle could feed the device false data. Another reason to treat the display as awareness only.

## The decision

`classify_danger()` in `ember_watch/ember_watch.ino` is a small deterministic rule engine, not an LLM, combining three signals, most urgent first:

- **Distance** to the nearest fire (haversine, computed on-device). Beyond 300 km it's ALL CLEAR, with the distance still shown.
- **Wind alignment**: bearing from the fire to you, compared with Open-Meteo's wind direction (converted from "blowing from" to "blowing towards"). Within 45 degrees either side counts as "towards you".
- **Confidence**: Kanari's own `possible` / `probable` / `corrobore` rating, passed through unchanged. "Likely/confirmed" means probable or corrobore.

Thresholds live in `ember_watch/ember_config.h`.

**Why fire size isn't in the cascade.** Size (from Kanari's `maxFrp`, fire radiative power in MW) is shown for context only. A big fire far away with the wind blowing elsewhere isn't urgent; a small but very close, wind-aligned, confirmed one is. Resist making a big-but-distant fire look scarier than a small-but-close one just because MW is a bigger number. The MW-to-words mapping (`size_word_from_frp()`) is a rough approximation, not an official scale: Kanari doesn't publish one, and FRP measures fire intensity, not burned area.

## Offline fallback

If WiFi, geolocation, Kanari or the parse fail, the device shows the last successful reading (kept in RTC memory, which survives deep sleep) tagged `(cached)`. On a cold start with nothing cached it shows the NO DATA icon rather than guessing. If only the wind lookup fails, the reading still proceeds, treating wind as "not towards you", since distance and confidence are still useful. The cache does **not** survive a reflash or power loss, so the first boot after an upload is always cold.

## Install it yourself

### 1. Install the tools

Install [arduino-cli](https://arduino.github.io/arduino-cli/latest/installation/) (on macOS: `brew install arduino-cli`).

On a Mac with Apple silicon you may also need Rosetta, because one of Arduino's helper tools is Intel-only. If compiling fails with "bad CPU type in executable", run:

```bash
softwareupdate --install-rosetta --agree-to-license
```

### 2. Get the code and add your WiFi

```bash
git clone https://github.com/greencat667/ember-watch.git
cd ember-watch
cp ember_watch/secrets.h.example ember_watch/secrets.h
```

Edit `ember_watch/secrets.h` with your WiFi name and password. It is git-ignored; never commit it. (`./flash.sh` creates it from the example if it's missing, then stops so you can edit it.)

### 3. Flash

Plug the board in, then run:

```bash
./flash.sh
```

The script installs the ESP32 core, Adafruit GFX and ArduinoJson (v7) if needed, lists the boards it can see, asks for the port, then compiles and uploads. You can pass the port directly: `./flash.sh /dev/cu.usbmodem1101`.

The manual equivalent:

```bash
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "Adafruit GFX Library" "ArduinoJson"
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB" ember_watch
arduino-cli upload  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashMode=qio,FlashSize=8M,PartitionScheme=default_8MB" -p <your-port> ember_watch
```

Ports look like `/dev/cu.usbmodem...` on macOS, `/dev/ttyACM0` on Linux and `COM5` on Windows. On Linux you may need to add yourself to the `dialout` group.

Serial output (115200 baud) logs each fetch, which helps when debugging WiFi or API problems.

### Download mode

Ember Watch deep-sleeps between checks, and **deep sleep switches off the USB port**. If `arduino-cli` can't find a port, put the board into download mode:

1. Unplug the USB cable.
2. Hold the **BOOT** button.
3. Plug the cable in while still holding BOOT.
4. Wait about 3 seconds, then release BOOT.
5. Run the flash step straight away.

If the port appears and then vanishes, run the upload again. If uploads keep losing the race with the board going back to sleep, keep holding the button (or retry the whole upload in a loop) while it runs.

## Troubleshooting

| Symptom | Try |
|---|---|
| No serial port appears | Use a data-capable cable, plug straight into the computer, use download mode. |
| "bad CPU type in executable" (macOS) | Install Rosetta (see above). |
| Port vanishes during upload | Run the upload again. |
| Screen looks frozen | Wait 5 seconds after a press: e-paper refreshes slowly. |
| Always shows NO DATA | Check `secrets.h`, that the network is 2.4 GHz, and the serial log. |
| WiFi sometimes fails to connect | Known issue with some phone hotspots (see below); press a button to retry. Enable "Maximize Compatibility" on iPhone hotspots. |
| Wrong location | Set `OVERRIDE_LOCATION` and your coordinates in `ember_config.h`. |

## Check the logic without hardware

`tools/check_wildfire_risk.py` is a standalone script (not part of the firmware). It mirrors the same haversine, bearing, wind-alignment and classification logic in Python against the live feeds, so you can sanity-check what the device should show:

```bash
python3 tools/check_wildfire_risk.py
```

## Regenerating the icons

The icons are **AI-generated** (Stable Diffusion via the Draw Things app), then autocropped, thresholded to 1-bit and baked into `ember_watch/ember_icons.h`. You don't need to regenerate them. If you want to, `tools/gen_icons.py` talks to any Automatic1111-compatible image API (default `http://127.0.0.1:7860`, Draw Things' default; override with `EMBER_ICON_API`) and needs Pillow:

```bash
pip install pillow
python3 tools/gen_icons.py
```

It rewrites `ember_icons.h` and `tools/icon_preview.png` (a decoded contact sheet; check it before reflashing). Output varies between runs, so results won't match exactly. Two sizes are used: 72 px for the six severity icons and 144 px for the splash.

## What has and hasn't been verified

Verified on a real board:

- Compiles and flashes on the Waveshare ESP32-S3-ePaper-1.54 (about 33% flash, 14% RAM).
- A live end-to-end reading against the real Kanari, Open-Meteo and geolocation APIs: nearest fire found with correct distance, bearing, size and confidence, the right ALL CLEAR classification for a fire beyond 300 km, tagged `(live)`, and cross-checked against an independent Python query of the same feed (distances agreed to within about 0.1 km). That cross-check is what caught the chunked-encoding bug.
- Icon and text layout checked against photos of the real screen.

**Not verified:**

- WiFi association against a phone Personal Hotspot was flaky (roughly one in three to one in two attempts stuck at `WL_DISCONNECTED`, even with correct credentials). The connect timeout was widened from 8 s to 20 s, which helped but is not a full fix. Cause unknown.
- No long unattended run: not tested across many 15-minute wake cycles, in multiple locations, or on battery power. Battery life is unmeasured.
- Only the "all clear" case has been seen live. The higher severity levels follow the rules above but were not triggered by a real nearby fire.
- A bottom-button "detail view" (nearest three fires, wind speed) was tried and dropped because it looked messy on screen.

## Data sources and etiquette

- **Kanari** asks for attribution on public reuse: *Source: [kanari.io](https://kanari.io)*. Its feed is recomputed every 2-5 minutes and it asks clients to cache and not poll more than once a minute; this device polls once every 15 minutes (or on a button press) and downloads the full global feed each time. Please don't shorten `WAKE_INTERVAL_US` drastically. If you run many devices, talk to Kanari first. Kanari states that it is an information service, not an official alert channel.
- **Open-Meteo**: [Weather data by Open-Meteo.com](https://open-meteo.com/), CC BY 4.0. The free API is for non-commercial use only; commercial use needs a paid plan.
- **ipinfo.io**: unauthenticated free lookups are rate limited and subject to their terms.

See `NOTICE.md` for third-party code and data.

## Licence

MIT for this project's code and icons (see `LICENSE`). Third-party code and data are listed in `NOTICE.md`.
