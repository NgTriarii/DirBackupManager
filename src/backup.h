#ifndef BACKUP_H
#define BACKUP_H

#include <sys/types.h>

int copy_file_data(const char* src, const char* dst, mode_t mode);
int recursive_copy(char* src_path, char* dst_path);
void recursive_copy_internal(const char* root_src, const char* root_dst, const char* current_src,
                             const char* current_dst);
int recursive_remove(const char* path);
int restore_backup(const char* backup_root, const char* target_root);

#endif
