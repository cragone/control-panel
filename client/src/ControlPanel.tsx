import { useEffect, useRef, useState } from "react";

// Protocol zone names — must match exactly in the Go server and ESP32 firmware.
// Supported zones: "desktop"
const LIGHTS = [
  { id: "desktop", name: "Desktop", icon: "🖥️" },
] as const;

type LightId = (typeof LIGHTS)[number]["id"];

const WS_URL = "ws://localhost:8080/ws";

export function ControlPanel() {
  const [on, setOn] = useState<Set<LightId>>(new Set());
  const ws = useRef<WebSocket | null>(null);

  useEffect(() => {
    const socket = new WebSocket(WS_URL);
    ws.current = socket;
    return () => socket.close();
  }, []);

  function send(id: LightId, state: boolean) {
    if (ws.current?.readyState === WebSocket.OPEN) {
      ws.current.send(JSON.stringify({ zone: id, state }));
    }
  }

  function toggle(id: LightId) {
    setOn((prev) => {
      const next = new Set(prev);
      const nowOn = !next.has(id);
      nowOn ? next.add(id) : next.delete(id);
      send(id, nowOn);
      return next;
    });
  }

  function allOn() {
    setOn(new Set(LIGHTS.map((l) => l.id)));
    LIGHTS.forEach((l) => send(l.id, true));
  }

  function allOff() {
    setOn(new Set());
    LIGHTS.forEach((l) => send(l.id, false));
  }

  const activeCount = on.size;

  return (
    <div className="cp-page">
      <div className="cp-panel">
        <header className="cp-header">
          <div className="cp-header-left">
            <span className="cp-title">Light Control</span>
            <span className="cp-subtitle">Model CP-01 · Zone Controller</span>
          </div>
          <div className="cp-header-right">
            <div className={`cp-status-dot ${activeCount > 0 ? "active" : ""}`} />
          </div>
        </header>

        <div className="cp-grid">
          {LIGHTS.map((light) => {
            const isOn = on.has(light.id);
            return (
              <div
                key={light.id}
                className={`cp-zone ${isOn ? "on" : ""}`}
                onClick={() => toggle(light.id)}
                role="button"
                aria-pressed={isOn}
                aria-label={`${light.name} light`}
              >
                <div className="cp-bulb">{light.icon}</div>
                <span className="cp-zone-name">{light.name}</span>
                <div className="cp-toggle" />
              </div>
            );
          })}
        </div>

        <footer className="cp-footer">
          <div style={{ display: "flex", gap: 8 }}>
            <button className="cp-master-btn" onClick={allOn}>
              All On
            </button>
            <button className="cp-master-btn" onClick={allOff}>
              All Off
            </button>
          </div>
          <span className="cp-count">
            <span>{activeCount}</span> / {LIGHTS.length} on
          </span>
        </footer>
      </div>
    </div>
  );
}
