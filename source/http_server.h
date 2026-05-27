// pctltcp-web — HTTP Server Header
// ====================================
// Lightweight HTTP server for Switch NRO.
// Serves embedded Web UI and REST API for mobile control.
//
// REST API:
//   GET  /              -> Web UI HTML
//   GET  /api/status    -> {"enabled","restricted","remaining_min","daily_limit_min"}
//   GET  /api/settings  -> {"days":[{"name","minutes"},...]}
//   POST /api/set       -> body: {"minutes":N}           set all 7 days
//   POST /api/set_day   -> body: {"day":0,"minutes":N}   set one day (0=Sun..6=Sat)
//   POST /api/start     -> start play timer
//   POST /api/stop      -> stop play timer
//   POST /api/reset     -> reset today's play counter
//   GET  /api/version   -> {"version":"1.0.0"}
// ====================================

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <switch.h>
#include <stdbool.h>

#define HTTP_PORT 8080

void http_server_start(void);
void http_server_stop(void);
bool http_server_is_running(void);

#endif // HTTP_SERVER_H
