// pctltcp-web — TCP Server Header
// ====================================
// TCP command server (port 6000) for PC client compatibility.
// Protocol: plain text commands terminated by \n.
//
// Commands:
//   PING       -> PONG
//   VERSION    -> VERSION x.x.x
//   STATUS     -> STATUS enabled|disabled <remaining_min> <daily_limit_min> restricted|free
//   GET        -> Returns all 7 day limits (tab-separated)
//   SET <min>  -> Set all 7 days to <min> minutes (0=no limit)
//   SET_DAY <day> <min> -> Set one day (0=Sun..6=Sat)
//   START      -> Start play timer
//   STOP       -> Stop play timer
//   RESET      -> Reset today's play time
//   REMAINING  -> <minutes>
// ====================================

#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <switch.h>

#define TCP_PORT 6000
#define VERSION_S "1.0.0"

void tcp_server_start(void);
void tcp_server_stop(void);
const char *tcp_server_get_ip(void);
unsigned int tcp_server_client_count(void);

#endif // TCP_SERVER_H
