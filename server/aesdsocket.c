#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <syslog.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>

#define DATAFILE "/var/tmp/aesdsocketdata"

volatile sig_atomic_t exit_requested = 0;
int global_sockfd = -1;
int global_clientfd = -1;

void signal_handler(int signo) {
    exit_requested = 1;
    // Close sockets if open
    if (global_sockfd != -1) close(global_sockfd);
    if (global_clientfd != -1) close(global_clientfd);
    printf("Caught signal %d, exiting...\n", signo);
}

int open_socket() {
    int sockfd;
    struct sockaddr_in serv_addr;

    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }

    // Zero out the server address structure
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(9000);

    // Bind socket to port 9000
    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }

    // Listen for incoming connections
    if (listen(sockfd, 10) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int listen_socket(int sockfd) {
    openlog("aesdsocket", LOG_PID | LOG_CONS, LOG_USER);
    while (!exit_requested) {
        int clientfd;
        struct sockaddr_in cli_addr;
        socklen_t clilen = sizeof(cli_addr);

        // Accept a new connection
        clientfd = accept(sockfd, (struct sockaddr *)&cli_addr, &clilen);
        if (clientfd < 0) {
            if (exit_requested) break;
            perror("accept");
            closelog();
            return -1;
        }
        global_clientfd = clientfd;

        printf("Accepted connection from %s\n", inet_ntoa(cli_addr.sin_addr));
        syslog(LOG_INFO, "Accepted connection from %s", inet_ntoa(cli_addr.sin_addr));

        // Receive data and append newline-terminated packets to file
        FILE *fp = fopen(DATAFILE, "a");
        if (!fp) {
            syslog(LOG_ERR, "Failed to open %s", DATAFILE);
            close(clientfd);
            global_clientfd = -1;
            continue;
        }

        size_t bufsize = 1024;
        char *recvbuf = malloc(bufsize);
        if (!recvbuf) {
            syslog(LOG_ERR, "malloc failed");
            fclose(fp);
            close(clientfd);
            global_clientfd = -1;
            continue;
        }
        size_t datalen = 0;
        ssize_t n;
        char temp[512];
        while (!exit_requested && (n = recv(clientfd, temp, sizeof(temp), 0)) > 0) {
            size_t temp_offset = 0;
            while (temp_offset < n) {
                // Copy as much as possible to recvbuf
                if (datalen + (n - temp_offset) >= bufsize) {
                    size_t newsize = bufsize * 2;
                    while (datalen + (n - temp_offset) >= newsize) newsize *= 2;
                    char *newbuf = realloc(recvbuf, newsize);
                    if (!newbuf) {
                        syslog(LOG_ERR, "realloc failed");
                        free(recvbuf);
                        fclose(fp);
                        close(clientfd);
                        global_clientfd = -1;
                        goto next_client;
                    }
                    recvbuf = newbuf;
                    bufsize = newsize;
                }
                // Copy data
                size_t copylen = n - temp_offset;
                memcpy(recvbuf + datalen, temp + temp_offset, copylen);
                datalen += copylen;
                temp_offset += copylen;

                // Process all complete packets in buffer
                size_t start = 0;
                for (size_t i = 0; i < datalen; ++i) {
                    if (recvbuf[i] == '\n') {
                        size_t pktlen = i - start + 1;
                        fwrite(recvbuf + start, 1, pktlen, fp);
                        fflush(fp);

                        // After writing a complete packet, return file contents to client
                        FILE *fp2 = fopen(DATAFILE, "r");
                        if (!fp2) {
                            syslog(LOG_ERR, "Failed to open %s for reading", DATAFILE);
                            close(clientfd);
                            global_clientfd = -1;
                            free(recvbuf);
                            fclose(fp);
                            goto next_client;
                        }
                        char sendbuf[1024];
                        ssize_t nn;
                        while ((nn = fread(sendbuf, 1, sizeof(sendbuf), fp2)) > 0) {
                            if (send(clientfd, sendbuf, nn, 0) < 0) {
                                syslog(LOG_ERR, "Failed to send data to client");
                                break;
                            }
                        }
                        fclose(fp2);

                        start = i + 1;
                    }
                }
                // Shift any remaining data to the start of the buffer
                if (start > 0 && start < datalen) {
                    memmove(recvbuf, recvbuf + start, datalen - start);
                    datalen -= start;
                } else if (start == datalen) {
                    datalen = 0;
                }
            }
        }
        // If connection closed and there is leftover data without newline, discard it
        free(recvbuf);
        fclose(fp);

        close(clientfd);
        global_clientfd = -1;
    next_client:
        ;
    }

    closelog();
    return 0;
}

int main(int argc, char *argv[]) {
    int sockfd;
    int daemon_mode = 0;

    // Parse arguments
    if (argc == 2 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    // Register signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    sockfd = open_socket();
    if (sockfd < 0) {
        fprintf(stderr, "Failed to open socket\n");
        return 1;
    }
    global_sockfd = sockfd;

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            close(sockfd);
            return 1;
        }
        if (pid > 0) {
            // Parent exits
            close(sockfd);
            return 0;
        }
        // Child continues: detach from terminal
        if (setsid() < 0) {
            perror("setsid");
            close(sockfd);
            return 1;
        }
        // Redirect standard file descriptors to /dev/null
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }

    // Socket opened successfully
    if (!daemon_mode) printf("Socket opened successfully\n");

    // Listen and handle connections
    listen_socket(sockfd);

    // Close the socket if not already closed
    if (global_sockfd != -1) close(global_sockfd);
    global_sockfd = -1;

    // Remove the data file
    if (remove(DATAFILE) != 0) {
        perror("remove");
    }

    return 0;
}
