# WebSocket Integration Plan

## Architecture

```
Browser (React client)
    ↕  WebSocket  ws://server:80/ws
Go Server
    ↕  WebSocket  ws://esp32-ip/ws
ESP32 (WebSocket server)
    ↓
GPIO 26 (relay/light)
```

The ESP32 runs its own WebSocket **server**. The Go server acts as a **client** to the ESP32 and a **server** to the browser. The browser only ever talks to Go.

---

## Message Format

Use a single JSON shape everywhere:

```
{ "zone": "living_room", "state": true }
```

`zone` matches the zone name from ControlPanel.tsx. `state` is `true` for on, `false` for off.

---

## Step 1 — Define the protocol (do this first)

Write down the full list of zone names you'll support and confirm they match what the UI already uses. Keep a copy somewhere (a comment, a constant file, a note) — both the server and ESP32 must agree on the same names.

---

## Step 2 — ESP32: add WiFi + WebSocket server

1. Open `hardware/platformio.ini` and add the `links2004/arduinoWebsockets` library (or `me-no-dev/ESP Async WebServer` + `me-no-dev/ESPAsyncTCP`).
2. In `main.cpp`:
   - Include `WiFi.h` and the WebSocket server header.
   - In `setup()`: connect to your local WiFi (SSID + password), print the assigned IP address over Serial so you know what address to point the Go server at.
   - Start a WebSocket server on port 81 (or 80 — pick one not used by anything else on the ESP32).
   - Register an `onMessage` callback that receives the JSON string, parses `zone` and `state`, then calls `digitalWrite` on the appropriate pin.
3. In `loop()`: call the WebSocket server's poll/loop method every iteration.
4. Flash and verify via Serial monitor that the ESP32 prints its IP and "WS server started".

---

## Step 3 — Go server: connect outward to the ESP32

The server already accepts connections from the browser on `/ws`. Now it also needs to maintain an outbound connection **to** the ESP32.

1. On startup, dial the ESP32's WebSocket endpoint (`ws://<esp32-ip>:81/ws`) using the gorilla client API.
2. Store that connection in a package-level variable (or a struct).
3. Add a helper function `sendToESP32(msg []byte)` that writes to that connection.
4. Handle reconnection: if the ESP32 reboots, the dial will fail or the connection will drop. A simple retry loop with a short sleep is enough for a home project.

---

## Step 4 — Go server: forward browser messages to ESP32

Right now `HandleConnections` echoes messages back. Change it to:

1. Read the message from the browser client.
2. Validate it is valid JSON with the expected fields (optional but helpful).
3. Call `sendToESP32(message)` instead of echoing.
4. Optionally echo back a `{"ok": true}` acknowledgement to the browser.

---

## Step 5 — React client: open a WebSocket connection

In `ControlPanel.tsx` (or a dedicated `useWebSocket` hook):

1. On component mount, open `new WebSocket("ws://<server-ip>:80/ws")`.
2. Store the socket in a `useRef` so it persists across renders.
3. On `onopen`, log "connected".
4. On `onmessage`, handle any acknowledgements or state sync messages the server sends back.
5. On `onclose` / `onerror`, attempt to reconnect after a short delay.

---

## Step 6 — Wire toggle actions to the WebSocket

In `ControlPanel.tsx`, every zone toggle currently only updates local React state. After updating local state, also call:

```
socket.current.send(JSON.stringify({ zone: "living_room", state: newState }))
```

Do this for every zone button. The zone string must match exactly what the ESP32 expects (from Step 1).

---

## Step 7 — Map zones to GPIO pins on the ESP32

In the ESP32 firmware, create a lookup from zone name → pin number. For now you only have GPIO 26 wired, so only `living_room` (or whatever you name it) does anything real. The others can be no-ops until you wire more pins.

---

## Step 8 — End-to-end test

1. Flash the ESP32, note its IP in Serial monitor.
2. Update the Go server config with that IP and restart it.
3. Open the React app in the browser.
4. Click a zone toggle — watch the Serial monitor on the ESP32 confirm a message arrived.
5. Verify GPIO 26 changes state (multimeter, LED, or relay click).

---

## Order of work

```
1. Agree on message format (Step 1)
2. ESP32 WiFi + WS server (Step 2)
3. Go outbound connection to ESP32 (Step 3)
4. Go forward logic (Step 4)
5. React WS connection (Step 5)
6. React toggle sends messages (Step 6)
7. ESP32 pin map (Step 7)
8. Full test (Step 8)
```

Steps 2–4 can be done and tested independently of Steps 5–6 by sending raw WebSocket frames with a tool like `wscat` or Postman.

---

## Notes

- Keep the ESP32 IP static: assign a fixed IP in your router's DHCP table (by MAC address) so the Go server config never needs to change.
- The Go server port is currently 80 — if you run it on a Mac/Linux machine you need `sudo` or change to a port above 1024. On Windows 80 is usually fine.
- Do not store the WiFi password in the ESP32 source if you plan to push this repo publicly — use a `secrets.h` file that is `.gitignore`d.
