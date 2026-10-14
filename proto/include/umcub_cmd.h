/*
 * Text commands (UMCUB_CFG_CMD_*): a configurable command table next to SMP.
 */
#ifndef UMCUB_CMD_H
#define UMCUB_CMD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Sends text back on the transport the command came from (NUL-terminated;
 * reply(NULL) pushes out what was buffered, e.g. one UDP/CAN packet). */
typedef void (*umcub_cmd_reply_t)(const char *text);

/* Implement in boards/<board>/umcub_board.c for UMCUB_CMD_USER(id) entries.
 * `text` is the command as received. Return false to answer "?". */
bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply);

/* --- used by the transport mux ------------------------------------------- */

typedef enum {
    UMCUB_CMD_MATCH_NONE,       /* not a command and no command starts with it */
    UMCUB_CMD_MATCH_PREFIX,     /* beginning of a command: keep reading */
    UMCUB_CMD_MATCH_FULL,       /* equals a command */
} umcub_cmd_match_t;

umcub_cmd_match_t umcub_cmd_match(const char *text, size_t len);

/* Execute a complete line / packet. `in_recovery` selects how "boot app"
 * works (reset with a request vs. ending the wait window). */
void umcub_cmd_execute(const char *text, size_t len, umcub_cmd_reply_t reply, bool in_recovery);

/* Wait-window decision taken by a command: 0 none, UMCUB_CMD_STAY or
 * UMCUB_CMD_BOOT_APP. Reading clears it. */
unsigned umcub_cmd_take_decision(void);

#endif
