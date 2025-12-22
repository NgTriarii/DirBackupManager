#ifndef MONITOR_H
#define MONITOR_H

#include <limits.h>
#include <sys/inotify.h>

// parts taken from: https://gitlab.com/SaQQ/sop1/-/blob/master/05_events/watch_tree.c?ref_type=heads

#define MAX_WATCHES 1024
#define EVENT_BUF_LEN (64 * (sizeof(struct inotify_event) + NAME_MAX + 1))

struct Watch
{
    int wd;      // Watch descriptor (key)
    char* path;  // Monitored path (value)
};

struct WatchMap
{
    struct Watch watch_map[MAX_WATCHES];
    int watch_count;
};

void add_to_map(struct WatchMap* map, int wd, const char* path);
struct Watch* find_watch(struct WatchMap* map, int wd);
void remove_from_map(struct WatchMap* map, int wd);
void update_watch_paths(struct WatchMap* map, const char* old_path, const char* new_path);

void add_watch_recursive(int notify_fd, struct WatchMap* map, const char* base_path);
void monitor_loop(const char* source_root, const char* target_root);
void cleanup_monitor(struct WatchMap* map, int fd);

#endif
