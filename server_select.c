/**
 * Concurrent TCP Chat Server - Approach 1: I/O Multiplexing with select()
 * 
 * Model: Single process, single thread.
 * Mechanism: select() monitors multiple file descriptors for readability.
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#define close closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define PORT 9090
#define MAX_CLIENTS 60
#define BUFFER_SIZE 1024

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

    int server_fd, client_sockets[MAX_CLIENTS];
    struct sockaddr_in address;
    fd_set read_fds;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0;
    }

    server_fd = (int)socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket failed");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

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
    printf(" TCP Chat Server [I/O Multiplexing - select()]\n");
    printf(" Listening on port %d...\n", PORT);
    printf("====================================================\n");

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);
        int max_sd = server_fd;

        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &read_fds);
            }
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        int activity = select(max_sd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0 && errno != EINTR) {
            perror("select error");
            break;
        }

        /* 1. New incoming connection */
        if (FD_ISSET(server_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int new_socket = (int)accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
            if (new_socket < 0) {
                perror("accept failed");
            } else {
                printf("[New Connection] Socket fd: %d, IP: %s, Port: %d\n",
                       new_socket, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

                int added = 0;
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockets[i] == 0) {
                        client_sockets[i] = new_socket;
                        added = 1;
                        break;
                    }
                }
                if (!added) {
                    printf("Server full. Rejecting socket %d\n", new_socket);
                    close(new_socket);
                }
            }
        }

        /* 2. I/O on existing client sockets */
        for (int i = 0; i < MAX_CLIENTS; i++) {
            int sd = client_sockets[i];
            if (sd > 0 && FD_ISSET(sd, &read_fds)) {
                char buffer[BUFFER_SIZE];
                int valread = recv(sd, buffer, BUFFER_SIZE - 1, 0);

                if (valread <= 0) {
                    /* Client disconnected */
                    printf("[Disconnected] Socket fd: %d\n", sd);
                    close(sd);
                    client_sockets[i] = 0;
                } else {
                    buffer[valread] = '\0';
                    printf("[Client %d]: %s", sd, buffer);

                    /* Broadcast message to all other clients */
                    char broadcast_msg[BUFFER_SIZE + 32];
                    int msg_len = snprintf(broadcast_msg, sizeof(broadcast_msg),
                                           "[Client %d]: %s", sd, buffer);

                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        int out_sd = client_sockets[j];
                        if (out_sd > 0 && out_sd != sd) {
                            send(out_sd, broadcast_msg, msg_len, 0);
                        }
                    }
                }
            }
        }
    }

    close(server_fd);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

