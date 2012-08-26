 /*
  * DRDDR-BP.h
  * 
  * Manages the list of breakpoints, and offers an interface to dynamically set breakpoints.
  */
 
void bp_init (size_t* addr_list, int addr_length);
bool bp_check (size_t address);
bool bp_remove (size_t address);
bool bp_add (int choice);
void bp_clean (void);
bool bp_inc_trapped (void);
void bp_dec_trapped (void);
void bp_monitor (char* s, size_t limit, size_t *cur_pos);
