/**
 * Concurrent TCP Chat Client
 * 
 * Cross-platform TCP Chat Client for Windows and Linux/WSL.
 * Supports simultaneous sending and receiving using threads (Windows)
 * and select() / threads (Linux).
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SSIZE_T ssize_t;
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

#define DEFAULT_IP "127.0.0.1"
#define DEFAULT_PORT 9090
#define BUFFER_SIZE 1024

static volatile int is_running = 1;

#ifdef _WIN32
static DWORD WINAPI receive_worker(LPVOID arg)
#else
static void *receive_worker(void *arg)
#endif
{
    SOCK_TYPE sock = *(SOCK_TYPE *)arg;
    char buffer[BUFFER_SIZE];

    while (is_running) {
        int bytes = recv(sock, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) {
            if (is_running) {
                printf("\n[Disconnected] Server closed the connection.\n");
            }
            is_running = 0;
            break;
        }
        buffer[bytes] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    setvbuf(stdout, NULL, _IONBF, 0);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }
#endif

    const char *server_ip = (argc >= 2) ? argv[1] : DEFAULT_IP;
    int port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;

    SOCK_TYPE sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        perror("Socket creation failed");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port);

    unsigned long ip_bin = inet_addr(server_ip);
    if (ip_bin == INADDR_NONE) {
        fprintf(stderr, "Invalid IP address: %s\n", server_ip);
        close(sock);
        return 1;
    }
    server_addr.sin_addr.s_addr = ip_bin;

    printf("Connecting to %s:%d...\n", server_ip, port);
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        close(sock);
        return 1;
    }

    printf("====================================================\n");
    printf(" Connected to Chat Server (%s:%d)\n", server_ip, port);
    printf(" Type messages and press Enter. Press Ctrl+C to quit.\n");
    printf("====================================================\n");

#ifdef _WIN32
    HANDLE recv_thread = CreateThread(NULL, 0, receive_worker, &sock, 0, NULL);
#else
    pthread_t recv_tid;
    pthread_create(&recv_tid, NULL, receive_worker, &sock);
#endif

    char input[BUFFER_SIZE];
    while (is_running && fgets(input, sizeof(input), stdin) != NULL) {
        int len = (int)strlen(input);
        if (len == 0) continue;

        int sent = send(sock, input, len, 0);
        if (sent <= 0) {
            printf("\n[Disconnected] Failed to send message.\n");
            break;
        }
    }

    is_running = 0;
    close(sock);

#ifdef _WIN32
    if (recv_thread) {
        WaitForSingleObject(recv_thread, 500);
        CloseHandle(recv_thread);
    }
    WSACleanup();
#else
    pthread_cancel(recv_tid);
    pthread_join(recv_tid, NULL);
#endif

    return 0;
}
