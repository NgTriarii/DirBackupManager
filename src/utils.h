#ifndef UTILS_H
#define UTILS_H

#include <sys/types.h>

#define MAX_ARGUMENTS 32

int command_parser(const char* line, char** args);

ssize_t bulk_read(int fd, char* buf, size_t count);
ssize_t bulk_write(int fd, char* buf, size_t count);

int starts_with(const char* pre, const char* str);
int dir_has_entries(const char* path);

#endif
