// pctltcp-web — pctl Handler Header
// ====================================
// Parental Control service (pctl) IPC wrapper.
// Provides functions to read/write play timer settings.
//
// PlayTimerSettings layout: u16[34]
//   raw[0] = header (0x0101)
//   raw[1..6] = reserved
//   raw[7+4n+0..1] = per-day block (day n, n=0..6)
//   raw[7+4n+2] = daily limit in minutes (0xFFFF = no limit)
//   raw[7+4n+3] = reserved
//
// Switch weekday: 0=Sunday, 1=Monday, ..., 6=Saturday
// ====================================

#ifndef PCTL_HANDLER_H
#define PCTL_HANDLER_H

#include <switch.h>
#include <stdbool.h>

// ---- Constants ----
#define PCTL_SETTINGS_SIZE 34

#define PCTL_DAY_BLOCK(day)     ((day) * 4)
#define PCTL_DAY_MINUTES(day)   (7 + PCTL_DAY_BLOCK(day) + 2)
#define PCTL_DAY_MINUTES_OFFSET(day) PCTL_DAY_MINUTES(day)

#define PCTL_NOLIMIT 0xFFFFu

// ---- Types ----
typedef struct {
    u16 raw[PCTL_SETTINGS_SIZE];
} PlayTimerSettings;

// ---- Init/Exit ----
void pctl_init(void);
void pctl_exit(void);
bool pctl_is_initialized(void);

// ---- Timer Control ----
Result pctl_start_play_timer(void);
Result pctl_stop_play_timer(void);
Result pctl_reset_play_time(void);

// ---- Status Query ----
Result pctl_is_enabled(bool *out_enabled);
Result pctl_is_restricted(bool *out_restricted);
Result pctl_get_remaining_time(u64 *out_nanoseconds);

// ---- Settings ----
Result pctl_get_settings(PlayTimerSettings *out);
Result pctl_set_settings(const PlayTimerSettings *settings);
Result pctl_set_uniform(int minutes);        // All 7 days same
Result pctl_set_day(int day, int minutes);   // One specific day (0=Sun..6=Sat)

#endif // PCTL_HANDLER_H
