package main

import (
	"encoding/json"
	"log"
	"net/http"
	"os"
	"strings"
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
		CheckOrigin: func(r *http.Request) bool { return true },
	}

	mqttClient mqtt.Client
)

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

func publishToMQTT(msg []byte) {
	var payload LightMsg
	if err := json.Unmarshal(msg, &payload); err != nil || payload.Zone == "" {
		log.Printf("invalid message, skipping: %v", err)
		return
	}

	topic := topicPrefix + payload.Zone
	state := "0"
	if payload.State {
		state = "1"
	}

	tok := mqttClient.Publish(topic, 1, false, state)
	tok.Wait()
	if err := tok.Error(); err != nil {
		log.Printf("MQTT publish error: %v", err)
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

		var payload map[string]any
		if err := json.Unmarshal(message, &payload); err != nil {
			log.Printf("invalid JSON from browser: %v", err)
			_ = conn.WriteJSON(JSONMap{"ok": false, "error": "invalid JSON"})
			continue
		}

		log.Printf("browser -> MQTT: %s", message)
		publishToMQTT(message)

		_ = conn.WriteJSON(JSONMap{"ok": true})
	}
}

func main() {
	log.Println("booting application")

	mqttClient = connectMQTT()

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", HandleConnections)
	mux.Handle("/", http.FileServer(http.Dir("../client/dist")))

	log.Fatal(http.ListenAndServe(":8080", logRequests(mux)))
}

type statusWriter struct {
	http.ResponseWriter
	code int
}

func (sw *statusWriter) WriteHeader(code int) {
	sw.code = code
	sw.ResponseWriter.WriteHeader(code)
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
