/*
 * RTSP/HTTP server of the AirPlay receiver: accepts connections on port 7000, frames
 * requests (header + Content-Length body) and sends responses. Replaces UxPlay's
 * lib/httpd.c + llhttp + http_request/response with a minimal parser.
 * Copyright (C) 2011-2012 Juho Vähä-Herttua, (C) 2021-2025 F. Duncanh; ESP32 port 2026.
 *
 * This program is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version. See LICENSE-GPL.md.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "ap_internal.h"

#define MAX_CONNS 3
#define MAX_REQUEST (64u * 1024u)
#define MAX_HEADER 8192u
#define PARTIAL_TIMEOUT_US 5000000LL
#define IDLE_TIMEOUT_US 30000000LL

airplay_cfg ap_cfg;
static ap_conn conns[MAX_CONNS];
static atomic_bool server_run, server_done = true;
static int listen_fd = -1;
static atomic_uint_fast32_t owner_activity_ms;

bool ap_control_recent(uint32_t within_ms) {
  uint32_t last=atomic_load(&owner_activity_ms);
  return last>0&&(uint32_t)((uint32_t)(esp_timer_get_time()/1000)-last)<within_ms;
}

void ap_log(const char *fmt, ...) {
  if (!ap_cfg.log) return;
  char msg[192];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  ap_cfg.log(msg);
}

const char *ap_header(const ap_req *r, const char *name) {
  for (int i = 0; i < r->nhdr; i++)
    if (!strcasecmp(r->names[i], name)) return r->vals[i];
  return NULL;
}

void ap_resp_header(ap_resp *r, const char *name, const char *val) {
  if (!name || !val || strchr(name, '\r') || strchr(name, '\n') ||
      strchr(val, '\r') || strchr(val, '\n') || r->hlen >= sizeof r->hdrs) return;
  int n = snprintf(r->hdrs + r->hlen, sizeof r->hdrs - r->hlen, "%s: %s\r\n", name, val);
  if (n > 0 && (size_t)n < sizeof r->hdrs - r->hlen) r->hlen += (size_t)n;
}

int airplay_state(void) {
  if (ap_mirror_streaming()) return AIRPLAY_STREAMING;
  for (int i = 0; i < MAX_CONNS; i++)
    if (conns[i].fd >= 0 && (conns[i].have_keymsg || ap_mirror_owner == &conns[i])) return AIRPLAY_CONNECTED;
  return AIRPLAY_IDLE;
}

const char *airplay_source(void) { return ap_mirror_owner ? ap_mirror_owner->model : ""; }

static bool send_all(int fd, const void *p, size_t n) {
  const uint8_t *b = p;
  while (n && atomic_load(&server_run)) {
    int k = send(fd, b, n, 0);
    if (k <= 0) return false;
    b += k;
    n -= (size_t)k;
  }
  return n == 0;
}

static void close_conn(ap_conn *c) {
  if (ap_mirror_owner == c) {
    ap_mirror_stop();
    ap_mirror_owner = NULL;
    if (atomic_load(&server_run))
      ap_receiver_notify(AIRPLAY_RECEIVER_ADVERTISING, "Mirror session closed; receiver available");
  }
  if (c->fd >= 0) close(c->fd);
  free(c->buf);
  memset(c, 0, sizeof *c);
  c->fd = -1;
  ap_log("rtsp: connection closed");
  if (!ap_mirror_owner && atomic_load(&server_run)) {
    bool connecting = false;
    for (int i = 0; i < MAX_CONNS; ++i)
      connecting |= conns[i].fd >= 0 && (conns[i].have_keymsg || conns[i].pair.status != PAIR_INITIAL);
    if (!connecting) ap_receiver_notify(AIRPLAY_RECEIVER_ADVERTISING, "Control connections closed; receiver available");
  }
}

static bool parse_head(char *head, ap_req *r) {
  char *line = head, *next;
  memset(r, 0, sizeof *r);
  next = strstr(line, "\r\n");
  if (next) *next = 0;
  r->method = strtok_r(line, " ", &line);
  r->url = strtok_r(NULL, " ", &line);
  r->proto = strtok_r(NULL, " ", &line);
  if (!r->method || !r->url || !r->proto || strtok_r(NULL, " ", &line) ||
      strlen(r->method) > 24 || strlen(r->url) > 1024 ||
      (strcmp(r->proto, "RTSP/1.0") && strcmp(r->proto, "HTTP/1.1") && strcmp(r->proto, "HTTP/1.0"))) return false;
  for (line = next ? next + 2 : NULL; line && *line; line = next ? next + 2 : NULL) {
    next = strstr(line, "\r\n");
    if (next) *next = 0;
    char *colon = strchr(line, ':');
    if (!colon || colon == line || r->nhdr == AP_MAX_HDRS) return false;
    *colon++ = 0;
    while (*colon == ' ' || *colon == '\t') colon++;
    r->names[r->nhdr] = line;
    r->vals[r->nhdr++] = colon;
  }
  return true;
}

/* Inspect bounded raw headers before mutating the receive buffer. Reject ambiguous framing. */
static bool content_length(const uint8_t *p, size_t n, size_t *body_len) {
  size_t at = 0;
  bool first = true, found = false;
  *body_len = 0;
  while (at + 2 <= n) {
    const uint8_t *end = memmem(p + at, n - at, "\r\n", 2);
    if (!end) return false;
    size_t count = (size_t)(end - (p + at));
    if (!count) return true;
    if (!first) {
      const uint8_t *colon = memchr(p + at, ':', count);
      if (!colon || colon == p + at) return false;
      size_t keylen = (size_t)(colon - (p + at));
      if (keylen == 17 && !strncasecmp((const char *)p + at, "Transfer-Encoding", keylen)) return false;
      if (keylen == 14 && !strncasecmp((const char *)p + at, "Content-Length", keylen)) {
        if (found) return false;
        found = true;
        const uint8_t *v = colon + 1;
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        if (v == end || *v < '0' || *v > '9') return false;
        size_t value = 0;
        while (v < end && *v >= '0' && *v <= '9') {
          unsigned digit = *v++ - '0';
          if (value > (MAX_REQUEST - digit) / 10) return false;
          value = value * 10 + digit;
        }
        while (v < end && (*v == ' ' || *v == '\t')) ++v;
        if (v != end) return false;
        *body_len = value;
      }
    }
    first = false;
    at += count + 2;
  }
  return false;
}

static bool respond(ap_conn *c, const ap_req *req, ap_resp *res) {
  char head[768];
  int n = snprintf(head, sizeof head, "%s %d %s\r\n%.*sContent-Length: %u\r\n\r\n", req->proto, res->code,
                   res->reason, (int)res->hlen, res->hdrs, (unsigned)res->body_len);
  bool ok = n > 0 && n < (int)sizeof head && send_all(c->fd, head, (size_t)n) &&
            (!res->body_len || send_all(c->fd, res->body, res->body_len));
  free(res->body);
  return ok && !res->close;
}

static bool process(ap_conn *c) {
  for (;;) {
    uint8_t *end = c->len >= 4 ? memmem(c->buf, c->len, "\r\n\r\n", 4) : NULL;
    if (!end) return c->len < MAX_HEADER;
    size_t head_len = (size_t)(end - c->buf) + 4, body_len = 0;
    if (head_len > MAX_HEADER || memchr(c->buf, 0, head_len) ||
        !content_length(c->buf, head_len, &body_len) || body_len > MAX_REQUEST - head_len) return false;
    if (c->len < head_len + body_len) return true;
    *end = 0;
    ap_req req;
    ap_resp res = {.code = 200, .reason = "OK"};
    if (!parse_head((char *)c->buf, &req)) return false;
    req.body = c->buf + head_len;
    req.body_len = body_len;
    c->last_request_us = esp_timer_get_time();
    ap_handle(c, &req, &res);
    if(c==ap_mirror_owner)atomic_store(&owner_activity_ms,(uint32_t)(esp_timer_get_time()/1000));
    if (!respond(c, &req, &res)) return false;
    size_t consumed = head_len + body_len;
    memmove(c->buf, c->buf + consumed, c->len - consumed);
    c->len -= consumed;
    c->partial_since_us = c->len ? esp_timer_get_time() : 0;
    if (!c->len) return true;
  }
}

static bool read_conn(ap_conn *c) {
  if (c->len == c->cap) {
    size_t cap = c->cap ? c->cap * 2 : 4096;
    if (cap > MAX_REQUEST) cap = MAX_REQUEST;
    if (cap <= c->cap) return false;
    uint8_t *b = realloc(c->buf, cap + 1);
    if (!b) return false;
    c->buf = b;
    c->cap = cap;
  }
  int n = recv(c->fd, c->buf + c->len, c->cap - c->len, 0);
  if (n <= 0) return false;
  if (!c->len) c->partial_since_us = esp_timer_get_time();
  c->len += (size_t)n;
  c->buf[c->len] = 0;
  return process(c);
}

static void server_task(void *arg) {
  (void)arg;
  int one = 1;
  while (atomic_load(&server_run)) {
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(listen_fd, &rf);
    int maxfd = listen_fd;
    int64_t now = esp_timer_get_time();
    for (int i = 0; i < MAX_CONNS; i++) {
      ap_conn *c = &conns[i];
      if (c->fd < 0) continue;
      if ((c->partial_since_us && now - c->partial_since_us > PARTIAL_TIMEOUT_US) ||
          (ap_mirror_owner != c && now - c->last_request_us > IDLE_TIMEOUT_US)) {
        ap_log("rtsp: partial-request or idle timeout");
        close_conn(c);
        continue;
      }
      FD_SET(c->fd, &rf);
      if (c->fd > maxfd) maxfd = c->fd;
    }
    struct timeval tv = {0, 100000};
    int ready = select(maxfd + 1, &rf, NULL, NULL, &tv);
    if (!atomic_load(&server_run)) break;
    if (ready <= 0) continue;
    if (FD_ISSET(listen_fd, &rf)) {
      struct sockaddr_in peer;
      socklen_t pl = sizeof peer;
      int fd = accept(listen_fd, (struct sockaddr *)&peer, &pl);
      ap_conn *c = NULL;
      for (int i = 0; i < MAX_CONNS && !c; i++) if (conns[i].fd < 0) c = &conns[i];
      if (fd >= 0 && (!c || (ap_cfg.allow_peer && !ap_cfg.allow_peer(peer.sin_addr.s_addr)))) {
        close(fd);
      } else if (fd >= 0) {
        memset(c, 0, sizeof *c);
        c->fd = fd;
        c->remote_ip = peer.sin_addr.s_addr;
        c->last_request_us = esp_timer_get_time();
        struct timeval timeout = {2, 0};
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
        ap_log("rtsp: connection from %s", inet_ntoa(peer.sin_addr));
      }
    }
    for (int i = 0; i < MAX_CONNS; i++)
      if (conns[i].fd >= 0 && FD_ISSET(conns[i].fd, &rf) && !read_conn(&conns[i])) close_conn(&conns[i]);
  }
  for (int i = 0; i < MAX_CONNS; i++) if (conns[i].fd >= 0) close_conn(&conns[i]);
  ap_mirror_stop();
  if (listen_fd >= 0) close(listen_fd);
  listen_fd = -1;
  atomic_store(&server_done, true);
  vTaskDelete(NULL);
}

void airplay_set_display(uint16_t width, uint16_t height, uint8_t max_fps) {
  ap_cfg.width = width;
  ap_cfg.height = height;
  ap_cfg.max_fps = max_fps;
}

bool airplay_start(const airplay_cfg *cfg) {
  if (!cfg || !atomic_load(&server_done)) return false;
  ap_cfg = *cfg;
  for (int i = 0; i < MAX_CONNS; i++) conns[i].fd = -1;
  if (!ap_crypto_init(cfg->ed25519_seed)) return false;
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) return false;
  int one = 1;
  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(AP_RTSP_PORT), .sin_addr.s_addr = htonl(INADDR_ANY)};
  if (bind(listen_fd, (struct sockaddr *)&a, sizeof a) || listen(listen_fd, MAX_CONNS) || !ap_mdns_start()) {
    close(listen_fd);
    listen_fd = -1;
    ap_mdns_stop();
    return false;
  }
  atomic_store(&server_run, true);
  atomic_store(&server_done, false);
  if (xTaskCreatePinnedToCore(server_task, "airplay_rtsp", 12 * 1024, NULL, 5, NULL, 0) != pdPASS) {
    atomic_store(&server_run, false);
    atomic_store(&server_done, true);
    close(listen_fd);
    listen_fd = -1;
    ap_mdns_stop();
    return false;
  }
  return true;
}

bool airplay_stop(void) {
  atomic_store(&server_run, false);
  ap_mdns_stop();
  for (int i = 0; i < 600 && !atomic_load(&server_done); ++i) vTaskDelay(pdMS_TO_TICKS(10));
  return atomic_load(&server_done) && !ap_mirror_running();
}

void airplay_readvertise(void) {
  ap_mdns_stop();
  ap_log(ap_mdns_start() ? "mdns: advertising again" : "mdns: advertising failed");
}
