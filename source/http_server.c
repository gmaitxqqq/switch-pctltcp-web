// pctltcp-web — HTTP Server Implementation
// ==========================================
// Lightweight HTTP/1.1 server using raw BSD sockets.
// Runs in a pthread alongside the TCP command server.
// Handles REST API calls and serves the embedded mobile Web UI.
// ==========================================

#include "http_server.h"
#include "pctl_handler.h"
#include "tcp_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>

// ---- Embedded Web UI HTML ----
// Compressed/stored as a C string constant.
// We include it from a generated header to keep this file clean.
// For simplicity, we embed it directly as a raw string.

static const char *WEB_UI_HTML =
"<!DOCTYPE html>"
"<html lang='en'>"
"<head>"
"<meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no'>"
"<meta name='apple-mobile-web-app-capable' content='yes'>"
"<meta name='apple-mobile-web-app-status-bar-style' content='black-translucent'>"
"<title>SWPC - Switch Parental Control</title>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0}"
"body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;"
"background:linear-gradient(135deg,#1a1a2e 0%%,#16213e 50%%,#0f3460 100%%);"
"color:#e0e0e0;min-height:100vh;padding:16px;max-width:480px;margin:0 auto}"
".card{background:rgba(255,255,255,0.08);backdrop-filter:blur(10px);"
"border-radius:16px;padding:20px;margin-bottom:16px;border:1px solid rgba(255,255,255,0.1)}"
"h1{text-align:center;font-size:1.4em;margin-bottom:4px;color:#fff}"
".sub{text-align:center;font-size:0.8em;color:#888;margin-bottom:16px}"
".label{font-size:0.85em;color:#aaa;margin-bottom:6px}"
".value{font-size:1.8em;font-weight:700;color:#4fc3f7;margin-bottom:12px}"
".value.warn{color:#ff7043}"
".value.ok{color:#66bb6a}"
".grid{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:12px}"
".day-card{background:rgba(255,255,255,0.06);border-radius:12px;padding:12px;text-align:center}"
".day-name{font-size:0.75em;color:#aaa;margin-bottom:4px}"
".day-input{width:100%%;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);"
"border-radius:8px;color:#fff;font-size:1em;padding:8px;text-align:center;-webkit-appearance:none}"
".day-input:focus{outline:none;border-color:#4fc3f7}"
".btn{display:block;width:100%%;padding:14px;border:none;border-radius:12px;"
"font-size:1em;font-weight:600;cursor:pointer;margin-bottom:8px;transition:transform 0.1s,opacity 0.1s}"
".btn:active{transform:scale(0.97);opacity:0.8}"
".btn-primary{background:linear-gradient(135deg,#4fc3f7,#0288d1);color:#fff}"
".btn-success{background:linear-gradient(135deg,#66bb6a,#2e7d32);color:#fff}"
".btn-warning{background:linear-gradient(135deg,#ffb74d,#e65100);color:#fff}"
".btn-danger{background:linear-gradient(135deg,#ef5350,#b71c1c);color:#fff}"
".btn-gray{background:rgba(255,255,255,0.1);color:#ccc}"
".btn-row{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:8px}"
".status-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;"
"border-bottom:1px solid rgba(255,255,255,0.06)}"
".status-row:last-child{border-bottom:none}"
".status-key{font-size:0.85em;color:#aaa}"
".status-val{font-size:0.95em;font-weight:600}"
".connecting{text-align:center;padding:20px;color:#888}"
".error{text-align:center;padding:20px;color:#ef5350}"
".toast{position:fixed;top:20px;left:50%%;transform:translateX(-50%%);"
"background:rgba(0,0,0,0.8);color:#fff;padding:10px 20px;border-radius:20px;"
"font-size:0.85em;z-index:999;opacity:0;transition:opacity 0.3s;pointer-events:none}"
".toast.show{opacity:1}"
"</style>"
"</head>"
"<body>"
"<div class='card'>"
"<h1>SWPC</h1>"
"<div class='sub'>Switch Parental Control</div>"
"<div id='conn'>"
"<button class='btn btn-primary' onclick='doConnect()'>Connect</button>"
"<div style='text-align:center;margin-top:8px'>"
"<input id='ip' type='text' value='192.168.31.143:8080' "
"style='width:200px;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);"
"border-radius:8px;color:#fff;padding:8px;text-align:center;font-size:0.9em;-webkit-appearance:none'>"
"</div></div>"
"</div>"
"<div id='main' style='display:none'>"
"<div class='card' id='statusCard'>"
"<div class='label'>Timer Status</div>"
"<div class='value' id='timerVal'>--</div>"
"<div class='status-row'><span class='status-key'>Today Limit</span>"
"<span class='status-val' id='limitVal'>--</span></div>"
"<div class='status-row'><span class='status-key'>Remaining</span>"
"<span class='status-val' id='remainVal'>--</span></div>"
"<div class='status-row'><span class='status-key'>Restricted</span>"
"<span class='status-val' id='restrictVal'>--</span></div>"
"</div>"
"<div class='card'>"
"<div class='label'>Daily Time Limits (minutes)</div>"
"<div class='grid'>"
"<div class='day-card'><div class='day-name'>Sun</div>"
"<input class='day-input' type='number' id='d0' min='0' max='1440' value='60'></div>"
"<div class='day-card'><div class='day-name'>Mon</div>"
"<input class='day-input' type='number' id='d1' min='0' max='1440' value='60'></div>"
"<div class='day-card'><div class='day-name'>Tue</div>"
"<input class='day-input' type='number' id='d2' min='0' max='1440' value='60'></div>"
"<div class='day-card'><div class='day-name'>Wed</div>"
"<input class='day-input' type='number' id='d3' min='0' max='1440' value='60'></div>"
"<div class='day-card'><div class='day-name'>Thu</div>"
"<input class='day-input' type='number' id='d4' min='0' max='1440' value='60'></div>"
"<div class='day-card'><div class='day-name'>Fri</div>"
"<input class='day-input' type='number' id='d5' min='0' max='1440' value='60'></div>"
"<div class='day-card'><div class='day-name'>Sat</div>"
"<input class='day-input' type='number' id='d6' min='0' max='1440' value='60'></div>"
"<div class='day-card'><div class='day-name'>All</div>"
"<input class='day-input' type='number' id='dall' min='0' max='1440' value='60'></div>"
"</div>"
"<button class='btn btn-primary' onclick='applyDays()'>Apply Daily Limits</button>"
"<button class='btn btn-gray' onclick='applyAll()'>Apply to All Days</button>"
"</div>"
"<div class='card'>"
"<div class='btn-row'>"
"<button class='btn btn-success' onclick='doStart()'>Start</button>"
"<button class='btn btn-warning' onclick='doStop()'>Pause</button>"
"</div>"
"<button class='btn btn-danger' onclick='doReset()'>Reset Today</button>"
"</div>"
"<button class='btn btn-gray' onclick='doDisconnect()' style='margin-bottom:0'>Disconnect</button>"
"</div>"
"<div id='toast' class='toast'></div>"
"<script>"
"var BASE='';"
"var pollId=null;"
"function doConnect(){"
"var v=document.getElementById('ip').value.trim();"
"BASE='http://'+v;document.getElementById('conn').style.display='none';"
"document.getElementById('main').style.display='block';startPoll();refresh();}"
"function doDisconnect(){stopPoll();document.getElementById('main').style.display='none';"
"document.getElementById('conn').style.display='block';}"
"function startPoll(){pollId=setInterval(refresh,5000)}"
"function stopPoll(){if(pollId){clearInterval(pollId);pollId=null}}"
"function refresh(){fetch(BASE+'/api/status').then(function(r){return r.json()}).then(function(d){"
"var t=document.getElementById('timerVal');"
"t.textContent=d.enabled?'Running':'Paused';"
"t.className='value '+(d.enabled?'ok':'warn');"
"document.getElementById('limitVal').textContent=d.daily_limit_min>0?d.daily_limit_min+' min':'Unlimited';"
"document.getElementById('remainVal').textContent=d.remaining_min+' min';"
"document.getElementById('restrictVal').textContent=d.restricted?'Yes':'No';"
"document.getElementById('restrictVal').style.color=d.restricted?'#ef5350':'#66bb6a';"
"}).catch(function(){toast('Connection lost')});"
"fetch(BASE+'/api/settings').then(function(r){return r.json()}).then(function(d){"
"for(var i=0;i<7;i++){document.getElementById('d'+i).value=d.days[i].minutes==65535?'':d.days[i].minutes}"
"}).catch(function(){});"
"}"
"function applyDays(){for(var i=0;i<7;i++){var v=parseInt(document.getElementById('d'+i).value)||0;"
"doPost('/api/set_day',JSON.stringify({day:i,minutes:v}))}"
"toast('Applied!');refresh()}"
"function applyAll(){var v=parseInt(document.getElementById('dall').value)||0;"
"doPost('/api/set',JSON.stringify({minutes:v}));"
"for(var i=0;i<7;i++)document.getElementById('d'+i).value=v;"
"toast('Applied to all!');refresh()}"
"function doStart(){doPost('/api/start','{}');toast('Timer started');refresh()}"
"function doStop(){doPost('/api/stop','{}');toast('Timer paused');refresh()}"
"function doReset(){if(confirm('Reset today play time?')){doPost('/api/reset','{}');toast('Today reset');refresh()}}"
"function doPost(path,body){fetch(BASE+path,{method:'POST',headers:{'Content-Type':'application/json'},body:body})"
".catch(function(){toast('Error')})}"
"function toast(msg){var t=document.getElementById('toast');t.textContent=msg;t.classList.add('show');"
"setTimeout(function(){t.classList.remove('show')},2000)}"
"</script>"
"</body>"
"</html>";

// ---- HTTP Server State ----
static volatile bool s_http_running = false;
static pthread_t s_http_thread;
static int s_http_fd = -1;

// ---- JSON Helpers ----
// Minimal JSON writing — no external dependencies needed.

static void json_write_str(char *buf, size_t *pos, size_t maxlen,
                            const char *key, const char *val)
{
    if (*pos < maxlen)
        *pos += snprintf(buf + *pos, maxlen - *pos,
                         "\"%s\":\"%s\"", key, val);
}

static void json_write_int(char *buf, size_t *pos, size_t maxlen,
                            const char *key, int val)
{
    if (*pos < maxlen)
        *pos += snprintf(buf + *pos, maxlen - *pos,
                         "\"%s\":%d", key, val);
}

static void json_write_bool(char *buf, size_t *pos, size_t maxlen,
                             const char *key, bool val)
{
    if (*pos < maxlen)
        *pos += snprintf(buf + *pos, maxlen - *pos,
                         "\"%s\":%s", key, val ? "true" : "false");
}

static void json_open_obj(char *buf, size_t *pos, size_t maxlen)
{
    if (*pos < maxlen) buf[(*pos)++] = '{';
}

static void json_close_obj(char *buf, size_t *pos, size_t maxlen)
{
    if (*pos < maxlen) buf[(*pos)++] = '}';
}

static void json_sep(char *buf, size_t *pos, size_t maxlen)
{
    if (*pos < maxlen) buf[(*pos)++] = ',';
}

// ---- HTTP Helpers ----

static void http_send_response(int client_fd, int status_code,
                                const char *status_text,
                                const char *content_type,
                                const char *body)
{
    char header[512];
    int body_len = body ? (int)strlen(body) : 0;
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "\r\n",
        status_code, status_text,
        content_type, body_len);
    send(client_fd, header, hlen, 0);
    if (body && body_len > 0)
        send(client_fd, body, body_len, 0);
}

// Read a full HTTP request line
static int http_read_request(int fd, char *buf, size_t bufsize)
{
    size_t total = 0;
    while (total < bufsize - 1) {
        int n = recv(fd, buf + total, 1, 0);
        if (n <= 0) return -1;
        total++;
        // Look for \r\n\r\n (end of headers)
        if (total >= 4 && buf[total-1] == '\n' && buf[total-2] == '\r'
            && buf[total-3] == '\n' && buf[total-4] == '\r') {
            buf[total] = '\0';
            return (int)total;
        }
        // Timeout safety: limit header size
        if (total > 4096) return -1;
    }
    return -1;
}

// ---- API Handlers ----

static void handle_api_status(int fd)
{
    char json[512];
    size_t pos = 0;
    bool enabled = false, restricted = false;
    u64 remaining_ns = 0;
    int daily_limit = 0;

    pctl_is_enabled(&enabled);
    pctl_is_restricted(&restricted);
    if (R_SUCCEEDED(pctl_get_remaining_time(&remaining_ns)))
        daily_limit = (int)(remaining_ns / 60000000000ULL);

    // Get today's limit
    PlayTimerSettings settings;
    if (R_SUCCEEDED(pctl_get_settings(&settings))) {
        int dow = 0; u64 ts = 0; TimeCalendarAdditionalInfo info;
        
        u64 timestamp = 0;
        TimeCalendarTime cal;
        TimeCalendarAdditionalInfo info;
        if (R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, &timestamp))) {
            timeToCalendarTimeWithMyRule(timestamp, &cal, &info);
            // cal.wday: 0=Sun..6=Sat
            dow = cal.wday;
        }
        u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(dow)];
        daily_limit = (m == 0xFFFFu) ? 0 : (int)m;
    }

    int remaining = (int)(remaining_ns / 60000000000ULL);

    json_open_obj(json, &pos, sizeof(json));
    json_write_bool(json, &pos, sizeof(json), "enabled", enabled);
    json_write_bool(json, &pos, sizeof(json), "restricted", restricted);
    json_sep(json, &pos, sizeof(json));
    json_write_int(json, &pos, sizeof(json), "remaining_min", remaining);
    json_sep(json, &pos, sizeof(json));
    json_write_int(json, &pos, sizeof(json), "daily_limit_min", daily_limit);
    json_close_obj(json, &pos, sizeof(json));
    json[pos] = '\0';

    http_send_response(fd, 200, "OK", "application/json", json);
}

static void handle_api_settings(int fd)
{
    char json[1024];
    size_t pos = 0;
    PlayTimerSettings settings;

    json_open_obj(json, &pos, sizeof(json));

    if (R_SUCCEEDED(pctl_get_settings(&settings))) {
        static const char *names[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
        pos += snprintf(json + pos, sizeof(json) - pos, "\"days\":[");
        for (int i = 0; i < 7; i++) {
            u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(i)];
            if (i > 0) json[pos++] = ',';
            pos += snprintf(json + pos, sizeof(json) - pos,
                "{\"name\":\"%s\",\"minutes\":%d}",
                names[i], (m == 0xFFFFu) ? 65535 : (int)m);
        }
        pos += snprintf(json + pos, sizeof(json) - pos, "]");
    }

    json_close_obj(json, &pos, sizeof(json));
    json[pos] = '\0';

    http_send_response(fd, 200, "OK", "application/json", json);
}

static void handle_api_set(int fd, const char *body)
{
    // Parse {"minutes":N}
    int minutes = 0;
    if (body) {
        const char *p = strstr(body, "\"minutes\"");
        if (p) {
            p = strchr(p + 9, ':');
            if (p) minutes = atoi(p + 1);
        }
    }

    char json[256];
    size_t pos = 0;
    Result rc = pctl_set_uniform(minutes);

    json_open_obj(json, &pos, sizeof(json));
    json_write_int(json, &pos, sizeof(json), "rc", (int)rc);
    json_sep(json, &pos, sizeof(json));
    json_write_str(json, &pos, sizeof(json), "message",
                   R_SUCCEEDED(rc) ? "OK" : "Failed");
    json_close_obj(json, &pos, sizeof(json));
    json[pos] = '\0';

    http_send_response(fd, 200, "OK", "application/json", json);
}

static void handle_api_set_day(int fd, const char *body)
{
    int day = 0, minutes = 0;
    if (body) {
        const char *p;
        p = strstr(body, "\"day\"");
        if (p) { p = strchr(p + 4, ':'); if (p) day = atoi(p + 1); }
        p = strstr(body, "\"minutes\"");
        if (p) { p = strchr(p + 9, ':'); if (p) minutes = atoi(p + 1); }
    }

    char json[256];
    size_t pos = 0;
    Result rc = pctl_set_day(day, minutes);

    json_open_obj(json, &pos, sizeof(json));
    json_write_int(json, &pos, sizeof(json), "rc", (int)rc);
    json_sep(json, &pos, sizeof(json));
    json_write_str(json, &pos, sizeof(json), "message",
                   R_SUCCEEDED(rc) ? "OK" : "Failed");
    json_close_obj(json, &pos, sizeof(json));
    json[pos] = '\0';

    http_send_response(fd, 200, "OK", "application/json", json);
}

static void handle_api_start(int fd)
{
    Result rc = pctl_start_play_timer();
    char json[256];
    size_t pos = 0;
    json_open_obj(json, &pos, sizeof(json));
    json_write_int(json, &pos, sizeof(json), "rc", (int)rc);
    json_sep(json, &pos, sizeof(json));
    json_write_str(json, &pos, sizeof(json), "message",
                   R_SUCCEEDED(rc) ? "OK" : "Failed");
    json_close_obj(json, &pos, sizeof(json));
    json[pos] = '\0';
    http_send_response(fd, 200, "OK", "application/json", json);
}

static void handle_api_stop(int fd)
{
    Result rc = pctl_stop_play_timer();
    char json[256];
    size_t pos = 0;
    json_open_obj(json, &pos, sizeof(json));
    json_write_int(json, &pos, sizeof(json), "rc", (int)rc);
    json_sep(json, &pos, sizeof(json));
    json_write_str(json, &pos, sizeof(json), "message",
                   R_SUCCEEDED(rc) ? "OK" : "Failed");
    json_close_obj(json, &pos, sizeof(json));
    json[pos] = '\0';
    http_send_response(fd, 200, "OK", "application/json", json);
}

static void handle_api_reset(int fd)
{
    Result rc = pctl_reset_play_time();
    char json[256];
    size_t pos = 0;
    json_open_obj(json, &pos, sizeof(json));
    json_write_int(json, &pos, sizeof(json), "rc", (int)rc);
    json_sep(json, &pos, sizeof(json));
    json_write_str(json, &pos, sizeof(json), "message",
                   R_SUCCEEDED(rc) ? "OK" : "Failed");
    json_close_obj(json, &pos, sizeof(json));
    json[pos] = '\0';
    http_send_response(fd, 200, "OK", "application/json", json);
}

static void handle_api_version(int fd)
{
    const char *json = "{\"version\":\"" VERSION_S "\"}";
    http_send_response(fd, 200, "OK", "application/json", json);
}

// ---- Request Router ----

static void handle_client(int client_fd)
{
    char reqbuf[8192];
    int n = http_read_request(client_fd, reqbuf, sizeof(reqbuf));
    if (n <= 0) {
        close(client_fd);
        return;
    }

    // Parse method and path
    char method[16] = {0};
    char path[256] = {0};
    sscanf(reqbuf, "%15s %255s", method, path);

    // Handle CORS preflight
    if (strcmp(method, "OPTIONS") == 0) {
        http_send_response(client_fd, 204, "No Content", "text/plain", NULL);
        close(client_fd);
        return;
    }

    // Find body (after \r\n\r\n)
    char *body = NULL;
    char *hdr_end = strstr(reqbuf, "\r\n\r\n");
    if (hdr_end) body = hdr_end + 4;

    // Route
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        http_send_response(client_fd, 200, "OK",
                           "text/html; charset=utf-8", WEB_UI_HTML);
    }
    else if (strcmp(path, "/api/status") == 0) {
        handle_api_status(client_fd);
    }
    else if (strcmp(path, "/api/settings") == 0) {
        handle_api_settings(client_fd);
    }
    else if (strcmp(path, "/api/set") == 0 && strcmp(method, "POST") == 0) {
        handle_api_set(client_fd, body);
    }
    else if (strcmp(path, "/api/set_day") == 0 && strcmp(method, "POST") == 0) {
        handle_api_set_day(client_fd, body);
    }
    else if (strcmp(path, "/api/start") == 0 && strcmp(method, "POST") == 0) {
        handle_api_start(client_fd);
    }
    else if (strcmp(path, "/api/stop") == 0 && strcmp(method, "POST") == 0) {
        handle_api_stop(client_fd);
    }
    else if (strcmp(path, "/api/reset") == 0 && strcmp(method, "POST") == 0) {
        handle_api_reset(client_fd);
    }
    else if (strcmp(path, "/api/version") == 0) {
        handle_api_version(client_fd);
    }
    else {
        http_send_response(client_fd, 404, "Not Found", "application/json",
                           "{\"error\":\"not found\"}");
    }

    close(client_fd);
}

// ---- HTTP Server Thread ----

static void *http_server_thread(void *arg)
{
    (void)arg;

    s_http_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_http_fd < 0) {
        printf("[HTTP] Failed to create socket\n");
        consoleUpdate(NULL);
        return NULL;
    }

    int opt = 1;
    setsockopt(s_http_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(HTTP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s_http_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[HTTP] Failed to bind port %d\n", HTTP_PORT);
        consoleUpdate(NULL);
        close(s_http_fd);
        s_http_fd = -1;
        return NULL;
    }

    if (listen(s_http_fd, 4) < 0) {
        printf("[HTTP] Failed to listen\n");
        consoleUpdate(NULL);
        close(s_http_fd);
        s_http_fd = -1;
        return NULL;
    }

    printf("[HTTP] Server started on port %d\n", HTTP_PORT);
    consoleUpdate(NULL);

    while (s_http_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        // Use select for non-blocking check
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(s_http_fd, &readfds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(s_http_fd + 1, &readfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        int client_fd = accept(s_http_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0) {
            handle_client(client_fd);
        }
    }

    if (s_http_fd >= 0) {
        close(s_http_fd);
        s_http_fd = -1;
    }

    printf("[HTTP] Server stopped\n");
    consoleUpdate(NULL);
    return NULL;
}

// ---- Public API ----

void http_server_start(void)
{
    if (s_http_running) return;
    s_http_running = true;
    pthread_create(&s_http_thread, NULL, http_server_thread, NULL);
    pthread_detach(s_http_thread);
}

void http_server_stop(void)
{
    s_http_running = false;
    // Wake up select() by closing the socket
    if (s_http_fd >= 0) {
        shutdown(s_http_fd, SHUT_RDWR);
    }
}

bool http_server_is_running(void)
{
    return s_http_running;
}
