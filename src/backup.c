#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "backup.h"
#include "utils.h"

int copy_file_data(const char* src, const char* dst, mode_t mode)
{
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0)
        return -1;

    int fd_dst = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd_dst < 0)
    {
        close(fd_src);
        return -1;
    }

    char buffer[4096];
    ssize_t bytes_read;

    while ((bytes_read = bulk_read(fd_src, buffer, sizeof(buffer))) > 0)
    {
        if (bulk_write(fd_dst, buffer, bytes_read) < 0)
        {
            close(fd_src);
            close(fd_dst);
            return -1;
        }
    }

    // Needed for checking if files are the same without scanning the whole content
    struct stat src_st;
    fstat(fd_src, &src_st);
    struct timespec times[2];
    times[0] = src_st.st_atim;  // Access time
    times[1] = src_st.st_mtim;  // Modification time

    // man 3p futimens
    if (futimens(fd_dst, times) == -1)
    {
        perror("futimens");
    }

    close(fd_src);
    close(fd_dst);
    return 0;
}

int recursive_copy(char* src_path, char* dst_path)
{
    struct stat src_st;
    if (lstat(src_path, &src_st) == -1)
    {
        fprintf(stderr, "Error: Source path '%s' does not exist or is inaccessible.\n", src_path);
        return -1;
    }
    if (!S_ISDIR(src_st.st_mode))
    {
        fprintf(stderr, "Error: Source '%s' is not a directory.\n", src_path);
        return -1;
    }

    char src_abs[PATH_MAX];
    char dst_abs[PATH_MAX];

    if (!realpath(src_path, src_abs))
    {
        perror("Error resolving source path");
        return -1;
    }

    if (realpath(dst_path, dst_abs))
    {
        if (starts_with(src_abs, dst_abs))
        {
            fprintf(stderr, "Error: Cannot create backup inside the source directory (Infinite Loop).\n");
            return -1;
        }
    }
    else
    {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)))
        {
            char full_dst[PATH_MAX];
            int required_len;

            if (dst_path[0] == '/')
            {
                required_len = snprintf(full_dst, sizeof(full_dst), "%s", dst_path);
            }
            else
            {
                required_len = snprintf(full_dst, sizeof(full_dst), "%s/%s", cwd, dst_path);
            }

            if (required_len < 0 || required_len >= (int)sizeof(full_dst))
            {
                fprintf(stderr, "Error: Destination path is too long.\n");
                return -1;
            }

            if (starts_with(src_abs, full_dst))
            {
                fprintf(stderr, "Error: Cannot create backup inside the source directory.\n");
                return -1;
            }
        }
    }

    struct stat dst_st;
    if (stat(dst_path, &dst_st) == 0)
    {
        if (!S_ISDIR(dst_st.st_mode))
        {
            fprintf(stderr, "Error: Target '%s' exists but is not a directory.\n", dst_path);
            return -1;
        }

        if (dir_has_entries(dst_path))
        {
            fprintf(stderr, "Error: Target directory '%s' is not empty.\n", dst_path);
            return -1;
        }
    }
    else
    {
        if (mkdir(dst_path, src_st.st_mode) < 0)
        {
            perror("Error creating target directory");
            return -1;
        }
    }
    recursive_copy_internal(src_path, dst_path, src_path, dst_path);
    return EXIT_SUCCESS;
}

void recursive_copy_internal(const char* root_src, const char* root_dst, const char* current_src,
                             const char* current_dst)
{
    DIR* dir = opendir(current_src);
    if (!dir)
        return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char sub_src[PATH_MAX];
        char sub_dst[PATH_MAX];

        snprintf(sub_src, sizeof(sub_src), "%s/%s", current_src, entry->d_name);
        snprintf(sub_dst, sizeof(sub_dst), "%s/%s", current_dst, entry->d_name);

        struct stat st;
        if (lstat(sub_src, &st) == -1)
            continue;

        if (S_ISDIR(st.st_mode))
        {
            mkdir(sub_dst, st.st_mode);
            recursive_copy_internal(root_src, root_dst, sub_src, sub_dst);
        }
        else if (S_ISREG(st.st_mode))
        {
            copy_file_data(sub_src, sub_dst, st.st_mode);
        }
        else if (S_ISLNK(st.st_mode))
        {
            char link_target[PATH_MAX];
            ssize_t len = readlink(sub_src, link_target, sizeof(link_target) - 1);
            if (len != -1)
            {
                link_target[len] = '\0';
                char root_src_abs[PATH_MAX];
                if (realpath(root_src, root_src_abs))
                {
                    if (starts_with(root_src_abs, link_target))
                    {
                        const char* suffix = link_target + strlen(root_src_abs);

                        char new_target[PATH_MAX];
                        snprintf(new_target, sizeof(new_target), "%s%s", root_dst, suffix);

                        symlink(new_target, sub_dst);
                    }
                    else
                    {
                        symlink(link_target, sub_dst);
                    }
                }
                else
                {
                    symlink(link_target, sub_dst);
                }
            }
        }
    }
    closedir(dir);
}

int recursive_remove(const char* path)
{
    struct stat st;
    if (lstat(path, &st) < 0)
    {
        return 0;
    }

    if (!S_ISDIR(st.st_mode))
    {
        return unlink(path);
    }

    DIR* dir = opendir(path);
    if (!dir)
        return -1;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char sub_path[PATH_MAX];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", path, entry->d_name);

        recursive_remove(sub_path);
    }
    closedir(dir);

    return rmdir(path);
}

void clean_target(const char* backup_root, const char* dst_root)
{
    DIR* dir = opendir(dst_root);
    if (!dir)
        return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char tar_sub[PATH_MAX];
        char backup_sub[PATH_MAX];
        snprintf(tar_sub, sizeof(tar_sub), "%s/%s", dst_root, entry->d_name);
        snprintf(backup_sub, sizeof(backup_sub), "%s/%s", backup_root, entry->d_name);

        struct stat st;
        if (lstat(backup_sub, &st) == -1)
        {
            recursive_remove(tar_sub);
        }
        else if (S_ISDIR(st.st_mode))
        {
            clean_target(backup_sub, tar_sub);
        }
    }
    closedir(dir);
}

int restore_backup(const char* backup_root, const char* target_root)
{
    clean_target(backup_root, target_root);

    DIR* dir = opendir(backup_root);
    if (!dir)
    {
        perror("Error occured while opening backup directory\n");
        return -1;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)
        {
            continue;
        }

        char src_sub[PATH_MAX];
        char tar_sub[PATH_MAX];

        snprintf(src_sub, sizeof(src_sub), "%s/%s", backup_root, entry->d_name);
        snprintf(tar_sub, sizeof(tar_sub), "%s/%s", target_root, entry->d_name);

        struct stat src_st, dst_st;
        if (lstat(src_sub, &src_st) == -1)
            continue;

        int copy_needed = 1;

        if (lstat(tar_sub, &dst_st) == 0)
        {
            if (S_ISDIR(src_st.st_mode))
            {
                if (S_ISDIR(dst_st.st_mode))
                    copy_needed = 0;
            }
            else if (S_ISREG(src_st.st_mode))
            {
                if (src_st.st_size == dst_st.st_size && src_st.st_mtime == dst_st.st_mtime)
                {
                    copy_needed = 0;
                }
            }
        }

        if (S_ISDIR(src_st.st_mode))
        {
            mkdir(tar_sub, src_st.st_mode);
            restore_backup(src_sub, tar_sub);
        }
        else if (copy_needed)
        {
            if (S_ISREG(src_st.st_mode))
            {
                copy_file_data(src_sub, tar_sub, src_st.st_mode);
            }
            else if (S_ISLNK(src_st.st_mode))
            {
                char link_target[PATH_MAX];
                ssize_t len = readlink(src_sub, link_target, sizeof(link_target) - 1);
                if (len != -1)
                {
                    link_target[len] = '\0';

                    char backup_abs[PATH_MAX];
                    if (realpath(backup_root, backup_abs) && starts_with(backup_abs, link_target))
                    {
                        const char* suffix = link_target + strlen(backup_abs);
                        char new_target[PATH_MAX];
                        snprintf(new_target, sizeof(new_target), "%s%s", target_root, suffix);
                        unlink(tar_sub);
                        symlink(new_target, tar_sub);
                    }
                    else
                    {
                        unlink(tar_sub);
                        symlink(link_target, tar_sub);
                    }
                }
            }
        }
    }
    closedir(dir);
    return 0;
}
