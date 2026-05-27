// pctltcp-web — TCP Server Implementation
// =========================================
// Handles TCP connections from PC client (swpc_client.py).
// Runs in a pthread, supports one client at a time.
// =========================================

#include "tcp_server.h"
#include "pctl_handler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>

// ---- State ----
static volatile bool s_tcp_running = false;
static pthread_t s_tcp_thread;
static int s_tcp_fd = -1;
static char s_ip_str[64] = "0.0.0.0";
static volatile unsigned int s_client_count = 0;

// ---- Helpers ----

static const char *day_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static int send_str(int fd, const char *str)
{
    return send(fd, str, strlen(str), 0);
}

// Read a line ending with \n
static int recv_line(int fd, char *buf, size_t bufsz)
{
    size_t total = 0;
    while (total < bufsz - 1) {
        int n = recv(fd, buf + total, 1, 0);
        if (n <= 0) return -1;
        if (buf[total] == '\n') {
            buf[total] = '\0';
            return (int)total;
        }
        total++;
    }
    buf[total] = '\0';
    return (int)total;
}

// ---- Command Handlers ----

static void cmd_ping(int fd)
{
    send_str(fd, "PONG\n");
}

static void cmd_version(int fd)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "VERSION %s\n", VERSION_S);
    send_str(fd, buf);
}

static void cmd_status(int fd)
{
    bool enabled = false, restricted = false;
    u64 remaining_ns = 0;
    int daily_limit = 0;

    pctl_is_enabled(&enabled);
    pctl_is_restricted(&restricted);

    if (R_SUCCEEDED(pctl_get_remaining_time(&remaining_ns))) {
        // Nothing extra needed
    }

    // Get today's limit
    PlayTimerSettings settings;
    if (R_SUCCEEDED(pctl_get_settings(&settings))) {
        // TODO: get actual day of week
        int dow = 0;
        TimeCalendarTime cal;
        if (R_SUCCEEDED(timeGetCurrentTime(TimeType_LocalSystemClock, NULL))) {
            timeToCalendarTimeWithMyRule(NULL, NULL, &cal);
            dow = cal.weekday;
        }
        u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(dow)];
        daily_limit = (m == 0xFFFFu) ? 0 : (int)m;
    }

    int remaining_min = (int)(remaining_ns / 60000000000ULL);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "STATUS %s %d %d %s\n",
        enabled ? "enabled" : "disabled",
        remaining_min,
        daily_limit,
        restricted ? "restricted" : "free");
    send_str(fd, buf);
}

static void cmd_get(int fd)
{
    PlayTimerSettings settings;
    if (R_FAILED(pctl_get_settings(&settings))) {
        send_str(fd, "ERROR Failed to get settings\n");
        return;
    }

    for (int i = 0; i < 7; i++) {
        u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(i)];
        char buf[64];
        if (m == 0xFFFFu)
            snprintf(buf, sizeof(buf), "%s\t65535\n", day_names[i]);
        else
            snprintf(buf, sizeof(buf), "%s\t%d\n", day_names[i], (int)m);
        send_str(fd, buf);
    }
    send_str(fd, "OK\n");
}

static void cmd_set(int fd, const char *args)
{
    int minutes = 0;
    if (args) minutes = atoi(args);

    Result rc = pctl_set_uniform(minutes);
    if (R_SUCCEEDED(rc))
        send_str(fd, "OK\n");
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERROR 0x%X\n", (unsigned)rc);
        send_str(fd, buf);
    }
}

static void cmd_set_day(int fd, const char *args)
{
    int day = 0, minutes = 0;
    if (args) sscanf(args, "%d %d", &day, &minutes);

    if (day < 0 || day > 6) {
        send_str(fd, "ERROR Invalid day (0-6)\n");
        return;
    }

    Result rc = pctl_set_day(day, minutes);
    if (R_SUCCEEDED(rc))
        send_str(fd, "OK\n");
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERROR 0x%X\n", (unsigned)rc);
        send_str(fd, buf);
    }
}

static void cmd_start(int fd)
{
    Result rc = pctl_start_play_timer();
    if (R_SUCCEEDED(rc))
        send_str(fd, "OK\n");
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERROR 0x%X\n", (unsigned)rc);
        send_str(fd, buf);
    }
}

static void cmd_stop(int fd)
{
    Result rc = pctl_stop_play_timer();
    if (R_SUCCEEDED(rc))
        send_str(fd, "OK\n");
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERROR 0x%X\n", (unsigned)rc);
        send_str(fd, buf);
    }
}

static void cmd_reset(int fd)
{
    Result rc = pctl_reset_play_time();
    if (R_SUCCEEDED(rc))
        send_str(fd, "OK\n");
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "ERROR 0x%X\n", (unsigned)rc);
        send_str(fd, buf);
    }
}

static void cmd_remaining(int fd)
{
    u64 ns = 0;
    if (R_SUCCEEDED(pctl_get_remaining_time(&ns))) {
        int min = (int)(ns / 60000000000ULL);
        char buf[64];
        snprintf(buf, sizeof(buf), "%d\n", min);
        send_str(fd, buf);
    } else {
        send_str(fd, "ERROR\n");
    }
}

// ---- Client Handler ----

static void handle_tcp_client(int client_fd, struct sockaddr_in *client_addr)
{
    s_client_count++;
    char client_ip[64];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));

    printf("[TCP] Client connected: %s\n", client_ip);
    consoleUpdate(NULL);

    // Send HELLO
    send_str(client_fd, "SWPC " VERSION_S " ready\n");

    char line[512];
    while (s_tcp_running) {
        int n = recv_line(client_fd, line, sizeof(line));
        if (n <= 0) break;

        // Parse command
        char cmd[64] = {0};
        char args[256] = {0};
        sscanf(line, "%63s %255[^\n]", cmd, args);

        if (strcmp(cmd, "PING") == 0) cmd_ping(client_fd);
        else if (strcmp(cmd, "VERSION") == 0) cmd_version(client_fd);
        else if (strcmp(cmd, "STATUS") == 0) cmd_status(client_fd);
        else if (strcmp(cmd, "GET") == 0) cmd_get(client_fd);
        else if (strcmp(cmd, "SET_DAY") == 0) cmd_set_day(client_fd, args);
        else if (strcmp(cmd, "SET") == 0) cmd_set(client_fd, args);
        else if (strcmp(cmd, "START") == 0) cmd_start(client_fd);
        else if (strcmp(cmd, "STOP") == 0) cmd_stop(client_fd);
        else if (strcmp(cmd, "RESET") == 0) cmd_reset(client_fd);
        else if (strcmp(cmd, "REMAINING") == 0) cmd_remaining(client_fd);
        else {
            send_str(client_fd, "ERROR Unknown command\n");
        }
    }

    s_client_count--;
    printf("[TCP] Client disconnected: %s\n", client_ip);
    consoleUpdate(NULL);
    close(client_fd);
}

// ---- Server Thread ----

static void *tcp_server_thread(void *arg)
{
    (void)arg;

    s_tcp_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s_tcp_fd < 0) {
        printf("[TCP] Failed to create socket\n");
        consoleUpdate(NULL);
        return NULL;
    }

    int opt = 1;
    setsockopt(s_tcp_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(TCP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s_tcp_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("[TCP] Failed to bind port %d\n", TCP_PORT);
        consoleUpdate(NULL);
        close(s_tcp_fd);
        s_tcp_fd = -1;
        return NULL;
    }

    if (listen(s_tcp_fd, 2) < 0) {
        printf("[TCP] Failed to listen\n");
        consoleUpdate(NULL);
        close(s_tcp_fd);
        s_tcp_fd = -1;
        return NULL;
    }

    // Get local IP for display
    struct sockaddr_in local_addr;
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(s_tcp_fd, (struct sockaddr *)&local_addr, &local_len) == 0) {
        // We need the actual interface IP, not 0.0.0.0
        // Use a temporary connection trick
        int tmp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (tmp_sock >= 0) {
            struct sockaddr_in dest;
            memset(&dest, 0, sizeof(dest));
            dest.sin_family = AF_INET;
            dest.sin_port = htons(80);
            dest.sin_addr.s_addr = inet_addr("8.8.8.8");
            connect(tmp_sock, (struct sockaddr *)&dest, sizeof(dest));
            getsockname(tmp_sock, (struct sockaddr *)&local_addr, &local_len);
            inet_ntop(AF_INET, &local_addr.sin_addr, s_ip_str, sizeof(s_ip_str));
            close(tmp_sock);
        }
    }

    printf("[TCP] Server started on port %d (IP: %s)\n", TCP_PORT, s_ip_str);
    consoleUpdate(NULL);

    while (s_tcp_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(s_tcp_fd, &readfds);
        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int sel = select(s_tcp_fd + 1, &readfds, NULL, NULL, &tv);
        if (sel <= 0) continue;

        int client_fd = accept(s_tcp_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd >= 0) {
            handle_tcp_client(client_fd, &client_addr);
        }
    }

    if (s_tcp_fd >= 0) {
        close(s_tcp_fd);
        s_tcp_fd = -1;
    }

    printf("[TCP] Server stopped\n");
    consoleUpdate(NULL);
    return NULL;
}

// ---- Public API ----

void tcp_server_start(void)
{
    if (s_tcp_running) return;
    s_tcp_running = true;
    pthread_create(&s_tcp_thread, NULL, tcp_server_thread, NULL);
    pthread_detach(&s_tcp_thread);
}

void tcp_server_stop(void)
{
    s_tcp_running = false;
    if (s_tcp_fd >= 0) {
        shutdown(s_tcp_fd, SHUT_RDWR);
    }
}

const char *tcp_server_get_ip(void)
{
    return s_ip_str;
}

unsigned int tcp_server_client_count(void)
{
    return s_client_count;
}
