// Xiaoyi Dong & Sihao Liu March 6, 2025
/*
Functionality:
Sends a file to the server over UDP.
Splits the file into fragments (max 1000 bytes per fragment).
Initiates a handshake by sending "ftp" and expects "yes" to proceed.
Implements timeout handling and retransmission using select().
Supports configurable maximum retry attempts (MAX_RETRIES) with a placeholder for infinite retry toggle (if (0)).

Highlights:
Custom packet format: <total_frag>:<frag_no>:<size>:<filename>: + binary file data.
Measures RTT using gettimeofday().
Designed for extensibility with clearly marked sections for timeout strategy adjustment.
Robust but minimalistic logic focusing on core file transfer functionality.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>

#define BUFFER_SIZE     1024
#define MAX_PACKET_LEN  1100

/////////////////////////////////////////////
#define MAX_RETRIES     300      // Max retry
/////////////////////////////////////////////

long long current_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <server IP> <server port>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] socket creation failed");
        return 1;
    }
    printf("[DEBUG] Socket created successfully. sockfd=%d\n", sockfd);

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("[ERROR] inet_pton failed");
        close(sockfd);
        return 1;
    }

    printf("[DEBUG] Ready to send to server: %s:%d\n", server_ip, port);

    char buffer[BUFFER_SIZE];
    printf("Enter a command (ftp <filename>): ");
    if (!fgets(buffer, BUFFER_SIZE, stdin)) {
        fprintf(stderr, "[ERROR] Reading user input failed\n");
        close(sockfd);
        return 1;
    }

    buffer[strcspn(buffer, "\n")] = '\0';

    if (strncmp(buffer, "ftp ", 4) != 0) {
        fprintf(stderr, "[ERROR] Invalid command. Must be 'ftp <filename>'.\n");
        close(sockfd);
        return 1;
    }

    char file_name[BUFFER_SIZE];
    strcpy(file_name, buffer + 4);

    struct stat file_stat;
    if (stat(file_name, &file_stat) != 0) {
        fprintf(stderr, "[ERROR] File '%s' does not exist.\n", file_name);
        close(sockfd);
        return 1;
    }
    printf("[DEBUG] File '%s' found, size=%ld bytes.\n", file_name, file_stat.st_size);

    strcpy(buffer, "ftp");

    long long t_send = current_timestamp_ms();
    socklen_t addr_len = sizeof(server_addr);

    int sent = sendto(sockfd, buffer, strlen(buffer), 0,
                      (struct sockaddr*)&server_addr, addr_len);
    if (sent < 0) {
        perror("[ERROR] sendto (handshake) failed");
        close(sockfd);
        return 1;
    }
    printf("[DEBUG] Sent handshake 'ftp' to server.\n");

    int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                     (struct sockaddr*)&server_addr, &addr_len);
    if (n < 0) {
        perror("[ERROR] recvfrom (handshake) failed");
        close(sockfd);
        return 1;
    }
    buffer[n] = '\0';

    long long t_recv = current_timestamp_ms();
    long long rtt = t_recv - t_send;
    printf("[DEBUG] Server handshake response: '%s'\n", buffer);
    printf("[DEBUG] RTT = %lld ms\n", rtt);

    if (strcmp(buffer, "yes") != 0) {
        fprintf(stderr, "[DEBUG] Server did not respond 'yes'. Exiting.\n");
        close(sockfd);
        return 1;
    }
    printf("A file transfer can start.\n");

    FILE *fp = fopen(file_name, "rb");
    if (!fp) {
        perror("[ERROR] fopen failed");
        close(sockfd);
        return 1;
    }

    long file_size = file_stat.st_size;
    unsigned int total_frag = (file_size + 999) / 1000;
    printf("[DEBUG] total_frag = %u\n", total_frag);

    for (unsigned int frag_no = 1; frag_no <= total_frag; frag_no++) {
        char data_buf[1000];
        int read_size = fread(data_buf, 1, 1000, fp);

        char header[256];
        sprintf(header, "%u:%u:%u:%s:", total_frag, frag_no, read_size, file_name);

        char send_buf[MAX_PACKET_LEN];
        int header_len = strlen(header);

        memcpy(send_buf, header, header_len);
        memcpy(send_buf + header_len, data_buf, read_size);

        int packet_len = header_len + read_size;

//////////////////////////////////////////////////////////////////////////////////////////
        int attempts = 0;
        while (attempts < MAX_RETRIES) {
            int bytes_sent = sendto(sockfd, send_buf, packet_len, 0,
                                    (struct sockaddr*)&server_addr, addr_len);
            if (bytes_sent < 0) {
                perror("[ERROR] sendto (fragment) failed");
            }

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sockfd, &fds);

            struct timeval tv;
            tv.tv_sec = 2;      // Waitting time
            tv.tv_usec = 0;

            int rv = select(sockfd + 1, &fds, NULL, NULL, &tv);
            if (rv == 0) {
                printf("Timeout waiting for ACK of frag #%u, retransmit\n", frag_no);
                attempts++;
                if (0) {      // attempts >= MAX_RETRIES for set a max retry time, 0 for infinity retry
                    printf("[DEBUG] Max retries reached for frag #%u. Exiting file transfer.\n", frag_no);
                    fclose(fp);
                    close(sockfd);
                    return 1;
                }
                continue;
            }

            char ack_buf[64];
            int ack_len = recvfrom(sockfd, ack_buf, sizeof(ack_buf)-1, 0,
                                   (struct sockaddr*)&server_addr, &addr_len);
            if (ack_len > 0) {
                ack_buf[ack_len] = '\0';
                if (strncmp(ack_buf, "ACK:", 4) == 0) {
                    unsigned int ack_no = atoi(ack_buf + 4);
                    if (ack_no == frag_no) {
                        printf("[DEBUG] Received ACK for frag #%u\n", frag_no);
                        break;
                    }
                }
            }
        }
//////////////////////////////////////////////////////////////////////////////////////////

    }

    fclose(fp);
    printf("[DEBUG] File transfer completed: sent %u fragments.\n", total_frag);

    close(sockfd);
    return 0;
}


