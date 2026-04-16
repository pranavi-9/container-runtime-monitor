#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "monitor_ioctl.h" 

#define STACK_SIZE (1024 * 1024)
#define CONTAINER_ID_LEN 32
#define CHILD_COMMAND_LEN 256
#define MAX_CONTAINERS 10

#define CONTROL_PATH "/tmp/mini_runtime.sock"

/* ================= ENUM ================= */
typedef enum {
    CMD_SUPERVISOR = 0,
    CMD_START,
    CMD_RUN,
    CMD_PS,
    CMD_LOGS,
    CMD_STOP
} command_kind_t;

/* ================= REQUEST ================= */
typedef struct {
    command_kind_t kind;
    char container_id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[CHILD_COMMAND_LEN];
} control_request_t;

/* ================= CHILD CONFIG ================= */
typedef struct {
    char id[CONTAINER_ID_LEN];
    char rootfs[PATH_MAX];
    char command[CHILD_COMMAND_LEN];
} child_config_t;

/* ================= CONTAINER INFO ================= */
typedef struct {
    char id[CONTAINER_ID_LEN];
    pid_t pid;
} container_info;

/* ================= CHILD FUNCTION ================= */
int child_fn(void *arg)
{
    child_config_t *config = (child_config_t *)arg;

    printf(">>> CHILD STARTED <<<\n"); fflush(stdout);

    if (chroot(config->rootfs) != 0) {
        perror("chroot failed");
        return 1;
    }

    printf(">>> CHROOT OK <<<\n"); fflush(stdout);

    chdir("/");

    int log_fd = open("/log.txt", O_CREAT | O_WRONLY | O_APPEND, 0644);
    dup2(log_fd, STDOUT_FILENO);
    dup2(log_fd, STDERR_FILENO);
    close(log_fd);

    if (mount("proc", "/proc", "proc", 0, NULL) != 0) {
        perror("mount failed");
        return 1;
    }

    printf(">>> MOUNT OK <<<\n"); fflush(stdout);

    char *args[] = {"/bin/sh", NULL};
    execvp(args[0], args);

    perror("exec failed");
    return 1;
}

/* ================= SEND REQUEST ================= */
static int send_control_request(const control_request_t *req)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    write(fd, req, sizeof(*req));
    close(fd);
    return 0;
}

/* ================= SUPERVISOR ================= */
static int run_supervisor(const char *rootfs)
{
    printf("Supervisor started with rootfs: %s\n", rootfs);

    int server_fd;
    struct sockaddr_un addr;

    container_info containers[MAX_CONTAINERS];
    int container_count = 0;

    server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    unlink(CONTROL_PATH);

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CONTROL_PATH);

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Supervisor listening...\n");

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);

        control_request_t req;
        memset(&req, 0, sizeof(req));

        read(client_fd, &req, sizeof(req));

        printf("Received command %d\n", req.kind);

        if (req.kind == CMD_START) {

            char *stack = malloc(STACK_SIZE);

            child_config_t config;
            memset(&config, 0, sizeof(config));

            strcpy(config.id, req.container_id);
            strcpy(config.rootfs, req.rootfs);

            pid_t pid = clone(child_fn,
                              stack + STACK_SIZE,
                              CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS | SIGCHLD,
                              &config);

            if (pid > 0) {

                printf("Started container %s PID %d\n",
                       req.container_id, pid);

                /* ✅ STORE */
                strcpy(containers[container_count].id, req.container_id);
                containers[container_count].pid = pid;
                container_count++;

                /* 🔥 REGISTER WITH MONITOR */
                int fd = open("/dev/container_monitor", O_RDWR);
                if (fd >= 0) {
                    struct monitor_request mreq;

                    memset(&mreq, 0, sizeof(mreq));
                    strcpy(mreq.container_id, req.container_id);
                    mreq.pid = pid;

                    mreq.soft_limit_bytes = 5* 1024 * 1024;  // LOW → trigger fast
                    mreq.hard_limit_bytes = 10 * 1024 * 1024;

                    ioctl(fd, MONITOR_REGISTER, &mreq);
                    close(fd);

                    printf("Monitor registered for PID %d\n", pid);
                } else {
                    perror("monitor open failed");
                }
            }
        }

        else if (req.kind == CMD_PS) {
            printf("Running containers:\n");
            for (int i = 0; i < container_count; i++) {
                printf("ID: %s PID: %d\n",
                       containers[i].id,
                       containers[i].pid);
            }
        }

        close(client_fd);
    }

    return 0;
}

/* ================= COMMANDS ================= */
static int cmd_start(int argc, char *argv[])
{
    control_request_t req;
    memset(&req, 0, sizeof(req));

    req.kind = CMD_START;
    strcpy(req.container_id, argv[2]);
    strcpy(req.rootfs, argv[3]);
    strcpy(req.command, argv[4]);

    return send_control_request(&req);
}

static int cmd_ps()
{
    control_request_t req;
    memset(&req, 0, sizeof(req));

    req.kind = CMD_PS;
    return send_control_request(&req);
}

/* ================= MAIN ================= */
int main(int argc, char *argv[])
{
    if (strcmp(argv[1], "supervisor") == 0)
        return run_supervisor(argv[2]);

    if (strcmp(argv[1], "start") == 0)
        return cmd_start(argc, argv);

    if (strcmp(argv[1], "ps") == 0)
        return cmd_ps();

    printf("Unknown command\n");
    return 1;
}
