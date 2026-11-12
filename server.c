#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define close closesocket
#define SOCK SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#define SOCK int
#define INVALID_SOCKET (-1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORT 9090
#define MAX_CLIENTS 30
#define BUFFER_SIZE 1024

static SOCK clients[MAX_CLIENTS];
static int client_count = 0;

#ifdef _WIN32
static CRITICAL_SECTION cs;
#define LOCK()   EnterCriticalSection(&cs)
#define UNLOCK() LeaveCriticalSection(&cs)
#else
static pthread_mutex_t cs = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&cs)
#define UNLOCK() pthread_mutex_unlock(&cs)
#endif

static void broadcast(const char *data, int length, SOCK sender)
{
    LOCK();
    for (int i = 0; i < client_count; i++) {
        if (clients[i] != sender) {
            send(clients[i], data, length, 0);
        }
    }
    UNLOCK();
}

static void remove_client(SOCK sock)
{
    LOCK();
    for (int i = 0; i < client_count; i++) {
        if (clients[i] == sock) {
            clients[i] = clients[client_count - 1];
            client_count--;
            break;
        }
    }
    UNLOCK();
}

#ifdef _WIN32
static DWORD WINAPI client_thread(LPVOID arg)
#else
static void *client_thread(void *arg)
#endif
{
    SOCK sock = *(SOCK *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    while (1) {
        int n = recv(sock, buffer, BUFFER_SIZE, 0);
        if (n <= 0) {
            fprintf(stderr, "Client disconnected: socket %llu\n", (unsigned long long)sock);
            break;
        }

        /* Display on server console */
        char display[BUFFER_SIZE + 1];
        memcpy(display, buffer, n);
        display[n] = '\0';
        fprintf(stderr, "[Client %llu]: %s", (unsigned long long)sock, display);

        /* Broadcast to other clients */
        broadcast(buffer, n, sock);
    }

    remove_client(sock);
    close(sock);
    return 0;
}

/* Thread to read server's own stdin and broadcast to all clients */
#ifdef _WIN32
static DWORD WINAPI server_input_thread(LPVOID arg)
#else
static void *server_input_thread(void *arg)
#endif
{
    (void)arg;
    char buffer[BUFFER_SIZE];
    while (1) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;

        fprintf(stderr, "[Server]: %s", buffer);

        char msg[BUFFER_SIZE + 16];
        int mlen = snprintf(msg, sizeof(msg), "[Server]: %s", buffer);
        broadcast(msg, mlen, INVALID_SOCKET);
    }
    return 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }
    InitializeCriticalSection(&cs);
    #endif

    SOCK server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) { fprintf(stderr, "socket failed\n"); return 1; }

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        fprintf(stderr, "bind failed\n"); close(server_fd); return 1;
    }
    if (listen(server_fd, 10) < 0) {
        fprintf(stderr, "listen failed\n"); close(server_fd); return 1;
    }

    fprintf(stderr, "Chat server listening on port %d...\n", PORT);
    fprintf(stderr, "You can type messages here too!\n");

    #ifdef _WIN32
    CreateThread(NULL, 0, server_input_thread, NULL, 0, NULL);
    #else
    pthread_t input_tid;
    pthread_create(&input_tid, NULL, server_input_thread, NULL);
    #endif

    while (1) {
        fprintf(stderr, "Waiting for connections...\n");
        SOCK new_fd = accept(server_fd, NULL, NULL);
        fprintf(stderr, "accept() returned: %llu\n", (unsigned long long)new_fd);

        if (new_fd == INVALID_SOCKET) {
            fprintf(stderr, "accept failed\n");
            continue;
        }

        LOCK();
        if (client_count >= MAX_CLIENTS) {
            UNLOCK();
            fprintf(stderr, "Max clients reached.\n");
            close(new_fd);
            continue;
        }
        clients[client_count++] = new_fd;
        UNLOCK();

        fprintf(stderr, "Client connected! (%d online)\n", client_count);

        SOCK *arg = malloc(sizeof(SOCK));
        *arg = new_fd;

        #ifdef _WIN32
        HANDLE h = CreateThread(NULL, 0, client_thread, arg, 0, NULL);
        if (!h) {
            fprintf(stderr, "CreateThread failed!\n");
        } else {
            CloseHandle(h);
        }
        #else
        pthread_t tid;
        pthread_create(&tid, NULL, client_thread, arg);
        pthread_detach(tid);
        #endif
    }

    close(server_fd);
    #ifdef _WIN32
    DeleteCriticalSection(&cs);
    WSACleanup();
    #endif
    return 0;
}
