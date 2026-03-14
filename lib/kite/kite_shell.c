/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kite_shell.h"

SHELL_SUBCMD_SET_CREATE(kite_cmds, (kite));
SHELL_CMD_REGISTER(kite, &kite_cmds, "CSSE4011 board commands", NULL);
