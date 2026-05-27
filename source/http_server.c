/**
 * http_server.c - Lightweight HTTP/1.1 server with embedded Web UI
 *
 * REST API:
 *   GET  /              -> Embedded HTML UI
 *   GET  /api/status    -> JSON status
 *   GET  /api/settings  -> JSON per-day settings
 *   POST /api/set       -> Set all days
 *   POST /api/set_day   -> Set specific day
 *   POST /api/start     -> Start play timer
 *   POST /api/stop      -> Stop play timer
 *   POST /api/reset     -> Reset play time
 *   GET  /api/version   -> Version info
 */

#include "http_server.h"
#include "pctl_handler.h"
#include "tcp_server.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ------------------------------------------------------------------ */
/* State                                                               */
/* ------------------------------------------------------------------ */
static int       s_server_fd = -1;
static bool      s_running   = false;
static pthread_t s_thread;

/* ------------------------------------------------------------------ */
/* Minimal JSON helpers                                                */
/* ------------------------------------------------------------------ */
static char s_json_buf[4096];
static int  s_json_pos = 0;

static void json_reset(void) { s_json_pos = 0; s_json_buf[0] = 0; }

static void json_str(const char *key, const char *val)
{
    s_json_pos += snprintf(s_json_buf + s_json_pos, sizeof(s_json_buf) - s_json_pos,
        "%s\"%s\":\"%s\"", (s_json_pos > 0 ? "," : ""), key, val);
}

static void json_int(const char *key, int val)
{
    s_json_pos += snprintf(s_json_buf + s_json_pos, sizeof(s_json_buf) - s_json_pos,
        "%s\"%s\":%d", (s_json_pos > 0 ? "," : ""), key, val);
}

static void json_uint(const char *key, unsigned int val)
{
    s_json_pos += snprintf(s_json_buf + s_json_pos, sizeof(s_json_buf) - s_json_pos,
        "%s\"%s\":%u", (s_json_pos > 0 ? "," : ""), key, val);
}

static const char *json_get(void) { return s_json_buf; }

/* ------------------------------------------------------------------ */
/* HTTP helpers                                                        */
/* ------------------------------------------------------------------ */
static void http_send(int fd, const char *status, const char *ctype, const char *body)
{
    char header[512];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        status, ctype, (int)strlen(body));
    write(fd, header, hlen);
    write(fd, body, strlen(body));
}

static int http_read_request(int fd, char *buf, int bufsize)
{
    int total = 0;
    while (total < bufsize - 1) {
        int n = read(fd, buf + total, bufsize - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    return total;
}

/* ------------------------------------------------------------------ */
/* Day names                                                           */
/* ------------------------------------------------------------------ */
static const char *day_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int get_today_dow(void)
{
    time_t t = time(NULL);
    if (t == (time_t)-1) return 0;
    struct tm *tm_info = localtime(&t);
    return tm_info ? tm_info->tm_wday : 0;
}

/* ------------------------------------------------------------------ */
/* API handlers                                                        */
/* ------------------------------------------------------------------ */
static void api_status(int fd)
{
    json_reset();
    bool enabled = false, restricted = false;
    u64 remaining_ns = 0;
    u32 daily_limit = 0;

    pctl_is_enabled(&enabled);
    pctl_get_remaining_time(&remaining_ns);
    pctl_is_restricted(&restricted);
    pctl_get_daily_limit_minutes(&daily_limit);

    json_str("version", VERSION_S);
    json_int("enabled", enabled ? 1 : 0);
    json_int("restricted", restricted ? 1 : 0);
    json_uint("remaining_min", NS_TO_MINUTES(remaining_ns));
    json_uint("daily_limit_min", daily_limit);
    json_int("today_dow", get_today_dow());

    http_send(fd, "200 OK", "application/json", json_get());
}

static void api_settings(int fd)
{
    PlayTimerSettings settings;
    memset(&settings, 0, sizeof(settings));

    json_reset();
    s_json_pos += snprintf(s_json_buf, sizeof(s_json_buf), "{\"days\":[");

    if (R_SUCCEEDED(pctl_get_settings(&settings))) {
        for (int i = 0; i < 7; i++) {
            u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(i)];
            char entry[128];
            snprintf(entry, sizeof(entry), "%s{\"day\":%d,\"name\":\"%s\",\"minutes\":%d}",
                (i > 0 ? "," : ""), i, day_names[i],
                (m == PT_DAY_NOLIMIT) ? 0 : (int)m);
            s_json_pos += snprintf(s_json_buf + s_json_pos, sizeof(s_json_buf) - s_json_pos, "%s", entry);
        }
    }
    s_json_pos += snprintf(s_json_buf + s_json_pos, sizeof(s_json_buf) - s_json_pos, "]}");

    http_send(fd, "200 OK", "application/json", json_get());
}

static void api_set(int fd, const char *body)
{
    unsigned int minutes = 0;
    const char *p = strstr(body, "minutes");
    if (p) {
        p = strchr(p + 7, ':');
        if (p) minutes = (unsigned int)atoi(p + 1);
    }

    Result rc = pctl_set_daily_limit_minutes(minutes);
    json_reset();
    json_int("success", R_SUCCEEDED(rc) ? 1 : 0);
    if (R_FAILED(rc))
        json_int("error", (int)rc);
    http_send(fd, "200 OK", "application/json", json_get());
}

static void api_set_day(int fd, const char *body)
{
    int day = 0;
    unsigned int minutes = 0;
    const char *p;

    p = strstr(body, "day");
    if (p) { p = strchr(p + 3, ':'); if (p) day = atoi(p + 1); }

    p = strstr(body, "minutes");
    if (p) { p = strchr(p + 7, ':'); if (p) minutes = (unsigned int)atoi(p + 1); }

    Result rc = pctl_set_day_limit_minutes(day, minutes);
    json_reset();
    json_int("success", R_SUCCEEDED(rc) ? 1 : 0);
    if (R_FAILED(rc))
        json_int("error", (int)rc);
    http_send(fd, "200 OK", "application/json", json_get());
}

static void api_start(int fd)
{
    Result rc = pctl_start_play_timer();
    json_reset();
    json_int("success", R_SUCCEEDED(rc) ? 1 : 0);
    if (R_FAILED(rc))
        json_int("error", (int)rc);
    http_send(fd, "200 OK", "application/json", json_get());
}

static void api_stop(int fd)
{
    Result rc = pctl_stop_play_timer();
    json_reset();
    json_int("success", R_SUCCEEDED(rc) ? 1 : 0);
    if (R_FAILED(rc))
        json_int("error", (int)rc);
    http_send(fd, "200 OK", "application/json", json_get());
}

static void api_reset(int fd)
{
    Result rc = pctl_reset_play_time();
    json_reset();
    json_int("success", R_SUCCEEDED(rc) ? 1 : 0);
    if (R_FAILED(rc))
        json_int("error", (int)rc);
    http_send(fd, "200 OK", "application/json", json_get());
}

static void api_version(int fd)
{
    json_reset();
    json_str("version", VERSION_S);
    json_str("name", "pctltcp-web");
    http_send(fd, "200 OK", "application/json", json_get());
}

/* ------------------------------------------------------------------ */
/* Embedded Web UI (mobile-responsive HTML+CSS+JS)                     */
/* Defined here so handle_request() can reference it.                  */
/* ------------------------------------------------------------------ */
static const char *WEB_HTML =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
"<meta name='apple-mobile-web-app-capable' content='yes'>"
"<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>"
"<title>Switch Parental Control</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e,#16213e,#0f3460);color:#fff;min-height:100vh;padding:16px}"
".card{background:rgba(255,255,255,0.08);backdrop-filter:blur(20px);border-radius:16px;padding:20px;margin-bottom:16px;border:1px solid rgba(255,255,255,0.1)}"
"h1{font-size:1.4em;text-align:center;margin-bottom:4px}"
".subtitle{text-align:center;color:rgba(255,255,255,0.5);font-size:0.85em;margin-bottom:16px}"
".status-grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px}"
".stat{text-align:center;padding:12px 8px;background:rgba(255,255,255,0.06);border-radius:12px}"
".stat-val{font-size:1.8em;font-weight:700}"
".stat-lbl{font-size:0.75em;color:rgba(255,255,255,0.5);margin-top:2px}"
".stat-val.running{color:#4ade80}.stat-val.stopped{color:#f87171}.stat-val.warn{color:#fbbf24}"
".days{display:grid;grid-template-columns:repeat(auto-fill,minmax(90px,1fr));gap:8px;margin-bottom:16px}"
".day{text-align:center;padding:10px 4px;background:rgba(255,255,255,0.06);border-radius:10px;transition:all 0.2s}"
".day.today{border:2px solid #60a5fa;background:rgba(96,165,250,0.15)}"
".day-name{font-size:0.7em;color:rgba(255,255,255,0.5)}"
".day-val{font-size:1.2em;font-weight:600;margin:4px 0}"
".day-val.unlimited{color:#a78bfa}"
".day input{width:60px;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);"
"border-radius:6px;color:#fff;text-align:center;padding:4px;font-size:0.9em}"
".controls{display:grid;grid-template-columns:1fr 1fr;gap:10px}"
".btn{padding:14px;border:none;border-radius:12px;font-size:1em;font-weight:600;cursor:pointer;"
"transition:all 0.2s;text-align:center}"
".btn:active{transform:scale(0.95)}"
".btn-start{background:#22c55e;color:#fff}"
".btn-stop{background:#ef4444;color:#fff}"
".btn-reset{background:#f59e0b;color:#000}"
".btn-set{background:#3b82f6;color:#fff}"
".btn-full{grid-column:1/-1}"
".section-title{font-size:0.85em;color:rgba(255,255,255,0.4);text-transform:uppercase;letter-spacing:1px;margin-bottom:8px}"
".uniform-section{text-align:center;padding:12px 0}"
".uniform-section input{width:80px;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);"
"border-radius:8px;color:#fff;text-align:center;padding:8px;font-size:1.1em;margin:0 8px}"
".uniform-section .unit{color:rgba(255,255,255,0.5);font-size:0.9em}"
"#toast{position:fixed;bottom:20px;left:50%;transform:translateX(-50%) translateY(100px);"
"background:rgba(0,0,0,0.8);backdrop-filter:blur(10px);padding:12px 24px;border-radius:12px;"
"font-size:0.9em;transition:transform 0.3s ease;z-index:999;pointer-events:none}"
"#toast.show{transform:translateX(-50%) translateY(0)}"
".refresh-btn{background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);color:#fff;"
"padding:8px 16px;border-radius:8px;font-size:0.85em;cursor:pointer;display:block;margin:0 auto 16px}"
"</style>"
"</head>"
"<body>"
"<div class='card'><h1>Switch Parental Control</h1><div class='subtitle' id='ver'>Loading...</div></div>"
"<button class='refresh-btn' onclick='loadAll()'>Refresh</button>"
"<div class='card'>"
"<div class='status-grid'>"
"<div class='stat'><div class='stat-val' id='s-timer'>--</div><div class='stat-lbl'>Timer</div></div>"
"<div class='stat'><div class='stat-val' id='s-remain'>--</div><div class='stat-lbl'>Remaining</div></div>"
"<div class='stat'><div class='stat-val' id='s-limit'>--</div><div class='stat-lbl'>Daily Limit</div></div>"
"<div class='stat'><div class='stat-val' id='s-restrict'>--</div><div class='stat-lbl'>Status</div></div>"
"</div></div>"
"<div class='card'>"
"<div class='section-title'>Per-Day Limits</div>"
"<div class='days' id='days-grid'></div>"
"<div class='uniform-section'>Set all: <input type='number' id='uniform-min' value='60' min='0' max='1440'>"
"<span class='unit'>min</span></div>"
"</div>"
"<div class='card'><div class='section-title'>Controls</div>"
"<div class='controls'>"
"<button class='btn btn-start' onclick='doPost(\"/api/start\")'>Start Timer</button>"
"<button class='btn btn-stop' onclick='doPost(\"/api/stop\")'>Stop Timer</button>"
"<button class='btn btn-reset' onclick='doPost(\"/api/reset\")'>Reset Time</button>"
"<button class='btn btn-set' onclick='setUniform()'>Set All Days</button>"
"</div></div>"
"<div id='toast'></div>"
"<script>"
"var D=['Sun','Mon','Tue','Wed','Thu','Fri','Sat'];"
"function toast(m){var t=document.getElementById('toast');t.textContent=m;t.classList.add('show');"
"setTimeout(function(){t.classList.remove('show')},2000)}"
"function get(u){return fetch(u).then(function(r){return r.json()}).catch(function(e){toast('Error: '+e.message);return null})}"
"function doPost(u){fetch(u,{method:'POST'}).then(function(r){return r.json()}).then(function(d){"
"toast(d.success?'OK':'Failed');setTimeout(loadAll,300)}).catch(function(e){toast('Error: '+e.message)})}"
"function loadStatus(){return get('/api/status').then(function(d){if(!d)return 0;"
"document.getElementById('ver').textContent='pctltcp-web v'+(d.version||'1.0.0');"
"var te=document.getElementById('s-timer');te.textContent=d.enabled?'ON':'OFF';"
"te.className='stat-val '+(d.enabled?'running':'stopped');"
"var rm=document.getElementById('s-remain');rm.textContent=d.remaining_min+'m';"
"rm.className='stat-val '+(d.remaining_min<15&&d.enabled?'warn':'running');"
"document.getElementById('s-limit').textContent=d.daily_limit_min+'m';"
"var rs=document.getElementById('s-restrict');rs.textContent=d.restricted?'BLOCKED':'FREE';"
"rs.className='stat-val '+(d.restricted?'stopped':'running');"
"return d.today_dow})}"
"function loadDays(){get('/api/settings').then(function(d){if(!d)return;"
"var g=document.getElementById('days-grid');g.innerHTML='';"
"for(var i=0;i<d.days.length;i++){var dy=d.days[i];var isT=dy.day===window._today;"
"var div=document.createElement('div');div.className='day'+(isT?' today':'');"
"div.innerHTML='<div class=\"day-name\">'+dy.name+'</div>"
"<div class=\"day-val '+(dy.minutes===0?'unlimited':'')+'\">'+"
"(dy.minutes===0?'Unlimited':dy.minutes+'m')+'</div>"
"<input type=\"number\" value=\"'+dy.minutes+'\" min=\"0\" max=\"1440\" data-day=\"'+dy.day+'\">';"
"g.appendChild(div)}"
"g.querySelectorAll('input').forEach(function(inp){inp.addEventListener('change',function(){"
"var day=parseInt(this.dataset.day);var min=parseInt(this.value);"
"fetch('/api/set_day',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({day:day,minutes:min})}).then(function(r){return r.json()})."
"then(function(d){toast(d.success?'Set '+D[day]+': '+min+'m':'Failed');loadAll()})})})})}"
"function setUniform(){var m=parseInt(document.getElementById('uniform-min').value);"
"fetch('/api/set',{method:'POST',headers:{'Content-Type':'application/json'},"
"body:JSON.stringify({minutes:m})}).then(function(r){return r.json()})."
"then(function(d){toast(d.success?'All set to '+m+'m':'Failed');setTimeout(loadAll,300)})}"
"function loadVersion(){get('/api/version').then(function(d){"
"if(d)document.getElementById('ver').textContent='pctltcp-web v'+d.version})}"
"function loadAll(){loadStatus().then(function(dow){window._today=dow;loadDays();loadVersion()})}"
"window._today=0;loadAll();setInterval(loadAll,10000)"
"</script>"
"</body>"
"</html>";

/* ------------------------------------------------------------------ */
/* Route dispatcher                                                    */
/* ------------------------------------------------------------------ */
static void handle_request(int fd)
{
    char buf[2048];
    int n = http_read_request(fd, buf, sizeof(buf));
    if (n <= 0) { close(fd); return; }

    char method[16] = {0}, path[256] = {0};
    sscanf(buf, "%15s %255s", method, path);

    /* CORS preflight */
    if (strcmp(method, "OPTIONS") == 0) {
        http_send(fd, "204 No Content", "text/plain", "");
        close(fd);
        return;
    }

    char *body = strstr(buf, "\r\n\r\n");
    if (body) body += 4;

    if (strcmp(path, "/") == 0 && strcmp(method, "GET") == 0) {
        http_send(fd, "200 OK", "text/html; charset=utf-8", WEB_HTML);
    } else if (strcmp(path, "/api/status") == 0) {
        api_status(fd);
    } else if (strcmp(path, "/api/settings") == 0) {
        api_settings(fd);
    } else if (strcmp(path, "/api/set") == 0) {
        api_set(fd, body ? body : "");
    } else if (strcmp(path, "/api/set_day") == 0) {
        api_set_day(fd, body ? body : "");
    } else if (strcmp(path, "/api/start") == 0) {
        api_start(fd);
    } else if (strcmp(path, "/api/stop") == 0) {
        api_stop(fd);
    } else if (strcmp(path, "/api/reset") == 0) {
        api_reset(fd);
    } else if (strcmp(path, "/api/version") == 0) {
        api_version(fd);
    } else {
        http_send(fd, "404 Not Found", "application/json", "{\"error\":\"not found\"}");
    }

    close(fd);
}

/* ------------------------------------------------------------------ */
/* Server thread                                                       */
/* ------------------------------------------------------------------ */
static void *http_thread_func(void *arg)
{
    (void)arg;

    while (s_running) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s_server_fd, &rfds);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        int ret = select(s_server_fd + 1, &rfds, NULL, NULL, &tv);
        if (ret <= 0) continue;

        if (FD_ISSET(s_server_fd, &rfds)) {
            int client_fd = accept(s_server_fd, NULL, NULL);
            if (client_fd >= 0)
                handle_request(client_fd);
        }
    }

    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void http_server_start(void)
{
    struct sockaddr_in addr;

    s_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_server_fd < 0) return;

    int optval = 1;
    setsockopt(s_server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s_server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_server_fd);
        s_server_fd = -1;
        return;
    }

    if (listen(s_server_fd, 4) < 0) {
        close(s_server_fd);
        s_server_fd = -1;
        return;
    }

    s_running = true;
    pthread_create(&s_thread, NULL, http_thread_func, NULL);
    pthread_detach(s_thread);
}

void http_server_stop(void)
{
    s_running = false;
    if (s_server_fd >= 0) {
        shutdown(s_server_fd, SHUT_RDWR);
        close(s_server_fd);
        s_server_fd = -1;
    }
}

bool http_server_is_running(void)
{
    return s_running;
}
