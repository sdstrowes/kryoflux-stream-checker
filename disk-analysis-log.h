#ifndef _DISK_ANALYSIS_LOG_H_
#define _DISK_ANALYSIS_LOG_H_

#include <stdlib.h>
#include <string.h>


#define KNRM  "\x1B[1;0m"
#define KRED  "\x1B[1;31m"
#define KGRN  "\x1B[1;32m"
#define KYEL  "\x1B[1;33m"
#define KBLU  "\x1B[1;34m"
#define KMAG  "\x1B[1;35m"
#define KCYN  "\x1B[1;36m"
#define KWHT  "\x1B[1;37m"


#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

void print_bin(char *buffer, uint8_t val, int n);

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

