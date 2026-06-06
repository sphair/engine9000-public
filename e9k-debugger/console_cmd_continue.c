/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "console_cmd_internal.h"

#include "debugger.h"
#include "libretro_host.h"
#include "machine.h"

int
console_cmd_continue_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    if (libretro_host_debugResume()) {
        machine_setRunning(&debugger.machine, 1);
        e9ui_setFocus(&e9ui->ctx, NULL);
        return 1;
    }
    debug_error("continue: resume failed");
    return 0;
}
