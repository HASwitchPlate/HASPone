# HASPone v1.09 Release Notes

## Home Assistant Update Integration

HASPone now registers as an updatable device in Home Assistant. When a firmware update is available, it appears in the HA **Settings > Updates** dashboard alongside your other device updates.

- **ESP8266 firmware** and **Nextion LCD firmware** are tracked as separate update entities
- Click **Install** directly from Home Assistant to trigger the OTA update
- Release notes link is included in each update card, configurable per-release via `version.json`
- Update availability is checked automatically every 12 hours

## Bug Fixes

### Fixed Nextion ACK timeout logic

The ACK wait loops in `nextionSetAttr()` and `nextionGetAttr()` used `||` (OR) with an inverted timeout comparison, meaning they would exit immediately instead of waiting for the ACK response. Changed to `&&` (AND) with corrected comparison direction so the loop properly waits for either an ACK or a timeout.

### Fixed beep on/off state inversion

The beep feedback had on and off states swapped: `analogWrite(beepPin, 254)` was called during the "off" phase and `analogWrite(beepPin, 0)` during the "on" phase. Corrected so the beep actually sounds during the on interval.

### Fixed WiFi password display in web UI

The web configuration page checked `mqttUser` instead of `mqttPassword` when deciding whether to show the password placeholder. The MQTT password field would appear empty even when a password was saved.

### Fixed page restore after LCD reboot

`nextionReset()` and `espWifiConnect()` compared `nextionActivePage` with a truthy check, which treated page 0 as "no page set." Changed to `>= 0` so page 0 is correctly restored.

### Fixed debugPrint() brace scoping

`debugPrint()` had a misplaced opening brace that put `Serial.print(debugText)` outside the `if (debugSerialEnabled)` block, causing serial output even when debug was disabled.

## Improvements

### mDNS actually starts now

Added the missing `MDNS.begin(haspNode)` call. Without it, the mDNS service registration was configured but the responder itself was never started, so the device was not discoverable on the local network.

### mDNS now advertises MAC address and MQTT server

Added `mac` and `mqtt_server` TXT records to the mDNS service advertisement, making device identification easier on the local network.

### WiFi reconnection hardened

- `WiFi.hostname()`, `WiFi.setAutoReconnect(true)`, and `WiFi.setSleepMode(WIFI_NONE_SLEEP)` are now set on both initial connection and reconnection
- `espWifiReconnect()` uses saved credentials from WiFiManager when no hardcoded SSID is configured, instead of passing empty strings to `WiFi.begin()`
- WiFi persistence disabled during reconnection to avoid unnecessary flash writes
- All WiFi settings are reapplied after reconnection

### OTA update reliability improved

- MQTT client, TLS buffers, telnet, and web server are disconnected/stopped before starting ESP OTA to free memory
- HTTPS OTA buffer increased from 512 to 4096 bytes for more reliable downloads
- Removed manual URL parsing — `ESPhttpUpdate` handles it with `HTTPC_FORCE_FOLLOW_REDIRECTS`

### Update check rewritten for Cloudflare compatibility

`updateCheck()` was rewritten to use raw HTTP/1.0 requests with explicit content-length parsing instead of `HTTPClient`. This resolves failures when `version.json` is served behind Cloudflare's edge network.

### Firmware download URLs switched to HTTP

Default firmware URLs changed from `https://` to `http://` to reduce memory pressure during OTA downloads on the ESP8266. Previously HTTPS was used through a cloud-hosted VM which would proxy from GitHub, that now runs through CloudFlare.  While this does change from HTTPS to HTTP, the security posture doesn't change as the ESP8266 does not have the capacity to pull in and validate the full cert chain so it never did actually check any part of that in previous releases.  The update check itself still uses HTTPS.

### Debug serial output optimized

`SoftwareSerial debugSerial` is now a global instance instead of being constructed and destroyed on every `debugPrintln()` / `debugPrint()` / `debugPrintCrash()` call. Reduces heap churn during debug output.

### OTA progress display deduplicated

`nextionUpdateProgress()` now only sends display updates when the percentage actually changes, avoiding redundant serial commands to the Nextion during firmware uploads.

### PlatformIO build configuration updated

Added `monitor_speed = 115200` and `upload_speed = 921600` to `platformio.ini` for faster development iteration.

## ESPHome Example Updated

The ESPHome example configuration (`esphome/haspone.yaml`) has been updated with:

- Serial logging disabled (`baud_rate: 0`) since the UART pins conflict with the Nextion — logs are available via the web UI and ESPHome dashboard instead
- Updated `ota:` to current ESPHome syntax (`platform: esphome`)
- Detailed header comments explaining the hardware UART constraints and why SoftwareSerial is required on this PCB
- Version bumped to 0.0.2

## version.json Changes

All firmware entries now include a `release_url` field pointing to the GitHub releases page. The device reads this on each update check and passes it to Home Assistant so the update card links to the correct release notes.

## Files Changed

| File | Description |
|------|-------------|
| `Arduino_Sketch/HASwitchPlate/HASwitchPlate.cpp` | All firmware changes above |
| `Arduino_Sketch/HASwitchPlate.ino.d1_mini.bin` | Compiled firmware binary |
| `Arduino_Sketch/debug/HASwitchPlate.ino.d1_mini.elf` | Debug symbols |
| `Arduino_Sketch/platformio.ini` | Build config updates |
| `esphome/haspone.yaml` | ESPHome example improvements |
| `update/version.json` | Updated paths, added release_url |
