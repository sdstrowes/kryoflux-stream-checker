#include <errno.h>
#include <stdio.h>

#include "disk-analysis-log.h"
#include "kf-info.h"


void log_kf_info(struct kf_info *info)
{
	log_dbg("[INFO] name: %s version: %s hwid: %s, hwrv: %s, sck: %.9f, ick: %.9f",
		info->name,
		info->version,
		info->hwid,
		info->hwrv,
		info->sck,
		info->ick);

	log_dbg("[INFO] host_date: %s, %s", info->host_date, info->host_time);
	log_dbg("[INFO] date: %s, %s", info->date, info->time);
}


int parse_kf_info(FILE *f, uint16_t size, struct kf_info *info)
{
	char *str;
	int rc;

	str = (char*)malloc(size);
	rc = fread(str, size, 1, f);
	if (rc < 1) {
		return 1;
	}

	log_dbg("kfinfo read:%s", str);

	char *saveptr1, *saveptr2;
	char *str1, *token;

	char *delim = ",";
	char *subdelim = "=";

	int j;
	for (j = 1, str1 = str; ; j++, str1 = NULL) {
		token = strtok_r(str1, delim, &saveptr1);
		if (token == NULL) {
			break;
		}

		char *key;
		char *value;

		key = strtok_r(token, subdelim, &saveptr2);
		if (key == NULL) {
			continue;
		}

		value = strtok_r(NULL, subdelim, &saveptr2);
		if (value == NULL) {
			continue;
		}

		if (! strcmp(key, "name")) {
			strncpy(info->name, value, sizeof(info->name));
		}
		else if (! strcmp(key, " version")) {
			strncpy(info->version, value, sizeof(info->version));
		}
		else if (! strcmp(key, " date")) {
			strncpy(info->date, value, sizeof(info->date));
		}
		else if (! strcmp(key, " time")) {
			strncpy(info->time, value, sizeof(info->time));
		}
		else if (! strcmp(key, "host_date")) {
			strncpy(info->host_date, value, sizeof(info->host_date));
		}
		else if (! strcmp(key, " host_time")) {
			strncpy(info->host_time, value, sizeof(info->host_time));
		}
		else if (! strcmp(key, " hwid")) {
			strncpy(info->hwid, value, sizeof(info->hwid));
		}
		else if (! strcmp(key, " hwrv")) {
			strncpy(info->hwrv, value, sizeof(info->hwrv));
		}
		else if (! strcmp(key, " sck")) {
			info->sck = strtod(value, NULL);
			if (errno != 0) {
				perror("strtod() cannot parse sck value");
				exit(EXIT_FAILURE);
			}
		}
		else if (! strcmp(key, " ick")) {
			info->ick = strtod(value, NULL);
			if (errno != 0) {
				perror("strtod() cannot parse ick value");
				exit(EXIT_FAILURE);
			}
		}
	}

	free(str);

	return 1;
}

