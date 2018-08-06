#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <syslog.h>

#include "disk-analysis-log.h"

#define BUF_LEN 2048
#define MAX_LEN (BUF_LEN - 1)

static int msg_threshold;

void print_bin(char *buffer, uint8_t val, int n)
{
	int i;
	for (i = 0; i < n; i++) {
		if ((val >> (n-i-1)) & 0x1) {
			buffer[i] = '1';
		} else {
			buffer[i] = '0';
		}
	}
}

static void printf_log_func(int level, const char *msg)
{
	/* debug < info < err */
	if (level <= msg_threshold) {
		printf("%s\n", msg);
	}
}

//static void syslog_log_func(int level, const char *msg)
//{
//	syslog(level, "%s\n", msg);
//}

typedef void (*log_func)(int level, const char *msg);
static log_func _log_func = printf_log_func;

//void log_init(const char *id, int debug)
//{
//	_log_func = syslog_log_func;
//	msg_threshold = debug;
//	openlog(id, LOG_PERROR|LOG_PID|LOG_NDELAY, LOG_DAEMON);
//	if (debug) {
//		setlogmask(LOG_UPTO(LOG_INFO));
//	}
//	else {
//		setlogmask(LOG_UPTO(LOG_ERR));
//	}
//	setvbuf(stdout, NULL, _IONBF, 0);
//}
void log_init(const char *id, int debug)
{
	id = id;
	_log_func = printf_log_func;
	msg_threshold = debug;
}

char *_log_msg_fmt(const char *fmt, ...)
{
	va_list args;
	char *msg = malloc(BUF_LEN);
	if (msg) {
		va_start(args, fmt);
		vsnprintf(msg, MAX_LEN, fmt, args);
		va_end(args);
	}
	return msg;
}

int _log_msg(const char *file, const char * func, int line, char *msg)
{
	char buf[BUF_LEN];
	buf[0] = '\0';

	if (LOG_INFO > msg_threshold) {
		snprintf(buf, MAX_LEN, "[%16.16s: %04u,%.16s] ", file, line, func);
	}

	int len = strlen(buf);
	snprintf(buf+len, MAX_LEN-len, "%s", msg);
	if (_log_func) {
		_log_func(LOG_INFO, buf);
	}
	free(msg);
	return 0;
}

int _log_dbg(const char *file, const char * func, int line, char *msg)
{
	char buf[BUF_LEN];
	snprintf(buf, MAX_LEN, "[%16.16s: %04u,%.16s] %s", file, line, func, msg);
	if (_log_func) {
		_log_func(LOG_DEBUG, buf);
	}
	free(msg);
	return 0;
}

int _log_err(const char *file, const char * func, int line, char *msg)
{
	char buf[BUF_LEN];
	buf[0] = '\0';

	if (LOG_INFO > msg_threshold) {
		snprintf(buf, MAX_LEN, "[%16.16s: %04u,%.16s] ", file, line, func);
	}

	int len = strlen(buf);
	snprintf(buf+len, MAX_LEN-len, "%s", msg);
	if (_log_func) {
		_log_func(LOG_ERR, buf);
	}
	free(msg);
	return 0;
}

