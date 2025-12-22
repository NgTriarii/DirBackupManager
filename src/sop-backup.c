#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// #define _GNU_SOURCE
// #define _POSIX_C_SOURCE 200809L

#include "backup.h"
#include "monitor.h"
#include "utils.h"

#define ERR(source) \
    (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), kill(0, SIGTERM), exit(EXIT_FAILURE))

#define MAX_WATCH 64
#define MAX_BUFFER 512
#define MAX_BACKUPS 64

sig_atomic_t last_signal;
sig_atomic_t SIGTERM_flag = 0;
sig_atomic_t SIGINT_flag = 0;

typedef struct
{
    pid_t pid;
    char src[PATH_MAX];
    char dst[PATH_MAX];
} Backup;

Backup active_backups[MAX_BACKUPS];
int backup_count = 0;

void sig_handler(int sig)
{
    last_signal = sig;
    if (last_signal == SIGTERM || last_signal == SIGINT)
    {
        if (last_signal == SIGTERM)
        {
            SIGTERM_flag = 1;
        }
        if (last_signal == SIGINT)
        {
            SIGINT_flag = 1;
        }
    }
}

void sethandler(void (*f)(int), int sigNo)
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        ERR("sigaction");
}

void usage(int argc, char* argv[])
{
    printf("%s\n", argv[0]);
    printf("\tToo many arguments\n");
    exit(EXIT_FAILURE);
}

void free_args(char** args)
{
    if (!args)
        return;

    for (int i = 0; args[i] != NULL; i++)
    {
        free(args[i]);
        args[i] = NULL;
    }
}

int exit_command()
{
    kill(0, SIGTERM);
    return 0;
}

int find_backup_index(pid_t pid)
{
    for (int i = 0; i < backup_count; i++)
    {
        if (active_backups[i].pid == pid)
            return i;
    }
    return -1;
}

int backup_exists(const char* src, const char* dst)
{
    for (int i = 0; i < backup_count; i++)
    {
        if (strcmp(active_backups[i].src, src) == 0 && strcmp(active_backups[i].dst, dst) == 0)
        {
            return 1;
        }
    }
    return 0;
}

void register_backup(pid_t pid, const char* src, const char* dst)
{
    if (backup_count >= MAX_BACKUPS)
    {
        fprintf(stderr, "Error: Maximum backup limit reached.\n");
        return;
    }
    active_backups[backup_count].pid = pid;

    char temp_path[PATH_MAX];

    if (realpath(src, temp_path) != NULL)
    {
        strncpy(active_backups[backup_count].src, temp_path, PATH_MAX);
    }
    else
    {
        if (src[0] != '/' && getcwd(temp_path, sizeof(temp_path)))
        {
            size_t len = strlen(temp_path);
            snprintf(temp_path + len, PATH_MAX - len, "/%s", src);
            strncpy(active_backups[backup_count].src, temp_path, PATH_MAX);
        }
        else
        {
            strncpy(active_backups[backup_count].src, src, PATH_MAX);
        }
    }

    if (realpath(dst, temp_path) != NULL)
    {
        strncpy(active_backups[backup_count].dst, temp_path, PATH_MAX);
    }
    else
    {
        if (dst[0] != '/' && getcwd(temp_path, sizeof(temp_path)))
        {
            size_t len = strlen(temp_path);
            snprintf(temp_path + len, PATH_MAX - len, "/%s", dst);
            strncpy(active_backups[backup_count].dst, temp_path, PATH_MAX);
        }
        else
        {
            strncpy(active_backups[backup_count].dst, dst, PATH_MAX);
        }
    }

    backup_count++;
}

void remove_backup(pid_t pid)
{
    int idx = find_backup_index(pid);
    if (idx != -1)
    {
        active_backups[idx] = active_backups[backup_count - 1];
        backup_count--;
    }
}

int add_command(char** comm_args, int arg_num)
{
    struct stat st;
    if (stat(comm_args[1], &st) == -1)
    {
        perror("Error accessing source");
        return 0;
    }

    char abs_src[PATH_MAX];
    if (!realpath(comm_args[1], abs_src))
    {
        if (comm_args[1][0] != '/' && getcwd(abs_src, sizeof(abs_src)))
        {
            size_t len = strlen(abs_src);
            snprintf(abs_src + len, PATH_MAX - len, "/%s", comm_args[1]);
        }
        else
        {
            strncpy(abs_src, comm_args[1], PATH_MAX);
        }
    }

    for (int i = 2; i < arg_num; i++)
    {
        char abs_dst[PATH_MAX];

        if (!realpath(comm_args[i], abs_dst))
        {
            if (comm_args[i][0] != '/')
            {
                if (getcwd(abs_dst, sizeof(abs_dst)) == NULL)
                {
                    perror("Error getting current working directory");
                    fprintf(stderr, "Skipping target '%s' due to path resolution error.\n", comm_args[i]);
                    continue;
                }

                size_t len = strlen(abs_dst);
                if (len + 1 + strlen(comm_args[i]) >= PATH_MAX)
                {
                    fprintf(stderr, "Error: Path too long for '%s'\n", comm_args[i]);
                    continue;
                }
                snprintf(abs_dst + len, PATH_MAX - len, "/%s", comm_args[i]);
            }
            else
            {
                strncpy(abs_dst, comm_args[i], PATH_MAX);
            }
        }

        if (backup_exists(abs_src, abs_dst))
        {
            fprintf(stderr, "Error: Backup from '%s' to '%s' is already active.\n", comm_args[1], comm_args[i]);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            continue;
        }

        if (pid == 0)
        {
            if (recursive_copy(comm_args[1], comm_args[i]) == 0)
            {
                printf("[%d] Backup [%s -> %s] completed.\n", getpid(), comm_args[1], comm_args[i]);

                monitor_loop(comm_args[1], comm_args[i]);
            }
            exit(EXIT_SUCCESS);
        }
        else
        {
            register_backup(pid, abs_src, abs_dst);
            printf("[%d] Started backup process [%d] for %s\n", getpid(), pid, comm_args[i]);
        }
    }
    return 0;
}

void list_command()
{
    if (backup_count == 0)
    {
        printf("No backups active!\n\n");
        return;
    }
    printf("\nLIST OF THE ACTIVE BACKUPS\n");
    printf("============================================- + -============================================\n");
    for (int i = 0; i < backup_count; i++)
    {
        printf("[Watch nr %d] Source: %s    Destination: %s\n", i, active_backups[i].src, active_backups[i].dst);
    }
    printf("============================================- + -============================================\n\n");
}

void end_command(char** comm_args, int arg_num)
{
    if (backup_count == 0)
    {
        printf("No backups active!\n\n");
        return;
    }

    char* source = comm_args[1];
    char abs_src[PATH_MAX];

    if (!realpath(source, abs_src))
    {
        if (source[0] != '/' && getcwd(abs_src, sizeof(abs_src)))
        {
            size_t len = strlen(abs_src);
            snprintf(abs_src + len, PATH_MAX - len, "/%s", source);
        }
        else
        {
            strncpy(abs_src, source, PATH_MAX);
        }
    }

    for (int i = 2; i < arg_num; i++)
    {
        char* target = comm_args[i];
        char abs_dst[PATH_MAX];

        if (!realpath(target, abs_dst))
        {
            if (target[0] != '/' && getcwd(abs_dst, sizeof(abs_dst)))
            {
                size_t len = strlen(abs_dst);
                snprintf(abs_dst + len, PATH_MAX - len, "/%s", target);
            }
            else
            {
                strncpy(abs_dst, target, PATH_MAX);
            }
        }

        if (backup_exists(abs_src, abs_dst))
        {
            int pid;
            for (int j = 0; j < backup_count; j++)
            {
                if (strcmp(active_backups[j].src, abs_src) == 0 && strcmp(active_backups[j].dst, abs_dst) == 0)
                {
                    pid = active_backups[j].pid;
                    break;
                }
            }
            kill(pid, SIGTERM);
            remove_backup(pid);
            printf("Backup [%s -> %s] ended.\n", source, target);
        }
        else
            printf("Cannot end a backup [%s -> %s]: This backup does not exist\n\n", source, target);
    }
    printf("\n");
}

void restore_command(char* source, char* target)
{
    printf("Restoring from %s to %s...\n", source, target);
    if (restore_backup(source, target) == 0)
    {
        printf("Restore completed successfully.\n");
    }
    else
    {
        fprintf(stderr, "Restore failed.\n");
    }
}

void clean_zombies()
{
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        if (WIFEXITED(status) || WIFSIGNALED(status))
        {
            remove_backup(pid);
        }
    }
}

int main(int argc, char** argv)
{
    if (argc != 1)
    {
        usage(argc, argv);
    }

    sethandler(sig_handler, SIGTERM);
    sethandler(sig_handler, SIGINT);

    sigset_t mask, oldmask;
    sigemptyset(&mask);

    // OBSOLETE
    //  not blocking critical signals, list from:
    //  https://stackoverflow.com/questions/11618112/most-important-signals-to-handle
    //  sigdelset(&mask, SIGSEGV);
    //  sigdelset(&mask, SIGILL);
    //  sigdelset(&mask, SIGBUS);
    //  sigdelset(&mask, SIGFPE);
    //  sigdelset(&mask, SIGABRT);
    //  sigdelset(&mask, SIGPIPE);

    sigprocmask(SIG_SETMASK, &mask, &oldmask);

    char* line = NULL;
    size_t size = 0;
    char* comm_args[MAX_ARGUMENTS] = {0};

    printf("\n***Welcome to the sop-backup program***\n");
    printf("List of available commands:\n");
    printf("1. add <source> <target1> ... \n");
    printf("2. exit\n");
    printf("3. list\n");
    printf("4. end <source> <target>\n");
    printf("5. restore <backup_source> <target>\n\n");

    while (!SIGINT_flag && !SIGTERM_flag && last_signal != SIGTERM && last_signal != SIGINT)
    {
        clean_zombies();

        errno = 0;
        ssize_t chars = getline(&line, &size, stdin);

        if (chars == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ERR("getline");
        }

        int count = command_parser(line, comm_args);

        if (count == 0)
            continue;

        if (strcmp(comm_args[0], "add") == 0)
        {
            if (count < 3)
            {
                fprintf(stderr, "Usage: add <source> <target1> ...\n\n");
            }
            else
            {
                add_command(comm_args, count);
            }
        }
        else if (strcmp(comm_args[0], "exit") == 0)
        {
            if (count > 1)
            {
                fprintf(stderr, "Usage: exit\n\n");
            }
            exit_command();

            break;
        }
        else if (strcmp(comm_args[0], "list") == 0)
        {
            if (count > 1)
            {
                fprintf(stderr, "Usage: list\n\n");
            }
            else
                list_command();
        }
        else if (strcmp(comm_args[0], "end") == 0)
        {
            if (count < 3)
            {
                fprintf(stderr, "Usage: end <backup_source> <target1> ...\n\n");
            }
            else
            {
                end_command(comm_args, count);
            }
        }
        else if (strcmp(comm_args[0], "restore") == 0)
        {
            if (count != 3)
            {
                fprintf(stderr, "Usage: restore <backup_source> <target>\n\n");
            }
            else
            {
                restore_command(comm_args[1], comm_args[2]);
            }
        }
        else
        {
            printf("%s - No such command found\n\n", comm_args[0]);
        }

        free_args(comm_args);
    }

    free_args(comm_args);
    free(line);

    while (wait(NULL) > 0)
        ;

    printf("\nApp closed\n");

    return EXIT_SUCCESS;
}
