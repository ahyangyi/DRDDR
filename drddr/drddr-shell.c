#include <stdio.h>
#include <string.h>
#include "drddr.h"

#define BUF_SIZE 33554432

char cmd[CMD_SIZE];
unsigned long address[N_ADDRESS];
char buf[BUF_SIZE];

void load ()
{
    FILE *f, *fa, *fp;
    int i = 0, j;

    if (strcmp(cmd, "load") == 0)
        f = fopen ("input.txt", "r");
    else
        f = fopen (cmd + 5, "r");

    if (f == NULL)
    {
        fprintf (stderr, "Failed to load command list file \"%s\"!\n", cmd + 5);
        return;
    }

    fa = fopen ("/sys/kernel/debug/drddr/address", "wb");
    if (fa == NULL)
    {
        fprintf (stderr, "Failed to open DRDDR address file.\n");
        return;
    }

    fp = fopen ("/sys/kernel/debug/drddr/addresscnt", "w");
    if (fp == NULL)
    {
        fprintf (stderr, "Failed to open DRDDR address count file.\n");
        return;
    }

    while (fgets (buf, BUF_SIZE, f))
    {
        address[i] = 0;
        for (j = 0; j < 16; j ++)
        {
            address[i] = address[i] * 16;
            if (buf[j] >= '0' && buf[j] <= '9')
                address[i] += buf[j] - '0';
            if (buf[j] >= 'a' && buf[j] <= 'f')
                address[i] += buf[j] - 'a' + 10;
            if (buf[j] >= 'A' && buf[j] <= 'F')
                address[i] += buf[j] - 'A' + 10;
        }
//        fprintf(stderr, "%16lx\n", address[i]);
        i ++;
    }

    printf ("Read %d addresses\n", i);

    fwrite(address, sizeof(unsigned long), i, fa);
    fprintf(fp, "%d", i);

    fclose (fa);
    fclose (fp);
    fclose (f);
}

void start (void)
{
    FILE* cmd;

    cmd = fopen ("/sys/kernel/debug/drddr/ctrl", "w");
    if (cmd == NULL)
    {
        fprintf (stderr, "Failed to open DRDDR control file.\n");
        return;
    }

    fprintf (cmd, "start");
    fclose(cmd);
}

void param (void)
{
    FILE* f;

    int x;

    if (sscanf (cmd + 6, "%d", &x) != 1)
    {
        fprintf (stderr, "\"Param\" command needs a parameter.\n");
        return;
    }

    f = fopen ("/sys/kernel/debug/drddr/param", "w");
    if (cmd == NULL)
    {
        fprintf (stderr, "Failed to open DRDDR param file.\n");
        return;
    }

    fprintf (f, "%d", x);
    fclose(f);
}

void stop (void)
{
    FILE* cmd;

    cmd = fopen ("/sys/kernel/debug/drddr/ctrl", "w");
    if (cmd == NULL)
    {
        fprintf (stderr, "Failed to open DRDDR control file.\n");
        return;
    }

    fprintf (cmd, "stop");
    fclose(cmd);
}

void status ()
{
    FILE *f;

    f = fopen ("/sys/kernel/debug/drddr/monitor", "r");
    {
        char c;
        while (fscanf (f, "%c", &c) != EOF)
            printf ("%c", c);
    }

    fclose(f);
}

void help ()
{
    printf ("Available commands:\n"
            "start|end|exit|load [filename]|param <param>|status|help\n"
            "\n");
}

int main ()
{
    printf(">");
    while (fgets(cmd, CMD_SIZE, stdin) != NULL)
    {
        while (cmd[0] != '\0' && cmd[strlen(cmd)-1] < ' ')
            cmd[strlen(cmd)-1] = 0x0;

        if (strstr(cmd, "start") == cmd)
        {
            start ();
        }
        else if (strstr(cmd, "end") == cmd)
        {
            stop ();
        }
        else if (strstr(cmd, "exit") == cmd)
        {
            return 0;
        }
        else if (strstr(cmd, "load") == cmd)
        {
            load ();
        }
        else if (strstr(cmd, "param") == cmd)
        {
            param ();
        }
        else if (strstr(cmd, "status") == cmd)
        {
            status ();
        }
        else if (strstr(cmd, "help") == cmd)
        {
            help ();
        }
        else
        {
            fprintf (stderr, "Syntax error. (read: \"%s\")\n", cmd);
        }
        printf(">");
    }

    return 0;
}
