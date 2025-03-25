// Xiaoyi Dong & Sihao Liu March 20, 2025
/*
Functionality:
Accepts multiple client connections using threads (one thread per client).
Handles login authentication, session creation/joining/leaving, and message broadcasting.
Maintains linked lists for connected clients and active sessions.
Supports user queries to list active users and sessions.

Highlights:
Uses pthread for concurrent client handling and mutex locks for shared list access.
Structured protocol message parsing using sscanf.
Validates login credentials from a predefined list (ken and andy).
Clean session lifecycle management with automatic removal of empty sessions.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
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

typedef struct {
    char id[MAX_NAME];
    char password[MAX_DATA];
} allowedUser_t;

allowedUser_t allowed_users[] = {
    {"ken", "12345"},
    {"andy", "12345"}
};

int allowed_users_count = sizeof(allowed_users)/sizeof(allowedUser_t);

typedef struct client {
    int sockfd;
    char id[MAX_NAME];
    char session[MAX_NAME];
    struct client *next;
} client_t;

client_t *client_list = NULL;
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct session {
    char session_id[MAX_NAME];
    struct session *next;
} session_t;

session_t *session_list = NULL;
pthread_mutex_t sessions_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handle_client(void *arg);
int send_message_to_client(int sockfd, struct message *msg);
int read_line(int sockfd, char *buffer, int max_len);
void process_message(client_t *client, struct message *msg);
void broadcast_message(const char *session_id, struct message *msg, const char *sender);
int is_valid_user(const char *id, const char *password);
int is_already_logged_in(const char *id);
void add_client(client_t *client);
void remove_client(client_t *client);
session_t *find_session(const char *session_id);
void add_session(const char *session_id);
void remove_session_if_empty(const char *session_id);
void send_client_list(int sockfd, const char *client_id);

int send_message_to_client(int sockfd, struct message *msg) {
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

int is_valid_user(const char *id, const char *password) {
    for (int i = 0; i < allowed_users_count; i++) {
        if(strcmp(allowed_users[i].id, id) == 0 &&
           strcmp(allowed_users[i].password, password) == 0)
            return 1;
    }
    return 0;
}

int is_already_logged_in(const char *id) {
    pthread_mutex_lock(&clients_mutex);
    client_t *cur = client_list;
    while(cur) {
        if(strcmp(cur->id, id) == 0) {
            pthread_mutex_unlock(&clients_mutex);
            return 1;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
    return 0;
}

void add_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    client->next = client_list;
    client_list = client;
    pthread_mutex_unlock(&clients_mutex);
}

void remove_client(client_t *client) {
    pthread_mutex_lock(&clients_mutex);
    client_t **cur = &client_list;
    while(*cur) {
        if(*cur == client) {
            *cur = client->next;
            break;
        }
        cur = &((*cur)->next);
    }
    pthread_mutex_unlock(&clients_mutex);
}

session_t *find_session(const char *session_id) {
    pthread_mutex_lock(&sessions_mutex);
    session_t *cur = session_list;
    while(cur) {
        if(strcmp(cur->session_id, session_id) == 0) {
            pthread_mutex_unlock(&sessions_mutex);
            return cur;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&sessions_mutex);
    return NULL;
}

void add_session(const char *session_id) {
    pthread_mutex_lock(&sessions_mutex);
    session_t *new_session = malloc(sizeof(session_t));
    strncpy(new_session->session_id, session_id, MAX_NAME);
    new_session->next = session_list;
    session_list = new_session;
    pthread_mutex_unlock(&sessions_mutex);
}

int session_has_clients(const char *session_id) {
    int found = 0;
    pthread_mutex_lock(&clients_mutex);
    client_t *cur = client_list;
    while(cur) {
        if(strcmp(cur->session, session_id) == 0) {
            found = 1;
            break;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
    return found;
}

void remove_session_if_empty(const char *session_id) {
    if(session_has_clients(session_id))
        return;
    pthread_mutex_lock(&sessions_mutex);
    session_t **cur = &session_list;
    while(*cur) {
        if(strcmp((*cur)->session_id, session_id) == 0) {
            session_t *tmp = *cur;
            *cur = (*cur)->next;
            free(tmp);
            break;
        }
        cur = &((*cur)->next);
    }
    pthread_mutex_unlock(&sessions_mutex);
}

void broadcast_message(const char *session_id, struct message *msg, const char *sender) {
    pthread_mutex_lock(&clients_mutex);
    client_t *cur = client_list;
    while(cur) {
        if(strcmp(cur->session, session_id) == 0 && strcmp(cur->id, sender) != 0) {
            send_message_to_client(cur->sockfd, msg);
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&clients_mutex);
}

void process_message(client_t *client, struct message *msg) {
    struct message reply;
    memset(&reply, 0, sizeof(reply));
    strncpy((char*)reply.source, "server", MAX_NAME);

    switch(msg->type) {
        case LOGIN: {
            if(is_valid_user((char*)msg->source, (char*)msg->data) && !is_already_logged_in((char*)msg->source)) {
                strncpy(client->id, (char*)msg->source, MAX_NAME);
                reply.type = LO_ACK;
                snprintf((char*)reply.data, MAX_DATA, "Login successful");
            } else {
                reply.type = LO_NAK;
                snprintf((char*)reply.data, MAX_DATA, "Invalid credentials or already logged in");
            }
            reply.size = strlen((char*)reply.data);
            send_message_to_client(client->sockfd, &reply);
            break;
        }
        case EXIT: {
            if(strlen(client->session) > 0) {
                memset(client->session, 0, MAX_NAME);
                remove_session_if_empty(client->session);
            }
            reply.type = EXIT;
            send_message_to_client(client->sockfd, &reply);
            break;
        }
        case JOIN: {
            if(find_session((char*)msg->data) == NULL) {
                reply.type = JN_NAK;
                snprintf((char*)reply.data, MAX_DATA, "Session does not exist");
            } else if(strlen(client->session) != 0) {
                reply.type = JN_NAK;
                snprintf((char*)reply.data, MAX_DATA, "Already in a session");
            } else {
                strncpy(client->session, (char*)msg->data, MAX_NAME);
                reply.type = JN_ACK;
                snprintf((char*)reply.data, MAX_DATA, "Joined session");
            }
            reply.size = strlen((char*)reply.data);
            send_message_to_client(client->sockfd, &reply);
            break;
        }
        case NEW_SESS: {
            if(strlen(client->session) != 0) {
                reply.type = NS_ACK;
                snprintf((char*)reply.data, MAX_DATA, "Already in a session");
            } else if(find_session((char*)msg->data) != NULL) {
                reply.type = NS_ACK;
                snprintf((char*)reply.data, MAX_DATA, "Session already exists");
            } else {
                add_session((char*)msg->data);
                strncpy(client->session, (char*)msg->data, MAX_NAME);
                reply.type = NS_ACK;
                snprintf((char*)reply.data, MAX_DATA, "Session created");
            }
            reply.size = strlen((char*)reply.data);
            send_message_to_client(client->sockfd, &reply);
            break;
        }
        case LEAVE_SESS: {
            if(strlen(client->session) == 0) {
                snprintf((char*)reply.data, MAX_DATA, "Not in a session");
            } else {
                snprintf((char*)reply.data, MAX_DATA, "Left session");
                memset(client->session, 0, MAX_NAME);
                remove_session_if_empty((char*)msg->data);
            }
            reply.type = LEAVE_SESS;
            reply.size = strlen((char*)reply.data);
            send_message_to_client(client->sockfd, &reply);
            break;
        }
        case MESSAGE: {
            if(strlen(client->session) > 0) {
                broadcast_message(client->session, msg, client->id);
            }
            break;
        }
        case QUERY: {
            char list[MAX_DATA] = "";
            pthread_mutex_lock(&clients_mutex);
            client_t *cur = client_list;
            strcat(list, "Clients: ");
            while(cur) {
                char line[128];
                snprintf(line, sizeof(line), "%s (session: %s), ", cur->id, (strlen(cur->session) ? cur->session : "None"));
                strcat(list, line);
                cur = cur->next;
            }
            pthread_mutex_unlock(&clients_mutex);

            pthread_mutex_lock(&sessions_mutex);
            session_t *sess = session_list;
            strcat(list, " Sessions: ");
            while(sess) {
                strcat(list, sess->session_id);
                strcat(list, ", ");
                sess = sess->next;
            }
            pthread_mutex_unlock(&sessions_mutex);

            reply.type = QU_ACK;
            strncpy((char*)reply.data, list, MAX_DATA);
            reply.size = strlen((char*)reply.data);
            send_message_to_client(client->sockfd, &reply);
            break;
        }
            break;
    }
}

void *handle_client(void *arg) {
    int sockfd = *(int *)arg;
    free(arg);
    char buffer[2048];
    int n;
    
    client_t *client = malloc(sizeof(client_t));
    client->sockfd = sockfd;
    client->id[0] = '\0';
    client->session[0] = '\0';
    client->next = NULL;
    
    add_client(client);

    while ((n = read_line(sockfd, buffer, sizeof(buffer))) > 0) {
        struct message msg;
        memset(&msg, 0, sizeof(msg));
        int fields = sscanf(buffer, "%u:%u:%[^:]:%[^\n]", &msg.type, &msg.size, msg.source, msg.data);
        if(fields < 3) {
            fprintf(stderr, "Malformed message: %s\n", buffer);
            continue;
        }
        process_message(client, &msg);
        if(msg.type == EXIT)
            break;
    }
    close(sockfd);
    remove_client(client);
    free(client);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <TCP port number>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    int server_sock, *new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    if((server_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    
    if(bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    if(listen(server_sock, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", port);
    
    while(1) {
        int client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if(client_sock < 0) {
            perror("Accept failed");
            continue;
        }
        printf("Accepted connection from %s:%d\n",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        new_sock = malloc(sizeof(int));
        *new_sock = client_sock;
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, (void*)new_sock);
        pthread_detach(tid);
    }
    
    close(server_sock);
    return 0;
}

