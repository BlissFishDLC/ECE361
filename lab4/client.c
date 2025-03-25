// Xiaoyi Dong & Sihao Liu March 20, 2025
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
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <errno.h>

#define MAX_NAME 50
#define MAX_DATA 1024

#define LOGIN       1
#define LO_ACK      2
#define LO_NAK      3
#define EXIT        4
#define JOIN        5
#define JN_ACK      6
#define JN_NAK      7
#define LEAVE_SESS  8
#define NEW_SESS    9
#define NS_ACK      10
#define MESSAGE     11
#define QUERY       12
#define QU_ACK      13

struct message {
    unsigned int type;
    unsigned int size;
    unsigned char source[MAX_NAME];
    unsigned char data[MAX_DATA];
};

int sockfd = -1;
pthread_t recv_thread;
int loggedIn = 0;
char current_session[MAX_NAME] = {0};
char pending_session[MAX_NAME] = {0};
char client_id[MAX_NAME] = {0};

void *receive_handler(void *arg);
int send_message(int sockfd, struct message *msg);
int read_line(int sockfd, char *buffer, int max_len);

void handle_login(char *clientID, char *password, char *server_ip, int server_port);
void handle_logout();
void handle_join_session(char *session_id);
void handle_leave_session();
void handle_create_session(char *session_id);
void handle_list();
void handle_quit();

int main() {
    char input[2048];
    while(1) {
        if(fgets(input, sizeof(input), stdin) == NULL)
            continue;
        input[strcspn(input, "\n")] = 0;
        
        if(input[0] == '/') {
            char *command = strtok(input, " ");
            if(strcmp(command, "/login") == 0) {
                char *clientID = strtok(NULL, " ");
                char *password = strtok(NULL, " ");
                char *server_ip = strtok(NULL, " ");
                char *server_port_str = strtok(NULL, " ");
                if(clientID && password && server_ip && server_port_str) {
                    int server_port = atoi(server_port_str);
                    handle_login(clientID, password, server_ip, server_port);
                } else {
                    printf("[warning]: Usage: /login <client ID> <password> <server-IP> <server-port>\n");
                }
            } else if(strcmp(command, "/logout") == 0) {
                if(!loggedIn)
                    printf("[warning]: Not logged in.\n");
                else
                    handle_logout();
            } else if(strcmp(command, "/joinsession") == 0) {
                char *session_id = strtok(NULL, " ");
                if(!loggedIn) {
                    printf("[warning]: You must login first.\n");
                } else if(strlen(current_session) != 0) {
                    printf("[warning]: Already in a session. Leave current session before joining another.\n");
                } else if(session_id) {
                    handle_join_session(session_id);
                } else {
                    printf("[warning]: Usage: /joinsession <session ID>\n");
                }
            } else if(strcmp(command, "/leavesession") == 0) {
                if(!loggedIn)
                    printf("[warning]: You must login first.\n");
                else if(strlen(current_session) == 0)
                    printf("[warning]: Not join any session yet.\n");
                else
                    handle_leave_session();
            } else if(strcmp(command, "/createsession") == 0) {
                char *session_id = strtok(NULL, " ");
                if(!loggedIn) {
                    printf("[warning]: You must login first.\n");
                } else if(strlen(current_session) != 0) {
                    printf("[warning]: Leave current session to create a new one.\n");
                } else if(session_id) {
                    handle_create_session(session_id);
                } else {
                    printf("[warning]: Usage: /createsession <session ID>\n");
                }
            } else if(strcmp(command, "/list") == 0) {
                if(!loggedIn)
                    printf("[warning]: You must login first.\n");
                else
                    handle_list();
            } else if(strcmp(command, "/quit") == 0) {
                handle_quit();
                break;
            } else {
                printf("[warning]: Use one of the following commands:\n"
                       "/logout\n"
                       "/joinsession\n"
                       "/leavesession\n"
                       "/createsession\n"
                       "/list\n"
                       "/quit\n");
            }
        } else {
            if (!loggedIn) {
                printf("[warning]: You must login first.\n");
                continue;
            }
            if(strlen(current_session) == 0) {
                printf("[warning]: Not join any session yet.\n");
                continue;
            }
            struct message msg;
            msg.type = MESSAGE;
            strncpy((char*)msg.source, client_id, MAX_NAME);
            strncpy((char*)msg.data, input, MAX_DATA);
            msg.size = strlen((char*)msg.data);
            if(send_message(sockfd, &msg) < 0) {
                printf("[warning]: send_message failed.\n");
            }
        }
    }
    
    return 0;
}

int send_message(int sockfd, struct message *msg) {
    char buffer[2048];
    int n = snprintf(buffer, sizeof(buffer), "%u:%u:%s:%s\n",
                     msg->type, msg->size, msg->source, msg->data);
    if(n < 0) return -1;
    return send(sockfd, buffer, n, 0);
}

int read_line(int sockfd, char *buffer, int max_len) {
    int total = 0;
    char ch;
    while(total < max_len - 1) {
        int n = recv(sockfd, &ch, 1, 0);
        if(n <= 0)
            return n;
        if(ch == '\n')
            break;
        buffer[total++] = ch;
    }
    buffer[total] = '\0';
    return total;
}

void *receive_handler(void *arg) {
    char buffer[2048];
    while (1) {
        int n = read_line(sockfd, buffer, sizeof(buffer));
        if(n <= 0) {
            printf("Disconnected from server.\n");
            loggedIn = 0;
            break;
        }
        struct message msg;
        int fields = sscanf(buffer, "%u:%u:%[^:]:%[^\n]", 
                            &msg.type, &msg.size, msg.source, msg.data);
        if(fields < 3)
            continue;
        switch(msg.type) {
            case LO_NAK:
                printf("[warning]: %s\n", msg.data);
                break;
            case JN_ACK:
                strncpy(current_session, pending_session, MAX_NAME);
                memset(pending_session, 0, sizeof(pending_session));
                printf("Current session: %s\n", current_session);
                break;
            case JN_NAK:
                printf("[warning]: %s\n", msg.data);
                memset(pending_session, 0, sizeof(pending_session));
                break;
            case NS_ACK:
                if(strcmp((char*)msg.data, "Session created") == 0) {
                    strncpy(current_session, pending_session, MAX_NAME);
                    printf("Current session: %s\n", current_session);
                } else {
                    printf("[warning]: %s\n", msg.data);
                }
                memset(pending_session, 0, sizeof(pending_session));
                break;
            case QU_ACK:
                printf("%s\n", msg.data);
                break;
            case MESSAGE:
                printf("[%s]: %s\n", msg.source, msg.data);
                break;
            default:
                break;
        }
    }
    return NULL;
}

void handle_login(char *clientID, char *password, char *server_ip, int server_port) {
    strncpy(client_id, clientID, MAX_NAME);
    struct sockaddr_in server_addr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd < 0) {
        printf("[warning]: Socket creation failed.\n");
        return;
    }
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if(inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        printf("[warning]: Invalid address.\n");
        return;
    }
    if(connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("[warning]: Connection failed.\n");
        return;
    }
    struct message msg;
    msg.type = LOGIN;
    strncpy((char*)msg.source, client_id, MAX_NAME);
    strncpy((char*)msg.data, password, MAX_DATA);
    msg.size = strlen((char*)msg.data);
    if(send_message(sockfd, &msg) < 0) {
        printf("[warning]: send_message failed.\n");
        return;
    }
    loggedIn = 1;
    pthread_create(&recv_thread, NULL, receive_handler, NULL);
}

void handle_logout() {
    if(!loggedIn)
        return;
    struct message msg;
    msg.type = EXIT;
    strncpy((char*)msg.source, client_id, MAX_NAME);
    msg.data[0] = '\0';
    msg.size = 0;
    send_message(sockfd, &msg);
    loggedIn = 0;
    close(sockfd);
    sockfd = -1;
    memset(current_session, 0, sizeof(current_session));
    memset(pending_session, 0, sizeof(pending_session));
}

void handle_join_session(char *session_id) {
    if(!loggedIn)
        return;
    struct message msg;
    msg.type = JOIN;
    strncpy((char*)msg.source, client_id, MAX_NAME);
    strncpy((char*)msg.data, session_id, MAX_DATA);
    msg.size = strlen((char*)msg.data);
    send_message(sockfd, &msg);
    strncpy(pending_session, session_id, MAX_NAME);
}

void handle_leave_session() {
    if(!loggedIn)
        return;
    if(strlen(current_session) == 0) {
        printf("[warning]: Not join any session yet.\n");
        return;
    }
    struct message msg;
    msg.type = LEAVE_SESS;
    strncpy((char*)msg.source, client_id, MAX_NAME);
    msg.data[0] = '\0';
    msg.size = 0;
    send_message(sockfd, &msg);
    memset(current_session, 0, sizeof(current_session));
}

void handle_create_session(char *session_id) {
    if(!loggedIn)
        return;
    if(strlen(current_session) != 0) {
        printf("[warning]: Leave current session to create a new one.\n");
        return;
    }
    struct message msg;
    msg.type = NEW_SESS;
    strncpy((char*)msg.source, client_id, MAX_NAME);
    strncpy((char*)msg.data, session_id, MAX_DATA);
    msg.size = strlen((char*)msg.data);
    send_message(sockfd, &msg);
    strncpy(pending_session, session_id, MAX_NAME);
}

void handle_list() {
    if(!loggedIn)
        return;
    struct message msg;
    msg.type = QUERY;
    strncpy((char*)msg.source, client_id, MAX_NAME);
    msg.data[0] = '\0';
    msg.size = 0;
    send_message(sockfd, &msg);
}

void handle_quit() {
    if(loggedIn)
        handle_logout();
    exit(0);
}

