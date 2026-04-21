# TIGA Companion App — v0.1

Single-file PWA for the TIGA wearable health tracker. Currently runs
with mock data; BLE scaffolding is in place for when firmware v6 is ready.

## How to run

### Simplest — just open in a browser
Double-click `index.html`. Works for UI testing. Bluetooth features
won't work because browsers block Web Bluetooth on `file://` URLs.

### Proper local dev server (needed for PWA features + BLE)
You need any static file server. Pick one you have:

**Python (comes with most systems):**
```
cd tiga_app
python3 -m http.server 8080
```
Then open http://localhost:8080

**Node.js:**
```
cd tiga_app
npx serve
```

**VS Code:**
Install the "Live Server" extension, right-click `index.html` → "Open with Live Server"

## Testing on your phone

### Desktop-to-phone over the local network
1. Find your laptop's local IP (e.g. 192.168.1.42)
2. Start the server: `python3 -m http.server 8080`
3. On your phone, open `http://192.168.1.42:8080`
4. Both devices must be on the same Wi-Fi

Note: Web Bluetooth requires HTTPS for remote access. For local testing
over plain HTTP, `localhost` is exempted but local IPs like 192.168.x.x
are not. Two fixes:
- For UI testing only, ignore BLE — toggle to Demo mode, everything else works
- For BLE testing over network, use `ngrok` or `localtunnel` to get a
  free HTTPS URL that tunnels to your local server

### iPhone BLE testing
Safari does not support Web Bluetooth. Install the free **Bluefy** app
from the App Store — it's a browser that implements Web Bluetooth.
Open your PWA inside Bluefy to test BLE features.

### Install as PWA
- **Android Chrome:** tap menu → "Add to Home screen"
- **iOS Safari:** tap share → "Add to Home Screen"

Once installed it runs full-screen without browser chrome.

## Project structure

```
tiga_app/
├── index.html      — app shell + all UI + all logic (single-file)
├── manifest.json   — PWA manifest for install
├── sw.js           — service worker for offline + install
└── README.md       — you are here
```

## What works now (demo mode)

- Login with role selection (Wearer / Caregiver)
- Dashboard with live-updating mock HR, steps, battery, SpO₂
- History list with fake walk sessions
- Emergency contacts list (add via prompt)
- SOS button (demo — does not actually send anything)
- Settings screen with mode toggle, role switch, sign out
- Persistence via localStorage (role, mode, contacts)
- Installable as PWA

## What's stubbed

- `scanForDevice()` does real Web Bluetooth scan but filters for devices
  named "TIGA" — no firmware advertises this yet. Once v6 firmware is
  flashed to the wearable with a BLE service, this will connect.
- `triggerSOS()` only shows a toast. Real implementation will: play
  audible alert, read last GPS fix from wearable, send push notification
  or SMS to emergency contacts via caregiver app.
- Authentication is non-existent. Role choice is local only.
- No backend — history and contacts live in localStorage on one device.

## Next milestones

- **v0.2**: Define BLE service UUIDs, stub a BLE data contract
- **v0.3**: Caregiver view — connected wearers list, alert queue
- **v0.4**: Real BLE connection to firmware v6 (when wearable firmware
  advertises the TIGA service)
- **v0.5**: Push notifications via Web Push API
- **v0.6**: Optional cloud sync (Firebase or Supabase) — opt-in only
