FROM oven/bun:1-alpine AS frontend

WORKDIR /control-panel/client

COPY client/package.json client/bun.lock ./
RUN bun install --frozen-lockfile

COPY client/ ./
RUN bun run build.ts

FROM golang:1.26.4-alpine AS builder

WORKDIR /control-panel/web

COPY web/go.mod web/go.sum ./
RUN go mod download

COPY web/ ./
RUN go build -o main

FROM alpine:latest

WORKDIR /control-panel

COPY --from=builder /control-panel/web/main ./main
COPY --from=frontend /control-panel/client/dist ./dist

ENTRYPOINT [ "./main" ]
