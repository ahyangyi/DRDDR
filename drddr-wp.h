#include <linux/ptrace.h>

struct watchpoint_id {
    unsigned int dr:2;
    int num:30;
};

#define watchpoint_id_set_invalid(id) do { (id)->num = -1; } while (0)
#define watchpoint_id_is_valid(id) ((id)->num >= 0)

struct watchpoint_id wp_add (size_t data_addr, int data_length, bool is_write, struct pt_regs* regs);
void wp_remove (struct watchpoint_id wp_no);
void wp_init (void);
void wp_clean (void);
struct watchpoint_id wp_query (void);
void wp_report (struct watchpoint_id wp);

void wp_monitor (char *s, size_t limit, size_t *cur_pos);
