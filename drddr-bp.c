#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/delay.h>
#include "drddr-bp.h"
#include "drddr.h"
#include "drddr-utils.h"

/*
 * Constants
 */
const char INT3 = 0xCC;

/*
 * Type definitions
 */
typedef enum {
    UNUSED,
    USING,
    TRIGGERED,
} bp_state_t;

/*
 * Global Variables
 */

size_t bp_addr[N_ADDRESS];
bp_state_t bp_state[N_ADDRESS];
char bp_backup[N_ADDRESS];
int bp_count;
int bp_trap_count;
bool bp_stopping;

static DEFINE_SPINLOCK(bp_lock_drddr);
unsigned long bp_lock_flags;

/*
 * Internal functions
 */

void bp_lock (void)
{
    spin_lock_irqsave(&bp_lock_drddr, bp_lock_flags);
}

void bp_unlock (void)
{
    spin_unlock_irqrestore(&bp_lock_drddr, bp_lock_flags);
}

int bp_usage_count (void)
{
    int re = 0, i;
    
    for (i = 0; i < bp_count; i ++)
        if (bp_state[i] == USING)
            re ++;
    return re;
}

/*
 * Initialize the list of breakpoints.
 */
void bp_init (size_t* addr_list, int addr_length)
{
    int i;
   
    bp_count = addr_length;
    for (i = 0; i < addr_length; i ++)
    {
//        printk ("bp init: %016lx\n", (unsigned long)addr_list[i]);
        bp_addr[i] = addr_list[i];
        drddr_memory_rw (addr_list[i]);
        bp_state[i] = UNUSED;
    }

    bp_trap_count = 0;
    bp_stopping = false;
}

/*
 * Return true if the address is a potential breakpoint address...
 */
bool bp_check (size_t address)
{
    int i;
    
    bp_lock ();
    for (i = 0; i < bp_count; i ++)
    {
        if (bp_addr[i] == address)
        {
            bp_unlock();
            return true;
        }
    }
    bp_unlock();
    return false;
}

bool _bp_remove (size_t address)
{
    int i;

    for (i = 0; i < bp_count; i ++)
        if (bp_addr[i] == address && bp_state[i] == USING)
        {
            *(char *)bp_addr[i] = bp_backup[i];
            bp_state[i] = UNUSED;
            return true;
        }
    return false;
}

bool bp_remove (size_t address)
{
    bool re;
    
    bp_lock ();

    re = _bp_remove (address);

    bp_unlock();        
    return re;
}

bool bp_add (int choice)
{
    int i;

    if (bp_stopping)
        return false;

    bp_lock ();

    printk ("bp_add: cur %d, total %d\n", bp_usage_count(), bp_count);

    if (choice == -1)
    {
        if (bp_usage_count() == bp_count)
        {
            bp_unlock();
            return false;
        }
    
        do
        {
            i = random32() % bp_count;
        } while (bp_state[i] == USING);
    
/*
        printk ("%02x %02x %02x %02x %02x %02x %02x %02x\n", 
                (unsigned int)(unsigned char)(*(char *)bp_addr[i]), 
                (unsigned int)(unsigned char)(*(((char *)bp_addr[i])+1)), 
                (unsigned int)(unsigned char)(*(((char *)bp_addr[i])+2)), 
                (unsigned int)(unsigned char)(*(((char *)bp_addr[i])+3)), 
                (unsigned int)(unsigned char)(*(((char *)bp_addr[i])+4)), 
                (unsigned int)(unsigned char)(*(((char *)bp_addr[i])+5)), 
                (unsigned int)(unsigned char)(*(((char *)bp_addr[i])+6)), 
                (unsigned int)(unsigned char)(*(((char *)bp_addr[i])+7)) 
                );
*/
    }
    else
    {
        i = choice;

        if (i >= bp_count)
        {
            bp_unlock();
            return false;
        }

        if (bp_state[i] == USING)
        {
            bp_unlock();
            return false;
        }
    }

    printk ("Modifying %016lx\n", (unsigned long)(bp_addr[i]));

    bp_state[i] = USING;
    bp_backup[i] = *(char *)bp_addr[i];
    *(char *)bp_addr[i] = INT3;
    bp_unlock ();
    
    return true;
}

bool bp_inc_trapped (void)
{
    bp_lock ();
    if (bp_stopping)
    {
        bp_unlock ();
        return false;
    }
    bp_trap_count ++;
    bp_unlock ();
    return true;
}

void bp_dec_trapped (void)
{
    bp_lock ();
    bp_trap_count --;
    bp_unlock ();
}

void bp_clean (void)
{
    int i;

    bp_lock ();
    for (i = 0; i < bp_count; i ++)
        if (bp_state[i] == USING)
            _bp_remove(bp_addr[i]);
    bp_stopping = true;
    bp_unlock ();

    while (bp_trap_count)
    {
        msleep(100);
        printk ("bp_trap_count = %d\n", bp_trap_count);
    }
}

void bp_monitor (char *s, size_t limit, size_t *cur_pos)
{
#define PRINT(...) do{(*cur_pos)+=sprintf(s+*cur_pos, __VA_ARGS__);} while(0)

    int i;

    printk ("cur_pos is %d\n", (int)*cur_pos);

    bp_lock ();

    PRINT ("Breakpoint status:\n");
    for (i = 0; i < bp_count; i ++)
    {
        PRINT ("    Breakpoint%5d at %016lx: ", i, (long)bp_addr[i]);
        switch (bp_state[i])
        {
            case UNUSED:
                PRINT ("UNUSED\n");
                break;
            case USING:
                PRINT ("USING\n");
                break;
            case TRIGGERED:
                PRINT ("TRIGGERED\n");
                break;
        }
    }

    bp_unlock ();
    
    printk ("cur_pos is %d\n", (int)*cur_pos);
#undef PRINT
}
