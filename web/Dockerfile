FROM golang:1.26.4-alpine AS builder

WORKDIR /control-panel/web

COPY go.mod go.sum ./
RUN go mod download

COPY . .
RUN go build -o main

FROM alpine:latest

WORKDIR /control-panel


COPY --from=builder /control-panel/web/main ./main

ENTRYPOINT [ "./main" ]
