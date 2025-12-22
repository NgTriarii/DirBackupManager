#define _GNU_SOURCE
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "utils.h"

ssize_t bulk_read(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0)
            return c;
        if (c == 0)
            return len;  // EOF
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

ssize_t bulk_write(int fd, char* buf, size_t count)
{
    ssize_t c;
    ssize_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0)
            return c;
        buf += c;
        len += c;
        count -= c;
    } while (count > 0);
    return len;
}

// Function for checking if destination is in source
int starts_with(const char* pre, const char* str)
{
    size_t lenpre = strlen(pre);
    size_t lenstr = strlen(str);
    return lenstr < lenpre ? 0 : memcmp(pre, str, lenpre) == 0;
}

int dir_has_entries(const char* path)
{
    DIR* dir = opendir(path);
    if (!dir)
        return -1;

    struct dirent* entry;
    int has_entries = 0;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
        {
            has_entries = 1;
            break;
        }
    }
    closedir(dir);
    return has_entries;
}

int command_parser(const char* line, char** args)
{
    int count = 0;
    int i = 0;
    int len = strlen(line);

    if (len > 0 && line[len - 1] == '\n')
    {
        len--;
    }

    while (i < len && count < MAX_ARGUMENTS - 1)
    {
        while (i < len && line[i] == ' ')
        {
            i++;
        }

        if (i == len)
            break;

        char* arg = malloc(len - i + 1);  // freed in main
        if (!arg)
            break;
        int arg_pos = 0;
        int in_quotes = 0;

        while (i < len)
        {
            if (line[i] == '"')
            {
                in_quotes = !in_quotes;
                i++;
            }
            else if (line[i] == ' ' && !in_quotes)
            {
                break;
            }
            else
            {
                arg[arg_pos++] = line[i++];
            }
        }

        arg[arg_pos] = '\0';
        args[count++] = arg;
    }

    args[count] = NULL;
    return count;
}
