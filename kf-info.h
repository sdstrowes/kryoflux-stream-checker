#ifndef __KFINFO_H__
#define __KFINFO_H__

#include <stdint.h>

#define BUF_SIZE 64

struct kf_info {
	char name[BUF_SIZE];
	char version[BUF_SIZE];
	char date[BUF_SIZE];
	char time[BUF_SIZE];
	char host_date[BUF_SIZE];
	char host_time[BUF_SIZE];
	char hwid[BUF_SIZE];
	char hwrv[BUF_SIZE];
	double sck;
	double ick;
};

void log_kf_info(struct kf_info *info);

int parse_kf_info(FILE *f, uint16_t size, struct kf_info *info);


#endif

