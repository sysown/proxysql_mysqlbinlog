/* Copyright (c) 2006, 2010, Oracle and/or its affiliates
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * Library for providing TAP support for testing C and C++ was written
 * by Mats Kindahl <mats@mysql.com>.
 *
 * --- Modifications ---
 * Trimmed for proxysql_binlog_reader integration tests.
 * Unmodified upstream:
 * - https://github.com/sysown/proxysql/tree/v3.0/test/tap/tap/tap.h
 */

#ifndef BINLOG_READER_TEST_TAP_H
#define BINLOG_READER_TEST_TAP_H

#include <unistd.h>

#define NO_PLAN (0)

typedef struct TEST_DATA {
	int plan;
	int last;
	int failed;
	char todo[128];
} TEST_DATA;

#ifdef __cplusplus
extern "C" {
#endif

extern volatile int tap_log_us;

size_t get_fmt_time(char* tm_buf, size_t len, bool us = false);

void plan(int count);

void ok(int pass, char const* fmt, ...) __attribute__((format(printf, 2, 3)));
void ok1(int const pass);

void skip(int how_many, char const* const reason, ...)
    __attribute__((format(printf, 2, 3)));

#define SKIP_BLOCK_IF(SKIP_IF_TRUE, COUNT, REASON) \
	if (SKIP_IF_TRUE) skip((COUNT), (REASON)); else

void diag(char const* fmt, ...) __attribute__((format(printf, 1, 2)));

void BAIL_OUT(char const* fmt, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

int exit_status(void);

void skip_all(char const* reason, ...)
    __attribute__((noreturn, format(printf, 1, 2)));

void todo_start(char const* message, ...)
    __attribute__((format(printf, 1, 2)));
void todo_end();

int tests_failed();
int tests_last();

#ifdef __cplusplus
}
#endif

#endif
