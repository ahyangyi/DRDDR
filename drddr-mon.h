#ifndef DRDDR_MON_H
#define DRDDR_MON_H

#define MON_BUF_SIZE 65536

extern char mon_buf[MON_BUF_SIZE];

void mon_update(void);

#endif
