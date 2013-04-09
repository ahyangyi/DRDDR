/*
 * YY's STUPID
 * -r regex: uses this regex instead of from stdin
 * -f file: reads regex from file
 * -a all: accepts all functions
 * -O: output ORed function list
 * -i: input file
 * -o: output file
 */
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define BUF_LEN 33554432

char buf[BUF_LEN];
regex_t reg;

union parser_t
{
    struct function_t
    {
        char name[BUF_LEN];
    } func;
    struct instruction_t
    {
        char text[BUF_LEN];
        unsigned long address;
    } instr;
} state;

struct option_t
{
    enum {STDIN, CMDLINE, FILE} regex;
    enum {INSTR, FUNC} work;
    enum {DEFAULT, ALREADY} input;
} option;

bool isFunction ()
{
    int i;
    char *p;

    for (i = 0; i < 16; i ++)
    {
        if (buf[i] >= '0' && buf[i] <= '9' || buf[i] >= 'a' && buf[i] <= 'f')
            ;
        else
            return false;
    }

    if (buf[16] != ' ') return false;
    if (buf[17] != '<') return false;
    p = strchr(buf+18,'>');
    if (p == NULL) return false;
    *p = '\0';
    strcpy (state.func.name, buf+18);
    *p = '>';

    return true;
}

bool isInstruction ()
{
    int i;
    char *p;

    state.instr.address = 0;

    for (i = 0; i < 16; i ++)
    {
        if (buf[i] >= '0' && buf[i] <= '9' || buf[i] >= 'a' && buf[i] <= 'f')
        {
            state.instr.address *= 16;
            if (buf[i] <= '9')
                state.instr.address += buf[i] - '0';
            else
                state.instr.address += buf[i] - 'a' + 10;
        }
        else
            return false;
    }

    if (strlen(buf) > 40)
        strcpy (state.instr.text, buf+40);
    else
        strcpy (state.instr.text, "");

    return true;
}

const char* DangerousFunction[] =
{
    "copy_from_user",
    "copy_to_user",
    "might_fault",
    "sched",
    "sys_select",
};

const char* FrequentFunction[] =
{
    "get_page",
};

const char* MemoryAccess[] =
{
    "mov",
    "add", "adc",
    "sub", "sbb",
    "mul", "div",
    "inc", "dec",
    "and",  "or", "xor", "not",
    "neg",
    "sh", "ro", 
    "cmp","test",
};
int MCount = sizeof(MemoryAccess)/sizeof(MemoryAccess[0]);
int DCount = sizeof(DangerousFunction)/sizeof(DangerousFunction[0]);
int FCount = sizeof(FrequentFunction)/sizeof(FrequentFunction[0]);

void parse ()
{
    if (option.input != option.ALREADY)
        freopen ("vmlinux.objd", "r", stdin);
    bool relevant = false;
    regmatch_t match;
    int i;
    
    if (option.work == option_t::FUNC)
        printf ("^(");

    while (fgets (buf, BUF_LEN, stdin))
    {
        while (strlen(buf) > 0 && buf[strlen(buf)-1] < ' ')
            buf[strlen(buf)-1] = '\0';
        if (isFunction())
        {
            relevant = !regexec(&reg, state.func.name, 1, &match, 0);
            for (int i = 0; relevant && i < DCount; i ++)
                if (strstr(state.func.name, DangerousFunction[i]) != NULL)
                    relevant = false;
            for (int i = 0; relevant && i < FCount; i ++)
                if (strcmp(state.func.name, FrequentFunction[i]) == 0)
                    relevant = false;
            if (relevant)
            {
                if (option.work == option_t::INSTR)
                    fprintf(stderr,"In function %s:\n", state.func.name);
                else
                    printf("%s|", state.func.name);
            }
        }
        else if (relevant && isInstruction() && option.work == option_t::INSTR)
        {
            for (i = 0; i < MCount; i ++)
                if (strstr(state.instr.text, MemoryAccess[i]) == state.instr.text)
                {
                    char *p, *q;
                    char *sp, *bp;

                    p = strchr(state.instr.text, '(');

                    if (p == NULL) break;
                    q = strchr(p, ')');

                    if (q == NULL) q = p + strlen(p);

                    sp = strstr(p, "sp");
                    bp = strstr(p, "bp");

                    if ((sp == NULL || sp < q) && (bp == NULL || bp < q))
                    {
                        printf ("%016lx: %s\n", state.instr.address, state.instr.text);
                    }
                    
                    break;
                }
        }
    }

    if (option.work == option_t::FUNC)
        printf ("iMpoSSiBlE)$\n");
}

void parse_options (int argc, char **argv)
{
    option.regex = option.STDIN;
    option.work = option.INSTR;
    option.input = option.DEFAULT;

    for (int i = 1; i < argc;)
        if (strcmp(argv[i], "-r") == 0)
        {
            i ++;
            if (i < argc)
            {
                strcpy (buf, argv[i]);
                i ++;
                option.regex = option.CMDLINE;
            }
            else
            {
                fprintf (stderr, "-r should be followed with a regular expression.\n");
                exit(1);
            }
        }
        else if (strcmp(argv[i], "-a") == 0)
        {
            i ++;
            strcpy (buf, ".");
            option.regex = option.CMDLINE;
        }
        else if (strcmp(argv[i], "-f") == 0)
        {
            i ++;
            if (i < argc)
            {
                strcpy (buf, argv[i]);
                i ++;
                option.regex = option.FILE;
            }
            else
            {
                fprintf (stderr, "-r should be followed with a file name.\n");
                exit(1);
            }
        }
        else if (strcmp(argv[i], "-i") == 0)
        {
            i ++;
            if (i < argc)
            {
                freopen (argv[i], "r", stdin);
                i ++;
                option.input = option.ALREADY;
            }
            else
            {
                fprintf (stderr, "-i should be followed with a file name.\n");
                exit(1);
            }
        }
        else if (strcmp(argv[i], "-o") == 0)
        {
            i ++;
            if (i < argc)
            {
                freopen (argv[i], "w", stdout);
                i ++;
            }
            else
            {
                fprintf (stderr, "-o should be followed with a file name.\n");
                exit(1);
            }
        }
        else if (strcmp(argv[i], "-O") == 0)
        {
            i ++;
            option.work = option.FUNC;
        }
        else
        {
            fprintf (stderr, "Unrecognized parameter.\n");
            exit(1);
        }
}

void input ()
{
    switch (option.regex)
    {
        case option_t::STDIN:
            fgets (buf, BUF_LEN, stdin); 
            break;
        case option_t::FILE:
            {
                FILE *f = fopen (buf, "r");
                if (f == NULL)
                {
                    fprintf (stderr, "Failed to open file.\n");
                    exit(0);
                }
                fgets (buf, BUF_LEN, f); 
                fclose(f);
            }
            break;
    }
    while (strlen(buf) > 0 && buf[strlen(buf)-1] < ' ')
        buf[strlen(buf)-1] = '\0';
    fprintf (stderr, "Begin compiling...\n");
    regcomp (&reg, buf, REG_EXTENDED);
    fprintf (stderr, "Finished compiling.\n");
}

int main (int argc, char **argv)
{
    parse_options(argc, argv);
    input();
    parse ();

    return 0;
}
