// pctltcp-web - Switch Parental Control Web Server
// =============================================================
// NRO homebrew with dual servers:
//   - TCP server (port 6000) for PC client
//   - HTTP server (port 8080) with embedded mobile Web UI
//
// Uses pctl IPC for parental control play timer operations.
// pctl + sockets initialized in main(), servers in pthreads.
//
// Compatible: Atmosphere CFW + fw 22.1.0
// =============================================================

#include <switch.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

#include "tcp_server.h"
#include "pctl_handler.h"
#include "http_server.h"

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

static void getIpAddressStr(char *buf, size_t buf_size)
{
    buf[0] = '\0';

    /* Method 1: tcp_server_get_ip() */
    const char *ip = tcp_server_get_ip();
    if (ip && ip[0] != '\0' && strcmp(ip, "0.0.0.0") != 0) {
        snprintf(buf, buf_size, "%s", ip);
        return;
    }

    /* Method 2: nifm */
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

// ---- Main ----

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    consoleInit(NULL);
    initPad();

    // Splash screen
    consoleClear();
    printf("\n");
    printSeparator();
    printf("   Switch Parental Control\n");
    printf("   Web Server - NRO Edition\n");
    printf("   v" VERSION_S " | by gmaitxqqq\n");
    printSeparator();
    printf("\n");
    printf("   Initializing...\n");
    consoleFlush();

    // Initialize pctl service
    Result pctl_rc = pctl_init();
    if (R_FAILED(pctl_rc)) {
        printf("   pctl service: FAILED 0x%08X\n", (unsigned)pctl_rc);
        printf("   (CFW required for pctl features)\n");
        consoleFlush();
    } else {
        printf("   pctl service: OK\n");
        consoleFlush();
    }

    // Initialize socket driver
    Result sock_rc = socketInitializeDefault();
    if (R_FAILED(sock_rc)) {
        printf("   socket init: FAILED 0x%08X\n", (unsigned)sock_rc);
        printf("   Press any key to exit...\n");
        consoleFlush();
        while (appletMainLoop()) {
            if (padGetDown()) break;
            consoleFlush();
            svcSleepThread(10000000ULL);
        }
        consoleExit(NULL);
        return 1;
    }
    printf("   Socket driver: OK\n");
    consoleFlush();

    // Initialize nifm for IP
    Result nifm_rc = nifmInitialize(NifmServiceType_User);
    printf("   Network: %s\n", R_SUCCEEDED(nifm_rc) ? "OK" : "N/A");
    consoleFlush();

    // Start TCP server
    Result tcp_rc = tcp_server_start();
    if (R_FAILED(tcp_rc)) {
        printf("   TCP server: FAILED 0x%08X\n", (unsigned)tcp_rc);
    } else {
        printf("   TCP server: OK (port %d)\n", TCP_PORT);
    }
    consoleFlush();

    // Start HTTP server
    http_server_start();
    printf("   HTTP server: %s (port %d)\n",
        http_server_is_running() ? "OK" : "FAILED", HTTP_PORT);
    consoleFlush();

    // Get IP
    char ip_str[64];
    getIpAddressStr(ip_str, sizeof(ip_str));
    printf("   IP Address: %s\n", ip_str);
    consoleFlush();

    // Ready
    printf("\n");
    printSeparator();
    printf("   READY\n");
    printSeparator();
    printf("\n");
    if (http_server_is_running())
        printf("   Web UI:  http://%s:%d\n", ip_str, HTTP_PORT);
    if (R_SUCCEEDED(tcp_rc))
        printf("   TCP:     %s:%d\n", ip_str, TCP_PORT);
    printf("\n");
    printf("   Open the URL on your phone!\n");
    printf("\n");
    printf("   A: Refresh display\n");
    printf("   B: Exit\n");
    printSeparator();
    consoleFlush();

    svcSleepThread(1000000000ULL);  // 1 sec splash

    // Main loop
    while (appletMainLoop()) {
        u64 k = padGetDown();

        if (k & HidNpadButton_B) break;

        if (k & HidNpadButton_A) {
            // Refresh display
            char refresh_ip[64];
            getIpAddressStr(refresh_ip, sizeof(refresh_ip));

            consoleClear();
            printf("\n");
            printSeparator();
            printf("   Switch Parental Control Web\n");
            printf("   v" VERSION_S " | Clients: %u\n", tcp_server_client_count());
            printSeparator();
            printf("\n");
            printf("   IP: %s\n", refresh_ip);
            printf("   Web UI:  http://%s:%d\n", refresh_ip, HTTP_PORT);
            printf("   TCP:     %s:%d\n", refresh_ip, TCP_PORT);
            printf("\n");

            if (R_SUCCEEDED(pctl_rc)) {
                bool enabled = false, restricted = false;
                u64 remaining_ns = 0;

                printf("   Timer:    %s\n",
                    (pctl_is_enabled(&enabled) == 0 && enabled) ? "Running" : "Stopped");

                if (pctl_get_remaining_time(&remaining_ns) == 0 && remaining_ns > 0) {
                    printf("   Remain:   %llu min\n", (unsigned long long)NS_TO_MINUTES(remaining_ns));
                }

                if (pctl_is_restricted(&restricted) == 0 && restricted) {
                    printf("   STATUS:   ** BLOCKED **\n");
                }
            } else {
                printf("   pctl: not available\n");
            }

            printf("\n");
            printf("   A: Refresh   B: Exit\n");
            printSeparator();
            consoleFlush();
        }

        svcSleepThread(50000000ULL);
    }

    // Cleanup
    printf("\n   Shutting down...\n");
    consoleFlush();

    http_server_stop();
    tcp_server_stop();

    if (R_SUCCEEDED(nifm_rc)) nifmExit();
    if (R_SUCCEEDED(pctl_rc)) pctl_exit();
    socketExit();
    consoleExit(NULL);
    return 0;
}
