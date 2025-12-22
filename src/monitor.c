#define _GNU_SOURCE
#define _XOPEN_SOURCE 700

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "backup.h"
#include "monitor.h"
#include "utils.h"

extern volatile sig_atomic_t SIGTERM_flag;
extern volatile sig_atomic_t SIGINT_flag;

// parts taken from: https://gitlab.com/SaQQ/sop1/-/blob/master/05_events/watch_tree.c?ref_type=heads

void add_to_map(struct WatchMap* map, int wd, const char* path)
{
    if (map->watch_count >= MAX_WATCHES)
    {
        fprintf(stderr, "Exceeded max watches!\n");
        return;
    }
    map->watch_map[map->watch_count].wd = wd;
    map->watch_map[map->watch_count].path = strdup(path);  // Must copy the path!
    map->watch_count++;
    printf("new watch: '%s' @wd=%d\n", path, wd);
}

struct Watch* find_watch(struct WatchMap* map, int wd)
{
    for (int i = 0; i < map->watch_count; i++)
    {
        if (map->watch_map[i].wd == wd)
        {
            return &map->watch_map[i];
        }
    }
    return NULL;
}

void remove_from_map(struct WatchMap* map, int wd)
{
    for (int i = 0; i < map->watch_count; i++)
    {
        if (map->watch_map[i].wd == wd)
        {
            printf("removing watch: '%s' @wd=%d\n", map->watch_map[i].path, wd);
            free(map->watch_map[i].path);
            map->watch_map[i] = map->watch_map[map->watch_count - 1];
            map->watch_count--;
            return;
        }
    }
}

void add_watch_recursive(int notify_fd, struct WatchMap* map, const char* base_path)
{
    uint32_t mask = IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO | IN_CLOSE_WRITE;

    int wd = inotify_add_watch(notify_fd, base_path, mask);
    if (wd < 0)
    {
        perror("inotify_add_watch");
        return;
    }
    add_to_map(map, wd, base_path);

    DIR* dir = opendir(base_path);
    if (!dir)
    {
        perror("opendir");
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL)
    {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        {
            continue;
        }

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == 0 && S_ISDIR(st.st_mode))
        {
            add_watch_recursive(notify_fd, map, full_path);
        }
    }

    closedir(dir);
}

void update_watch_paths(struct WatchMap* map, const char* old_path, const char* new_path)
{
    size_t old_len = strlen(old_path);

    for (int i = 0; i < map->watch_count; i++)
    {
        if (strncmp(map->watch_map[i].path, old_path, old_len) == 0 &&
            (map->watch_map[i].path[old_len] == '/' || map->watch_map[i].path[old_len] == '\0'))
        {
            char new_full_path[PATH_MAX];
            // Sklej nową ścieżkę z resztą (sufiksem) starej ścieżki
            snprintf(new_full_path, sizeof(new_full_path), "%s%s", new_path, map->watch_map[i].path + old_len);

            free(map->watch_map[i].path);
            map->watch_map[i].path = strdup(new_full_path);
        }
    }
}

void cleanup_monitor(struct WatchMap* map, int fd)
{
    printf("Cleaning up monitoring resources...\n");
    for (int i = 0; i < map->watch_count; i++)
    {
        if (map->watch_map[i].path)
        {
            free(map->watch_map[i].path);
            map->watch_map[i].path = NULL;
        }
    }
    if (fd >= 0)
    {
        close(fd);
    }
}

void monitor_loop(const char* source_root, const char* target_root)
{
    int fd = inotify_init();
    if (fd < 0)
    {
        perror("inotify_init");
        printf("The watch in %s has not been established\n", source_root);
        return;
    }

    struct WatchMap map = {0};

    add_watch_recursive(fd, &map, source_root);

    printf("Monitoring started: %s -> %s\n", source_root, target_root);

    char buffer[EVENT_BUF_LEN];

    while (!SIGTERM_flag && !SIGINT_flag)
    {
        ssize_t len = read(fd, buffer, EVENT_BUF_LEN);
        if (len < 0)
        {
            if (errno == EINTR)
            {
                if (SIGTERM_flag || SIGINT_flag)
                    break;
                continue;
            }
            perror("read");
            break;
        }

        const struct inotify_event* event;
        char* ptr;

        for (ptr = buffer; ptr < buffer + len; ptr += sizeof(struct inotify_event) + event->len)
        {
            event = (const struct inotify_event*)ptr;

            if (event->len > 0)
            {
                struct Watch* w = find_watch(&map, event->wd);

                if (!w)
                    continue;

                char src_path[PATH_MAX];
                snprintf(src_path, sizeof(src_path), "%s/%s", w->path, event->name);

                char rel_path[PATH_MAX];
                if (starts_with(source_root, src_path))
                {
                    const char* suffix = src_path + strlen(source_root);
                    snprintf(rel_path, sizeof(rel_path), "%s", suffix);
                }
                else
                {
                    continue;
                }

                char dst_path[PATH_MAX];
                snprintf(dst_path, sizeof(dst_path), "%s%s", target_root, rel_path);

                // Case A: Create or Move-In
                if (event->mask & (IN_CREATE | IN_MOVED_TO))
                {
                    struct stat st;
                    if (lstat(src_path, &st) == -1)
                        continue;

                    if (S_ISDIR(st.st_mode))
                    {
                        mkdir(dst_path, st.st_mode);
                        add_watch_recursive(fd, &map, src_path);
                        recursive_copy(src_path, dst_path);
                    }
                    else if (S_ISREG(st.st_mode))
                    {
                        copy_file_data(src_path, dst_path, st.st_mode);
                    }
                    else if (S_ISLNK(st.st_mode))
                    {
                        char target[PATH_MAX];
                        ssize_t l = readlink(src_path, target, sizeof(target) - 1);
                        if (l != -1)
                        {
                            target[l] = '\0';

                            char root_src_abs[PATH_MAX];
                            if (realpath(source_root, root_src_abs) && starts_with(root_src_abs, target))
                            {
                                const char* suffix = target + strlen(root_src_abs);
                                char new_target[PATH_MAX];
                                snprintf(new_target, sizeof(new_target), "%s%s", target_root, suffix);
                                symlink(new_target, dst_path);
                            }
                            else
                            {
                                symlink(target, dst_path);
                            }
                        }
                    }
                }
                // Case B: Delete or Move-Out
                else if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                {
                    recursive_remove(dst_path);
                }
                // Case C: Modification
                else if (event->mask & IN_CLOSE_WRITE)
                {
                    struct stat st;
                    if (lstat(src_path, &st) == 0 && S_ISREG(st.st_mode))
                    {
                        copy_file_data(src_path, dst_path, st.st_mode);
                    }
                }
            }
        }
    }

    cleanup_monitor(&map, fd);
    printf("Monitoring stopped for %s\n", source_root);
}
