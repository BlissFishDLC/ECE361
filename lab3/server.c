// Xiaoyi Dong & Sihao Liu March 6, 2025
/*
Functionality:
Connects to a TCP server and provides a command-line chat interface.
Supports commands like /login, /logout, /joinsession, /createsession, /list, etc.
Maintains client session state, pending session info, and handles user interactions.
Starts a separate thread to asynchronously receive server messages.

Highlights:
Uses strtok() to parse command arguments, with validation checks.
Structured message format sent as: <type>:<size>:<source>:<data>\n.
Session state managed via global variables with proper resets on logout.
Warning messages standardized using [warning]: prefix for clarity.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUFFER_SIZE     1024
#define MAX_PACKET_LEN  1100

///////////////////////////////////////////////////////////
double uniform_rand() {
    return (double)rand() / RAND_MAX;
}
///////////////////////////////////////////////////////////

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <UDP listen port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("[ERROR] socket creation failed");
        return 1;
    }
    printf("[DEBUG] Server socket created successfully.\n");

    struct sockaddr_in serv_addr, cli_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[ERROR] bind failed");
        close(sockfd);
        return 1;
    }
    printf("[DEBUG] Server bound to port %d.\n", port);

    socklen_t cli_len = sizeof(cli_addr);

    while (1) {
        printf("[DEBUG] Waiting for handshake (ftp)...\n");
        char buffer[BUFFER_SIZE];
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                         (struct sockaddr*)&cli_addr, &cli_len);
        if (n < 0) {
            perror("[ERROR] recvfrom failed");
            continue;
        }
        buffer[n] = '\0';
        printf("[DEBUG] Received message: '%s'\n", buffer);

        if (strcmp(buffer, "ftp") == 0) {
            const char* reply = "yes";
            sendto(sockfd, reply, strlen(reply), 0,
                   (struct sockaddr*)&cli_addr, cli_len);
            printf("[DEBUG] Sent 'yes' to client. Start receiving file...\n");

            FILE *fp = NULL;
            unsigned int total_frag = 0;
            unsigned int received_count = 0;
            char filename[256];

            while (1) {
                char recv_buf[MAX_PACKET_LEN];
                int packet_len = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 0,
                                          (struct sockaddr*)&cli_addr, &cli_len);
                if (packet_len < 0) {
                    perror("[ERROR] recvfrom (fragment) failed");
                    break;
                }

////////////////////////////////////////////////////////////////////////////////////////////////
                // Simulate packet loss
                if (uniform_rand() <= 1e-2) {
                    printf("[DEBUG] Packet lost, simulating network failure.\n");
                    continue;
                }
////////////////////////////////////////////////////////////////////////////////////////////////

                int colon_count = 0;
                int header_end_index = -1;
                for (int i = 0; i < packet_len; i++) {
                    if (recv_buf[i] == ':') {
                        colon_count++;
                        if (colon_count == 4) {
                            header_end_index = i;
                            break;
                        }
                    }
                }
                if (header_end_index < 0) {
                    fprintf(stderr, "[DEBUG] Invalid packet: no 4 colons\n");
                    continue;
                }

                char header[300];
                memcpy(header, recv_buf, header_end_index);
                header[header_end_index] = '\0';

                unsigned int t_frag, frag_no, f_size;
                char fname[200];

                if (sscanf(header, "%u:%u:%u:%s", &t_frag, &frag_no, &f_size, fname) < 4) {
                    fprintf(stderr, "[DEBUG] sscanf parse header failed\n");
                    continue;
                }

                int data_start = header_end_index + 1;
                int data_len = packet_len - data_start;

                if ((unsigned int)data_len != f_size) {
                    fprintf(stderr, "[DEBUG] data size mismatch: data_len=%d, f_size=%u\n",
                            data_len, f_size);
                    continue;
                }

                if (frag_no == 1) {
                    total_frag = t_frag;
                    strcpy(filename, fname);
                    fp = fopen(filename, "wb");
                    if (!fp) {
                        perror("[ERROR] fopen failed");
                        continue;
                    }
                    printf("[DEBUG] Start receiving file '%s' (total %u fragments)\n",
                           filename, total_frag);
                }

                if (fp) {
                    fwrite(recv_buf + data_start, 1, f_size, fp);
                }

                received_count++;

                char ack_msg[64];
                sprintf(ack_msg, "ACK:%u", frag_no);
                sendto(sockfd, ack_msg, strlen(ack_msg), 0,
                       (struct sockaddr*)&cli_addr, cli_len);
                printf("[DEBUG] Sent ACK for fragment #%u\n", frag_no);

                if (frag_no == total_frag) {
                    fclose(fp);
                    printf("[DEBUG] File '%s' received completely (%u fragments).\n",
                           filename, total_frag);
                    break;
                }
            }

            continue;
        } else {
            const char* reply = "no";
            sendto(sockfd, reply, strlen(reply), 0,
                   (struct sockaddr*)&cli_addr, cli_len);
            printf("[DEBUG] Sent 'no' to client.\n");
        }
    }

    close(sockfd);
    return 0;
}

