#include "drddr-mon.h"
#include "drddr-wp.h"
#include "drddr-bp.h"

char mon_buf[MON_BUF_SIZE];

void mon_update (void)
{
    printk ("mon_update() called\n");

    size_t cur = 0;

    bp_monitor (mon_buf, MON_BUF_SIZE, &cur);
    wp_monitor (mon_buf, MON_BUF_SIZE, &cur);
    mon_buf[cur] = '\0';
}
