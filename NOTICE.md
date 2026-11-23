# Third-party code and data

## Code

- `ember_watch/src/display/`, `ember_watch/src/power/` - hardware driver code adapted from Waveshare's official example code for the ESP32-S3-ePaper-1.54 board (https://www.waveshare.com/wiki/ESP32-S3-ePaper-1.54). Waveshare's examples carry no explicit licence file in the copy this was taken from; they are published for use with their hardware. If you are a Waveshare representative and would like this handled differently, please open an issue.
- [ArduinoJson](https://arduinojson.org/) (Benoit Blanchon), MIT licence. Not bundled: installed by `flash.sh` / `arduino-cli lib install`.
- [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library), BSD licence. Not bundled: installed by `flash.sh` / `arduino-cli lib install`.
- Arduino-ESP32 core (Espressif), LGPL 2.1. Not bundled.

## Data sources

Ember Watch fetches live data at run time. None of it is stored in this repository.

- **Kanari** (https://kanari.io) - wildfire events feed. Kanari's API page asks for attribution ("Source: kanari.io" with a link) on any public reuse, and states "real-time data is recomputed every 2-5 minutes - no point polling more than once a minute. Cache responses." Kanari's detections come from NASA FIRMS, NOAA GOES and EUMETSAT. Kanari describes itself as an information service, not an official alert channel.
- **Open-Meteo** (https://open-meteo.com) - wind direction. Data is CC BY 4.0; the free API is for non-commercial use only. Weather data by Open-Meteo.com.
- **ipinfo.io** (https://ipinfo.io) - approximate location from your public IP, unauthenticated free lookup. Subject to ipinfo.io's own terms and rate limits.

## Artwork

- The icons in `ember_watch/ember_icons.h` (and the preview in `tools/icon_preview.png`) were generated with an AI image model (Stable Diffusion via Draw Things), then converted to 1-bit bitmaps. They are released under the same MIT licence as the code.
