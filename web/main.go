package main

import (
	"encoding/json"
	"log"
	"net/http"

	"github.com/gorilla/websocket"
)

var upgrader = websocket.Upgrader{
	ReadBufferSize:  1024,
	WriteBufferSize: 1024,
	CheckOrigin: func(r *http.Request) bool {
		return true
	},
}

func HandleConnections(w http.ResponseWriter, r *http.Request) {
	// question for later what can I put in place for nil
	conn, err := upgrader.Upgrade(w, r, nil)
	if err != nil {
		WriteJSONResponse(w, http.StatusBadRequest, JSONMap{"error": err.Error()})
		return
	}
	defer conn.Close()
	log.Println("connection to client successful")
	// needs to run for the duration of the connection
	for {
		messageType, message, err := conn.ReadMessage()
		if err != nil {
			log.Printf("could not read message: %v", err)
			break
		}

		log.Printf("message received: %s\n", message)

		if err := conn.WriteMessage(messageType, message); err != nil {
			log.Printf("error writing message: %v", err)
			break
		}
	}
}

func main() {
	log.Println("booting application")

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", HandleConnections)

	mux.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		WriteJSONResponse(w, http.StatusNotFound, JSONMap{"message": "hello"})
	})

	log.Fatal(http.ListenAndServe(":80", mux))
}

type JSONMap map[string]any

func WriteJSONResponse(w http.ResponseWriter, code int, data any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	if err := json.NewEncoder(w).Encode(data); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}
