/**
 * Concurrent TCP Chat Server - Approach 3: Fork-per-Client
 * 
 * Model: Multi-process (POSIX standard).
 * Mechanism: fork() duplicates the process for every incoming connection.
 * Memory: Child process gets isolated virtual address space.
 * Clean-up: Uses SIGCHLD handler to reap zombie processes via waitpid().
 * 
 * Note: fork() is a native POSIX system call (Linux / macOS / WSL).
 */

#ifdef _WIN32
#error "fork() is a POSIX system call and is only supported on Linux/UNIX/WSL environments. Please compile inside WSL/Linux."
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9090
#define MAX_CLIENTS 64
#define BUFFER_SIZE 1024

/* Shared memory structure to hold active client descriptors across processes */
typedef struct {
    int sockets[MAX_CLIENTS];
    int count;
} SharedClientTable;

static SharedClientTable *shared_table = NULL;

/* Signal handler to prevent zombie processes */
void sigchld_handler(int sig)
{
    (void)sig;
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

void broadcast_all(const char *msg, int len, int sender_fd)
{
    if (!shared_table) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        int target = shared_table->sockets[i];
        if (target > 0 && target != sender_fd) {
            send(target, msg, len, 0);
        }
    }
}

void remove_shared_client(int fd)
{
    if (!shared_table) return;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (shared_table->sockets[i] == fd) {
            shared_table->sockets[i] = 0;
            shared_table->count--;
            break;
        }
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Setup SIGCHLD handler to reap terminated child processes */
    struct sigaction sa;
    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    if (sigaction(SIGCHLD, &sa, 0) == -1) {
        perror("sigaction failed");
        return 1;
    }

    /* Allocate shared memory table so processes can see active sockets */
    shared_table = (SharedClientTable *)mmap(NULL, sizeof(SharedClientTable),
                                              PROT_READ | PROT_WRITE,
                                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shared_table == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }
    memset(shared_table, 0, sizeof(SharedClientTable));

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        return 1;
    }

    printf("====================================================\n");
    printf(" TCP Chat Server [Fork-per-Client (Multi-Process)]\n");
    printf(" Listening on port %d...\n", PORT);
    printf(" Parent Process PID: %d\n", getpid());
    printf("====================================================\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept failed");
            continue;
        }

        /* Register in shared memory */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (shared_table->sockets[i] == 0) {
                shared_table->sockets[i] = client_fd;
                shared_table->count++;
                break;
            }
        }

        printf("[Parent] Client connected: socket %d from %s:%d\n",
               client_fd, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            close(client_fd);
            remove_shared_client(client_fd);
            continue;
        }

        if (pid == 0) {
            /* ================= CHILD PROCESS ================= */
            /* Child does not need the listening server socket */
            close(server_fd);

            printf("[Child PID %d] Handling client socket %d\n", getpid(), client_fd);

            char buffer[BUFFER_SIZE];
            while (1) {
                int bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                if (bytes <= 0) {
                    printf("[Child PID %d] Client %d disconnected\n", getpid(), client_fd);
                    break;
                }
                buffer[bytes] = '\0';
                printf("[Child PID %d - Client %d]: %s", getpid(), client_fd, buffer);

                char bcast[BUFFER_SIZE + 32];
                int blen = snprintf(bcast, sizeof(bcast), "[Client %d]: %s", client_fd, buffer);
                broadcast_all(bcast, blen, client_fd);
            }

            remove_shared_client(client_fd);
            close(client_fd);
            exit(0); /* Terminate child process */
        } else {
            /* ================= PARENT PROCESS ================= */
            /* Parent closes client_fd because child now owns its duplicate */
            close(client_fd);
        }
    }

    close(server_fd);
    munmap(shared_table, sizeof(SharedClientTable));
    return 0;
}

