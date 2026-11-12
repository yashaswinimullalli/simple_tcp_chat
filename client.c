#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SSIZE_T ssize_t;
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFFER_SIZE 1024

static volatile int running = 1;

#ifdef _WIN32
/* Thread that reads from the network and prints to screen */
static DWORD WINAPI recv_thread(LPVOID arg)
{
    int sock = *(int *)arg;
    char buffer[BUFFER_SIZE + 1];
    while (running) {
        ssize_t n = recv(sock, buffer, BUFFER_SIZE, 0);
        if (n <= 0) {
            printf("\nServer disconnected.\n");
            running = 0;
            break;
        }
        buffer[n] = '\0';
        printf("%s", buffer);
        fflush(stdout);
    }
    return 0;
}
#endif

int main(int argc, char *argv[])
{
    #ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "WSAStartup failed.\n");
        return 1;
    }
    #endif

    if (argc != 3) {
        printf("Usage: %s <server-ip> <port>\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in server;
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)atoi(argv[2]));

    unsigned long address = inet_addr(argv[1]);
    if (address == INADDR_NONE && strcmp(argv[1], "255.255.255.255") != 0) {
        fprintf(stderr, "Invalid IPv4 address.\n");
        close(sock); return 1;
    }
    server.sin_addr.s_addr = address;

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("connect"); close(sock); return 1;
    }

    printf("Connected. Type messages and press Enter to send.\n");

#ifdef _WIN32
    /* On Windows: use a thread for receiving, main thread reads stdin */
    HANDLE hThread = CreateThread(NULL, 0, recv_thread, &sock, 0, NULL);
    if (!hThread) {
        fprintf(stderr, "Failed to create receive thread.\n");
        close(sock); return 1;
    }

    char buffer[BUFFER_SIZE];
    while (running) {
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;

        size_t length = strlen(buffer), sent = 0;
        while (sent < length) {
            ssize_t n = send(sock, buffer + sent, length - sent, 0);
            if (n < 0) {
                perror("send"); running = 0; break;
            }
            if (n == 0) { running = 0; break; }
            sent += (size_t)n;
        }
    }

    running = 0;
    WaitForSingleObject(hThread, 1000);
    CloseHandle(hThread);
#else
    /* On Linux: use select() on both stdin and socket */
    while (1) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        int max_fd = (sock > STDIN_FILENO) ? sock : STDIN_FILENO;
        int ready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select"); break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            char buffer[BUFFER_SIZE + 1];
            ssize_t n = recv(sock, buffer, BUFFER_SIZE, 0);
            if (n <= 0) {
                printf("\nServer disconnected.\n");
                break;
            }
            buffer[n] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char buffer[BUFFER_SIZE];
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;

            size_t length = strlen(buffer), sent = 0;
            while (sent < length) {
                ssize_t n = send(sock, buffer + sent, length - sent, 0);
                if (n < 0) {
                    if (errno == EINTR) continue;
                    perror("send"); close(sock); return 1;
                }
                if (n == 0) break;
                sent += (size_t)n;
            }
        }
    }
#endif

    close(sock);
    #ifdef _WIN32
    WSACleanup();
    #endif
    return 0;
}
