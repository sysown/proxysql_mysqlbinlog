/* Copyright (c) 2006, 2010, Oracle and/or its affiliates
 * Copyright (c) 2011, Monty Program Ab
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

#include "tap.h"

#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#ifdef __APPLE__
typedef unsigned long ulong;
#endif

static TEST_DATA g_test = {NO_PLAN, 0, 0, ""};

#define tapout stdout

volatile int tap_log_us = 1;

size_t get_fmt_time(char* tm_buf, size_t len, bool us) {
	struct timeval tv {};
	gettimeofday(&tv, NULL);

	struct tm tm_info {};
	localtime_r(&tv.tv_sec, &tm_info);

	size_t rc = strftime(tm_buf, len, "%Y-%m-%d %H:%M:%S", &tm_info);
	if (rc == 0)
		return rc;
	if (us)
		rc = snprintf(tm_buf + rc, len - rc, ".%06ld", (long)tv.tv_usec);
	return rc;
}

static void vemit_tap(int pass, char const* fmt, va_list ap) {
	char tm_buf[28] = {0};
	get_fmt_time(tm_buf, sizeof(tm_buf), __sync_add_and_fetch(&tap_log_us, 0));
	fprintf(tapout, "%sok %d - %s%s", pass ? "" : "not ",
	        __sync_add_and_fetch(&g_test.last, 1), tm_buf,
	        (fmt && *fmt) ? " - " : "");
	if (fmt && *fmt)
		vfprintf(tapout, fmt, ap);
	fflush(tapout);
}

static void emit_dir(const char* dir, const char* why) {
	fprintf(tapout, " # %s %s", dir, why);
	fflush(tapout);
}

static void emit_endl() {
	fprintf(tapout, "\n");
	fflush(tapout);
}

static void handle_core_signal(int signo) {
	BAIL_OUT("Signal %d thrown\n", signo);
}

void BAIL_OUT(char const* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	fprintf(tapout, "Bail out! ");
	vfprintf(tapout, fmt, ap);
	diag("%d tests planned, %d failed, %d was last executed",
	     g_test.plan, g_test.failed, g_test.last);
	emit_endl();
	va_end(ap);
	exit(255);
}

void diag(char const* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char tm_buf[28] = {0};
	get_fmt_time(tm_buf, sizeof(tm_buf), __sync_add_and_fetch(&tap_log_us, 0));
	fprintf(tapout, "# %s  ", tm_buf);
	vfprintf(tapout, fmt, ap);
	emit_endl();
	va_end(ap);
}

typedef struct signal_entry {
	int signo;
	void (*handler)(int);
} signal_entry;

static signal_entry install_signal[] = {
#ifdef SIGBUS
    {SIGBUS, handle_core_signal},
#endif
#ifdef SIGXCPU
    {SIGXCPU, handle_core_signal},
    {SIGXFSZ, handle_core_signal},
    {SIGSYS, handle_core_signal},
    {SIGTRAP, handle_core_signal},
#endif
};

void plan(int count) {
	setvbuf(tapout, 0, _IONBF, 0);

	for (size_t i = 0; i < sizeof(install_signal) / sizeof(*install_signal); ++i)
		signal(install_signal[i].signo, install_signal[i].handler);

	g_test.plan = count;
	if (count > 0) {
		fprintf(tapout, "1..%d\n", count);
		fflush(tapout);
	}
}

void skip_all(char const* reason, ...) {
	va_list ap;
	va_start(ap, reason);
	fprintf(tapout, "1..0 # skip ");
	vfprintf(tapout, reason, ap);
	fflush(tapout);
	va_end(ap);
	exit(0);
}

void ok(int pass, char const* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);

	if (!pass && *g_test.todo == '\0')
		__sync_add_and_fetch(&g_test.failed, 1);

	vemit_tap(pass, fmt, ap);
	va_end(ap);
	if (*g_test.todo != '\0')
		emit_dir("todo", g_test.todo);
	emit_endl();
}

void ok1(int const pass) {
	va_list ap;
	memset(&ap, 0, sizeof(ap));

	if (!pass && *g_test.todo == '\0')
		__sync_add_and_fetch(&g_test.failed, 1);

	vemit_tap(pass, NULL, ap);
	if (*g_test.todo != '\0')
		emit_dir("todo", g_test.todo);
	emit_endl();
}

void skip(int how_many, char const* const fmt, ...) {
	char reason[80];
	if (fmt && *fmt) {
		va_list ap;
		va_start(ap, fmt);
		vsnprintf(reason, sizeof(reason), fmt, ap);
		va_end(ap);
	} else {
		reason[0] = '\0';
	}

	while (how_many-- > 0) {
		va_list ap;
		memset((char*)&ap, 0, sizeof(ap));
		vemit_tap(1, NULL, ap);
		emit_dir("skip", reason);
		emit_endl();
	}
}

void todo_start(char const* message, ...) {
	va_list ap;
	va_start(ap, message);
	vsnprintf(g_test.todo, sizeof(g_test.todo), message, ap);
	va_end(ap);
}

void todo_end() {
	*g_test.todo = '\0';
}

int exit_status() {
	if (g_test.plan == NO_PLAN)
		plan(g_test.last);

	if (g_test.plan != g_test.last) {
		diag("%d tests planned but%s %d executed", g_test.plan,
		     (g_test.plan > g_test.last ? " only" : ""), g_test.last);
		return EXIT_FAILURE;
	}

	if (g_test.failed > 0) {
		diag("Failed %d tests!", g_test.failed);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int tests_failed() {
	return g_test.failed;
}

int tests_last() {
	return g_test.last;
}
