/*
 * Copyright (c) 2026 Sam Kwort
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CSSE4011_KITE_SHELL_H_
#define CSSE4011_KITE_SHELL_H_

#include <zephyr/shell/shell.h>

/* Add a subcommand to the 'kite' root command */
#define KITE_CMD_ARG_ADD(_syntax, _subcmd, _help, _handler, _mand, _opt)       \
    SHELL_SUBCMD_ADD((kite), _syntax, _subcmd, _help, _handler, _mand, _opt)

#define KITE_CMD_ADD(_syntax, _subcmd, _help, _handler)                        \
    KITE_CMD_ARG_ADD(_syntax, _subcmd, _help, _handler, 0, 0)

#endif /* CSSE4011_KITE_SHELL_H_ */
