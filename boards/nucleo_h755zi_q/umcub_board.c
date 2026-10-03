/*
 * Board hooks for NUCLEO-H755ZI-Q (compiled into the bootloader when this
 * file exists). Here: the user commands of UMCUB_CFG_CMD_TABLE.
 */
#include "umcub_cfg.h"
#include "umcub_cmd.h"
#include "umcub_port.h"

bool umcub_cmd_user(unsigned id, const char *text, umcub_cmd_reply_t reply)
{
    (void)text;
    switch (id) {
    case 1: {   /* "hello": board name + unique id */
        static const char hex[] = "0123456789abcdef";
        char line[48] = "hello from nucleo_h755zi_q uid ";
        uint8_t uid[12];
        umcub_port_uid(uid);
        char *p = line + 31;
        for (int i = 11; i >= 8; i--) {
            *p++ = hex[uid[i] >> 4];
            *p++ = hex[uid[i] & 15];
        }
        *p++ = '\r';
        *p++ = '\n';
        *p = '\0';
        reply(line);
        return true;
    }
    default:
        return false;
    }
}
