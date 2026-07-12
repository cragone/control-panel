package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"strings"
	"sync"
	"time"

	mqtt "github.com/eclipse/paho.mqtt.golang"
	"github.com/gorilla/websocket"
)

const (
	defaultBroker = "tcp://localhost:1883"
	topicPrefix   = "lights/"
)

var (
	upgrader = websocket.Upgrader{
		ReadBufferSize:  1024,
		WriteBufferSize: 1024,
		CheckOrigin:     func(r *http.Request) bool { return true },
	}

	mqttClient mqtt.Client

	clientsMu sync.Mutex
	clients   = map[*websocket.Conn]bool{}

	stateMu sync.Mutex
	state   = map[string]bool{}
)

func registerClient(conn *websocket.Conn) {
	clientsMu.Lock()
	clients[conn] = true
	clientsMu.Unlock()
}

func unregisterClient(conn *websocket.Conn) {
	clientsMu.Lock()
	delete(clients, conn)
	clientsMu.Unlock()
}

func sendSnapshot(conn *websocket.Conn) {
	stateMu.Lock()
	snapshot := make(map[string]bool, len(state))
	for zone, on := range state {
		snapshot[zone] = on
	}
	stateMu.Unlock()

	for zone, on := range snapshot {
		_ = conn.WriteJSON(LightMsg{Zone: zone, State: on})
	}
}

func broadcast(msg LightMsg) {
	clientsMu.Lock()
	defer clientsMu.Unlock()
	for conn := range clients {
		if err := conn.WriteJSON(msg); err != nil {
			log.Printf("broadcast write error: %v", err)
		}
	}
}

type LightMsg struct {
	Zone  string `json:"zone"`
	State bool   `json:"state"`
}

func brokerURL() string {
	b := os.Getenv("MQTT_BROKER")
	if b == "" {
		return defaultBroker
	}
	if !strings.Contains(b, "://") {
		return "tcp://" + b + ":1883"
	}
	return b
}

func connectMQTT() mqtt.Client {
	opts := mqtt.NewClientOptions().
		AddBroker(brokerURL()).
		SetClientID("control-panel").
		SetAutoReconnect(true).
		SetOnConnectHandler(func(_ mqtt.Client) {
			log.Println("MQTT connected")
		}).
		SetConnectionLostHandler(func(_ mqtt.Client, err error) {
			log.Printf("MQTT connection lost: %v", err)
		})

	c := mqtt.NewClient(opts)
	if tok := c.Connect(); tok.Wait() && tok.Error() != nil {
		log.Fatalf("MQTT connect: %v", tok.Error())
	}
	return c
}

func publishToMQTT(msg []byte) (LightMsg, error) {
	var payload LightMsg
	if err := json.Unmarshal(msg, &payload); err != nil || payload.Zone == "" {
		log.Printf("invalid message, skipping: %v", err)
		return payload, fmt.Errorf("invalid message: %w", err)
	}

	topic := topicPrefix + payload.Zone
	statePayload := "0"
	if payload.State {
		statePayload = "1"
	}

	tok := mqttClient.Publish(topic, 1, true, statePayload)
	tok.Wait()
	if err := tok.Error(); err != nil {
		log.Printf("MQTT publish error: %v", err)
		return payload, err
	}

	stateMu.Lock()
	state[payload.Zone] = payload.State
	stateMu.Unlock()

	return payload, nil
}

// discardWriter absorbs the upgrader's own error response so we can write our own.
type discardWriter struct {
	http.ResponseWriter
}

func (dw *discardWriter) WriteHeader(int)             {}
func (dw *discardWriter) Write(b []byte) (int, error) { return len(b), nil }
func (dw *discardWriter) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	hj, ok := dw.ResponseWriter.(http.Hijacker)
	if !ok {
		return nil, nil, fmt.Errorf("underlying ResponseWriter does not implement http.Hijacker")
	}
	return hj.Hijack()
}

func HandleConnections(w http.ResponseWriter, r *http.Request) {
	conn, err := upgrader.Upgrade(&discardWriter{w}, r, nil)
	if err != nil {
		log.Printf("WebSocket upgrade failed: %v", err)
		WriteJSONResponse(w, http.StatusBadRequest, JSONMap{"error": err.Error()})
		return
	}
	defer conn.Close()
	log.Println("browser connected")

	registerClient(conn)
	defer unregisterClient(conn)
	sendSnapshot(conn)

	for {
		_, message, err := conn.ReadMessage()
		if err != nil {
			log.Printf("browser disconnected: %v", err)
			break
		}

		var payload map[string]any
		if err := json.Unmarshal(message, &payload); err != nil {
			log.Printf("invalid JSON from browser: %v", err)
			_ = conn.WriteJSON(JSONMap{"ok": false, "error": "invalid JSON"})
			continue
		}

		log.Printf("browser -> MQTT: %s", message)
		lightMsg, err := publishToMQTT(message)
		if err != nil {
			_ = conn.WriteJSON(JSONMap{"ok": false, "error": err.Error()})
			continue
		}

		broadcast(lightMsg)
	}
}

func main() {
	log.Println("booting application")

	mqttClient = connectMQTT()

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", HandleConnections)
	mux.Handle("/", http.FileServer(http.Dir("dist")))

	log.Fatal(http.ListenAndServe(":80", logRequests(mux)))
}

type statusWriter struct {
	http.ResponseWriter
	code int
}

func (sw *statusWriter) WriteHeader(code int) {
	sw.code = code
	sw.ResponseWriter.WriteHeader(code)
}

func (sw *statusWriter) Hijack() (net.Conn, *bufio.ReadWriter, error) {
	hj, ok := sw.ResponseWriter.(http.Hijacker)
	if !ok {
		return nil, nil, fmt.Errorf("underlying ResponseWriter does not implement http.Hijacker")
	}
	return hj.Hijack()
}

func logRequests(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		sw := &statusWriter{ResponseWriter: w, code: http.StatusOK}
		start := time.Now()
		next.ServeHTTP(sw, r)
		log.Printf("%s %s %s %d %s", r.RemoteAddr, r.Method, r.URL.Path, sw.code, time.Since(start))
	})
}

type JSONMap map[string]any

func WriteJSONResponse(w http.ResponseWriter, code int, data any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	if err := json.NewEncoder(w).Encode(data); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}
