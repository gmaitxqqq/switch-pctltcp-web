// pctltcp-web — Switch Parental Control Web Server (NRO)
// =============================================================
// Pure .nro homebrew app with:
//   - Console UI for on-device status display
//   - Background TCP server (port 6000) for PC client compatibility
//   - Background HTTP server (port 8080) for mobile web UI
//   - pctl IPC for parental control play timer operations
//
// Mobile Web UI: Open http://<Switch-IP>:8080 in any browser
// PC Client:     Connect to <Switch-IP>:6000 via TCP
//
// Based on switch-pctltcp-nro v1.5.0
// =============================================================

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "tcp_server.h"
#include "http_server.h"
#include "pctl_handler.h"

// ---- Constants ----
#define PT_DAY_NOLIMIT 0xFFFFu

// ---- Pad Input (new libnx API) ----
static PadState g_pad;

static void initPad(void)
{
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&g_pad);
}

static u64 padGetDown(void)
{
    padUpdate(&g_pad);
    return padGetButtonsDown(&g_pad);
}

// ---- UI Helpers ----
static void printSeparator(void)
{
    printf("  ========================================\n");
}

static void consoleFlush(void)
{
    consoleUpdate(NULL);
}

static void waitForKey(void)
{
    printf("\n   Press any key to continue...\n");
    consoleFlush();
    while (appletMainLoop()) {
        u64 k = padGetDown();
        if (k) break;
        consoleFlush();
        svcSleepThread(10000000ULL);
    }
}

// ---- Get Switch IP Address ----
static void getIpAddressStr(char *buf, size_t buf_size)
{
    buf[0] = '\0';

    /* Method 1: tcp_server_get_ip() — most reliable (getsockname) */
    const char *ip = tcp_server_get_ip();
    if (ip && ip[0] != '\0' && strcmp(ip, "0.0.0.0") != 0) {
        snprintf(buf, buf_size, "%s", ip);
        return;
    }

    /* Method 2: nifm fallback */
    static bool s_nifm_tried = false;
    if (!s_nifm_tried) {
        nifmInitialize(NifmServiceType_User);
        s_nifm_tried = true;
    }

    u32 ipaddr = 0;
    Result rc = nifmGetCurrentIpAddress(&ipaddr);
    if (R_SUCCEEDED(rc) && ipaddr != 0) {
        struct in_addr addr;
        addr.s_addr = ipaddr;
        snprintf(buf, buf_size, "%s", inet_ntoa(addr));
        return;
    }

    snprintf(buf, buf_size, "N/A");
}

// ---- Status Screen ----
static const char *day_names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

static void showStatus(void)
{
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Switch Parental Control Web\n");
    printf("   v" VERSION_S " | by gmaitxqqq\n");
    printSeparator();
    printf("\n");
    consoleFlush();

    char ip_str[64];
    getIpAddressStr(ip_str, sizeof(ip_str));

    printf("   TCP Server:  %s:%d (PC client)\n", ip_str, TCP_PORT);
    printf("   HTTP Server: %s:%d (Web UI)\n", ip_str, HTTP_PORT);
    printf("   Clients:     %u connected\n\n", tcp_server_client_count());
    consoleFlush();

    bool enabled = false, restricted = false;
    u64 remaining_ns = 0;

    if (R_SUCCEEDED(pctl_is_enabled(&enabled)))
        printf("   Timer:       %s\n", enabled ? "Running" : "Stopped");
    else
        printf("   Timer:       (query failed)\n");

    if (R_SUCCEEDED(pctl_is_restricted(&restricted)))
        printf("   Restricted:  %s\n", restricted ? "YES (time up)" : "No");
    else
        printf("   Restricted:  (query failed)\n");

    if (R_SUCCEEDED(pctl_get_remaining_time(&remaining_ns)) && remaining_ns > 0) {
        u64 rem_min = remaining_ns / 60000000000ULL;
        printf("   Remaining:   %llu min\n", (unsigned long long)rem_min);
    }

    printf("\n   Daily Time Limits (minutes):\n");
    consoleFlush();

    PlayTimerSettings settings;
    if (R_SUCCEEDED(pctl_get_settings(&settings))) {
        for (int i = 0; i < 7; i++) {
            u16 m = settings.raw[PCTL_DAY_MINUTES_OFFSET(i)];
            if (m == PT_DAY_NOLIMIT)
                printf("     %s: No limit\n", day_names[i]);
            else
                printf("     %s: %u min (%uh %um)\n", day_names[i],
                       m, m / 60, m % 60);
        }
    } else {
        printf("     (Could not read timer settings)\n");
    }

    waitForKey();
}

// ---- Main Loop ----
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    // Initialize services
    socketInitializeDefault();
    pctl_init();

    // Console
    consoleInit(NULL);
    initPad();

    // Start TCP server (port 6000) for PC client compatibility
    tcp_server_start();

    // Start HTTP server (port 8080) for mobile web UI
    http_server_start();

    // Main menu
    int cursor = 0;
    const int menu_count = 2;
    const char *menu_items[] = {
        "View Status",
        "Exit",
    };

    bool done = false;
    while (appletMainLoop() && !done) {
        u64 k = padGetDown();

        if (k & HidNpadButton_Up) {
            cursor = (cursor - 1 + menu_count) % menu_count;
        } else if (k & HidNpadButton_Down) {
            cursor = (cursor + 1) % menu_count;
        } else if (k & HidNpadButton_A) {
            switch (cursor) {
                case 0: showStatus(); break;
                case 1: done = true; break;
            }
        }

        // Render menu (every frame to show live IP)
        consoleClear();
        printf("\n");
        printSeparator();
        printf("   Switch Parental Control Web\n");
        printf("   v" VERSION_S " | by gmaitxqqq\n");
        printSeparator();
        printf("\n");

        char ip_str[64];
        getIpAddressStr(ip_str, sizeof(ip_str));
        printf("   Web UI:  http://%s:%d\n", ip_str, HTTP_PORT);
        printf("   TCP:     %s:%d\n", ip_str, TCP_PORT);
        printf("   Clients: %u\n\n", tcp_server_client_count());
        consoleFlush();

        printf("   Menu:\n");
        for (int i = 0; i < menu_count; i++) {
            printf("   %s %s\n", (i == cursor) ? ">" : " ", menu_items[i]);
        }
        printf("\n   Open the Web URL on your phone!\n");
        consoleFlush();

        svcSleepThread(16000000ULL); // ~16fps
    }

    // Cleanup
    http_server_stop();
    tcp_server_stop();
    pctl_exit();
    socketExit();
    consoleExit(NULL);

    return 0;
}
