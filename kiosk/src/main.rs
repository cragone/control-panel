// Native kiosk display for control-panel. Replaces the old browser-based
// (cage + Chromium --kiosk) frontend: this talks directly to the same Go
// backend's WebSocket API (ws://localhost:8080/ws), rendered with Slint
// instead of HTML/CSS, so there's no browser/webview in the loop at all.
//
// Zone names come from vecna's device registry when VECNA_DEVICES_URL is
// reachable (a LAN-only, unauthenticated route -- vecna's public hostname
// is BasicAuth-gated, see CONTROL-PANEL.md), so naming stays consistent
// across both home-automation UIs. The hardware firmware currently only
// understands a single zone (topic "lights/desktop"), so today this maps
// to exactly one tile: vecna's first device's name if available, else a
// generic fallback -- not hardcoded to "Desktop".

use std::io::Write;
use std::sync::{Arc, Mutex};

use futures_util::{SinkExt, StreamExt};
use serde::{Deserialize, Serialize};
use slint::{Model, ModelRc, SharedString, VecModel, Weak};

slint::include_modules!();

/// cage doesn't forward the child process's stdout/stderr into systemd's
/// journal for this unit in any way that's been observable while debugging
/// the kiosk startup, so diagnostics go straight to a file instead.
fn log(msg: &str) {
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open("/tmp/kiosk.log")
    {
        let _ = writeln!(f, "{msg}");
        let _ = f.flush();
    }
}

const DEFAULT_ZONE_ID: &str = "desktop";
const DEFAULT_ZONE_NAME: &str = "Light";

#[derive(Debug, Deserialize)]
struct VecnaDevice {
    name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct LightMsg {
    zone: String,
    state: bool,
}

async fn fetch_zone_name() -> String {
    let Ok(url) = std::env::var("VECNA_DEVICES_URL") else {
        return DEFAULT_ZONE_NAME.to_string();
    };
    let Ok(client) = reqwest::Client::builder()
        .timeout(std::time::Duration::from_secs(3))
        .build()
    else {
        return DEFAULT_ZONE_NAME.to_string();
    };
    match client.get(&url).send().await {
        Ok(resp) => match resp.json::<Vec<VecnaDevice>>().await {
            Ok(devices) if !devices.is_empty() => devices[0].name.clone(),
            _ => DEFAULT_ZONE_NAME.to_string(),
        },
        Err(_) => DEFAULT_ZONE_NAME.to_string(),
    }
}

fn ws_url() -> String {
    std::env::var("CONTROL_PANEL_WS_URL").unwrap_or_else(|_| "ws://localhost:8080/ws".to_string())
}

/// Apply `f` to the tile matching `zone_id` in the live window's model,
/// from whichever thread is calling (must be run via invoke_from_event_loop
/// if not already on the Slint event loop thread).
fn update_tile(window: &MainWindow, zone_id: &str, f: impl FnOnce(&mut ZoneTile)) {
    let model = window.get_zones();
    for i in 0..model.row_count() {
        if let Some(mut tile) = model.row_data(i) {
            if tile.zone_id == zone_id {
                f(&mut tile);
                model.set_row_data(i, tile);
                return;
            }
        }
    }
}

fn main() {
    std::panic::set_hook(Box::new(|info| log(&format!("[kiosk] PANIC: {info}"))));

    log("[kiosk] starting, creating window");
    let window = match MainWindow::new() {
        Ok(w) => {
            log("[kiosk] window created ok");
            w
        }
        Err(e) => {
            log(&format!("[kiosk] FATAL: MainWindow::new() failed: {e}"));
            std::process::exit(1);
        }
    };

    let zone_name = {
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .unwrap();
        rt.block_on(fetch_zone_name())
    };
    log(&format!("[kiosk] zone_name = {zone_name:?}"));

    let tiles = VecModel::from(vec![ZoneTile {
        zone_id: SharedString::from(DEFAULT_ZONE_ID),
        name: SharedString::from(zone_name.as_str()),
        on: false,
    }]);
    window.set_zones(ModelRc::new(tiles));
    log("[kiosk] zones set, about to run()");

    let outgoing: Arc<Mutex<Option<tokio::sync::mpsc::UnboundedSender<LightMsg>>>> =
        Arc::new(Mutex::new(None));

    {
        let outgoing = outgoing.clone();
        let window_weak = window.as_weak();
        window.on_toggle(move |zone_id| {
            let Some(window) = window_weak.upgrade() else {
                return;
            };
            let zone_id = zone_id.to_string();
            let mut new_state = false;
            update_tile(&window, &zone_id, |t| {
                t.on = !t.on;
                new_state = t.on;
            });
            if let Some(tx) = outgoing.lock().unwrap().as_ref() {
                let _ = tx.send(LightMsg {
                    zone: zone_id,
                    state: new_state,
                });
            }
        });
    }

    // Background tokio runtime owns the WebSocket connection; only the
    // Weak<MainWindow> handle crosses the thread boundary (Slint models
    // aren't Send) -- every UI mutation happens inside
    // invoke_from_event_loop, which re-fetches the model fresh on the
    // Slint event loop thread each time.
    let window_weak = window.as_weak();
    std::thread::spawn(move || {
        let rt = tokio::runtime::Builder::new_current_thread()
            .enable_all()
            .build()
            .unwrap();
        rt.block_on(async move {
            loop {
                if let Err(e) = run_ws(window_weak.clone(), outgoing.clone()).await {
                    log(&format!("[kiosk] ws error: {e}"));
                }
                let _ = slint::invoke_from_event_loop({
                    let window_weak = window_weak.clone();
                    move || {
                        if let Some(w) = window_weak.upgrade() {
                            w.set_connected(false);
                        }
                    }
                });
                tokio::time::sleep(std::time::Duration::from_secs(3)).await;
            }
        });
    });

    let heartbeat = slint::Timer::default();
    heartbeat.start(
        slint::TimerMode::Repeated,
        std::time::Duration::from_secs(2),
        || log("[kiosk] event loop alive"),
    );

    log("[kiosk] entering window.run()");
    match window.run() {
        Ok(()) => log("[kiosk] window.run() returned Ok"),
        Err(e) => log(&format!("[kiosk] window.run() returned Err: {e}")),
    }
}

async fn run_ws(
    window_weak: Weak<MainWindow>,
    outgoing: Arc<Mutex<Option<tokio::sync::mpsc::UnboundedSender<LightMsg>>>>,
) -> anyhow::Result<()> {
    let (ws_stream, _) = tokio_tungstenite::connect_async(ws_url()).await?;
    let (mut write, mut read) = ws_stream.split();

    let (tx, mut rx) = tokio::sync::mpsc::unbounded_channel::<LightMsg>();
    *outgoing.lock().unwrap() = Some(tx);

    {
        let window_weak = window_weak.clone();
        let _ = slint::invoke_from_event_loop(move || {
            if let Some(w) = window_weak.upgrade() {
                w.set_connected(true);
            }
        });
    }

    loop {
        tokio::select! {
            outgoing_msg = rx.recv() => {
                match outgoing_msg {
                    Some(msg) => {
                        let text = serde_json::to_string(&msg)?;
                        write.send(tokio_tungstenite::tungstenite::Message::Text(text.into())).await?;
                    }
                    None => break,
                }
            }
            incoming = read.next() => {
                match incoming {
                    Some(Ok(tokio_tungstenite::tungstenite::Message::Text(text))) => {
                        if let Ok(msg) = serde_json::from_str::<LightMsg>(&text) {
                            let window_weak = window_weak.clone();
                            let _ = slint::invoke_from_event_loop(move || {
                                if let Some(w) = window_weak.upgrade() {
                                    update_tile(&w, &msg.zone, |t| t.on = msg.state);
                                }
                            });
                        }
                    }
                    Some(Ok(_)) => {}
                    Some(Err(e)) => return Err(e.into()),
                    None => break,
                }
            }
        }
    }

    Ok(())
}
