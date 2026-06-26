package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

const defaultESP32Addr = "192.168.1.250:81"

var (
	upgrader = websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
		CheckOrigin: func(r *http.Request) bool { return true },
	}

	esp32Mu   sync.Mutex
	esp32Conn *websocket.Conn
)

func esp32URL() string {
	addr := os.Getenv("ESP32_ADDR")
	if addr == "" {
		addr = defaultESP32Addr
	}
	return "ws://" + addr + "/"
}

// connectESP32 runs forever in a goroutine, maintaining the outbound
// connection to the ESP32 and reconnecting on drop.
func connectESP32() {
	url := esp32URL()
	for {
		log.Printf("dialing ESP32 at %s", url)
		c, _, err := websocket.DefaultDialer.Dial(url, nil)
		if err != nil {
			log.Printf("ESP32 dial failed: %v — retrying in 5s", err)
			time.Sleep(5 * time.Second)
			continue
		}
		log.Println("connected to ESP32")

		esp32Mu.Lock()
		esp32Conn = c
		esp32Mu.Unlock()

		// Drain any incoming frames to keep the connection alive.
		for {
			if _, _, err := c.ReadMessage(); err != nil {
				log.Printf("ESP32 connection lost: %v — reconnecting", err)
				break
			}
		}

		esp32Mu.Lock()
		esp32Conn = nil
		esp32Mu.Unlock()
		c.Close()
		time.Sleep(5 * time.Second)
	}
}

func sendToESP32(msg []byte) {
	esp32Mu.Lock()
	defer esp32Mu.Unlock()
	if esp32Conn == nil {
		log.Println("ESP32 not connected — message dropped")
		return
	}
	if err := esp32Conn.WriteMessage(websocket.TextMessage, msg); err != nil {
		log.Printf("ESP32 write error: %v", err)
	}
}

func HandleConnections(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		WriteJSONResponse(w, http.StatusBadRequest, JSONMap{"error": err.Error()})
		return
	}
	defer conn.Close()
	log.Println("browser connected")

	for {
		_, message, err := conn.ReadMessage()
		if err != nil {
			log.Printf("browser disconnected: %v", err)
			break
		}

		// Validate the message is a JSON object before forwarding.
		var payload map[string]any
		if err := json.Unmarshal(message, &payload); err != nil {
			log.Printf("invalid JSON from browser: %v", err)
			_ = conn.WriteJSON(JSONMap{"ok": false, "error": "invalid JSON"})
			continue
		}

		log.Printf("browser -> ESP32: %s", message)
		sendToESP32(message)

		_ = conn.WriteJSON(JSONMap{"ok": true})
	}
}

func main() {
	log.Println("booting application")

	go connectESP32()

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", HandleConnections)
	mux.Handle("/", http.FileServer(http.Dir("../client/dist")))

	log.Fatal(http.ListenAndServe(":8080", mux))
}

type JSONMap map[string]any

func WriteJSONResponse(w http.ResponseWriter, code int, data any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	if err := json.NewEncoder(w).Encode(data); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}
