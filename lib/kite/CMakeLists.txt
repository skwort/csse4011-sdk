# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(kite)

zephyr_library_sources_ifdef(CONFIG_CSSE4011_SHELL kite_shell.c)

add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_LED led)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_GPIO gpio)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_PWM pwm)
add_subdirectory_ifdef(CONFIG_CSSE4011_SHELL_UART uart)
