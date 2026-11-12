/**
 * Concurrent TCP Chat Server - Approach 2: Thread-per-Client
 * 
 * Model: Multi-threaded process.
 * Mechanism: Each connected client is handled by an independent thread.
 * Synchronization: Mutex / CriticalSection protects shared client registry.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef int socklen_t;
#define close closesocket
#define SOCK_TYPE SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#define SOCK_TYPE int
#define INVALID_SOCKET (-1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 9090
#define MAX_CLIENTS 64
#define BUFFER_SIZE 1024

static SOCK_TYPE client_sockets[MAX_CLIENTS];
static int client_count = 0;

#ifdef _WIN32
static CRITICAL_SECTION lock;
#define MUTEX_INIT()   InitializeCriticalSection(&lock)
#define MUTEX_LOCK()   EnterCriticalSection(&lock)
#define MUTEX_UNLOCK() LeaveCriticalSection(&lock)
#define MUTEX_DESTROY() DeleteCriticalSection(&lock)
#else
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
#define MUTEX_INIT()   ((void)0)
#define MUTEX_LOCK()   pthread_mutex_lock(&lock)
#define MUTEX_UNLOCK() pthread_mutex_unlock(&lock)
#define MUTEX_DESTROY() pthread_mutex_destroy(&lock)
#endif

void broadcast(const char *msg, int len, SOCK_TYPE sender)
{
    MUTEX_LOCK();
    for (int i = 0; i < client_count; i++) {
        if (client_sockets[i] != sender && client_sockets[i] != INVALID_SOCKET) {
            send(client_sockets[i], msg, len, 0);
        }
    }
    MUTEX_UNLOCK();
}

void remove_client(SOCK_TYPE sd)
{
    MUTEX_LOCK();
    for (int i = 0; i < client_count; i++) {
        if (client_sockets[i] == sd) {
            client_sockets[i] = client_sockets[client_count - 1];
            client_count--;
            break;
        }
    }
    MUTEX_UNLOCK();
}

#ifdef _WIN32
static DWORD WINAPI handle_client(LPVOID arg)
#else
static void *handle_client(void *arg)
#endif
{
    SOCK_TYPE sd = *(SOCK_TYPE *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    while (1) {
        int n = recv(sd, buffer, BUFFER_SIZE - 1, 0);
        if (n <= 0) {
            printf("[Disconnected] Socket fd: %d\n", (int)sd);
            break;
        }

        buffer[n] = '\0';
        printf("[Client %d]: %s", (int)sd, buffer);

        char broadcast_msg[BUFFER_SIZE + 32];
        int msg_len = snprintf(broadcast_msg, sizeof(broadcast_msg),
                               "[Client %d]: %s", (int)sd, buffer);
        broadcast(broadcast_msg, msg_len, sd);
    }

    remove_client(sd);
    close(sd);
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }
#endif
    MUTEX_INIT();

    SOCK_TYPE server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) {
        perror("socket creation failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

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
    printf(" TCP Chat Server [Thread-per-Client]\n");
    printf(" Listening on port %d...\n", PORT);
    printf("====================================================\n");

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        SOCK_TYPE new_sd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);

        if (new_sd == INVALID_SOCKET) {
            perror("accept failed");
            continue;
        }

        MUTEX_LOCK();
        if (client_count >= MAX_CLIENTS) {
            MUTEX_UNLOCK();
            printf("Max clients reached. Rejecting connection.\n");
            close(new_sd);
            continue;
        }
        client_sockets[client_count++] = new_sd;
        MUTEX_UNLOCK();

        printf("[New Connection] Socket fd: %d (%d online)\n", (int)new_sd, client_count);

        SOCK_TYPE *arg = malloc(sizeof(SOCK_TYPE));
        *arg = new_sd;

#ifdef _WIN32
        HANDLE hThread = CreateThread(NULL, 0, handle_client, arg, 0, NULL);
        if (hThread) CloseHandle(hThread);
#else
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, arg);
        pthread_detach(tid);
#endif
    }

    close(server_fd);
    MUTEX_DESTROY();
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
