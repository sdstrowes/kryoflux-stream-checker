#ifndef _DISK_ANALYSIS_LOG_H_
#define _DISK_ANALYSIS_LOG_H_

#include <stdlib.h>
#include <string.h>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

void log_init(const char *id, int debug);

char *_log_msg_fmt(const char *format, ...)
	__attribute__((format(printf, 1, 2)));

int _log_msg(const char *file, const char * func, int line, char *msg);
int _log_dbg(const char *file, const char * func, int line, char *msg);
int _log_err(const char *file, const char * func, int line, char *msg);

#define log_msg(...) \
	_log_msg(__FILENAME__, __func__, __LINE__, _log_msg_fmt(__VA_ARGS__))

#define log_dbg(...) \
	_log_dbg(__FILENAME__, __func__, __LINE__, _log_msg_fmt(__VA_ARGS__))

#define log_err(...) \
	_log_err(__FILENAME__, __func__, __LINE__, _log_msg_fmt(__VA_ARGS__))

#endif

