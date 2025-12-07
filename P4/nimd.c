#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

#ifndef DEBUG
#define DEBUG
#endif

#define Q_SIZE 8

volatile int active = 1; // indication if server should keep running

//player
typedef struct {
    int fd;
    char name[73];
    int in_game;
    int player_number;
    int has_opened;
    int begun; //handles error code 24
} Player;

Player *player_create(int fd) {
    Player *p = malloc(sizeof(Player));
    p->fd = fd;
    p->name[0] = '\0';
    p->in_game = 0;
    p->player_number = 0;
    p->has_opened = 0;
    p->begun = 0;
    return p;
}

void player_destroy(Player *p) {
    if (!p) return;
    close(p->fd);
    free(p);
}

int player_send(Player *p, const char *message) {
    int msg = write(p->fd, message, strlen(message));
    if (msg < 0) perror("write");
    return msg;
}

int player_receive(Player *p, char *buf, size_t bufsize) {
    int msg = read(p->fd, buf, bufsize - 1);
    if (msg > 0) buf[msg] = '\0';
    return msg;
}

int player_parse(const char *msg, char fields[][128], int max_fields) {
    int count = 0;
    const char *s = msg;
    while (*s && count < max_fields) {
        const char *e = strchr(s, '|');
        if (!e) return -1;
        size_t len = e - s;
        if (len >= 128) return -1;
        strncpy(fields[count], s, len);
        fields[count][len] = '\0';
        count++;
        s = e + 1;
    }
    return count;
}
 
char *player_build(const char *type, const char fields[][128], int count) {
    char body[105];
    body[0] = '\0';
    size_t remaining = 104;
    if (fields != NULL) {
        for (int i = 0; i < count; i++) {
            size_t fl = strnlen(fields[i], 128);
            if (fl + 1 > remaining)
                break;
            strncat(body, fields[i], remaining);
            remaining -= fl;
            strncat(body, "|", remaining);
            remaining -= 1;
        }
}

    int length = strlen(type) + 1 + strlen(body);

    char *buf = malloc(105);
    if (!buf)
        return NULL;
    
    
    snprintf(buf, 111, "0|%02d|%s|%s", length, type, body);
    return buf;
}

int player_receive_open(Player *p) {
    char buf[128];
    int n = player_receive(p, buf, sizeof(buf));
    if (n <= 0) return -1;

    char fields[6][128];
    int count = player_parse(buf, fields, 6);
    if (count < 2 || strcmp(fields[2], "OPEN") != 0) return -1;
    
    strncpy(p->name, fields[3], 72);
    p->name[72] = '\0';
    p->has_opened = 1; //error handle
    return 0;
}

void player_send_fail(Player *p, const char *reason) {
    char fields[1][128];
    strncpy(fields[0], reason, 128);
    player_send(p, player_build("FAIL", fields, 1));
}

void player_send_wait(Player *p) {
    player_send(p, player_build("WAIT", NULL, 0));
}

//game logic
typedef struct {
    Player *p1;
    Player *p2;
    int board[5];
    int turn;
} Game;

Game *game_create(Player *p1, Player *p2) {
    Game *g = malloc(sizeof(Game));
    g->p1 = p1;
    g->p2 = p2;
    g->turn = 1;
    g->board[0] = 1;
    g->board[1] = 3;
    g->board[2] = 5;
    g->board[3] = 7;
    g->board[4] = 9;
    p1->in_game = 1;
    p2->in_game = 1;
    return g;
}

void game_destroy(Game *g) {
    if (!g) return;
    g->p1->in_game = 0;
    g->p2->in_game = 0;
    free(g);
}

int game_move(Game *g, int player_num, int pile, int count) {
    if (player_num != g->turn) return 31; // impatient
    if (pile < 0 || pile >= 5) return 32; // pile index error
    if (count <= 0 || count > g->board[pile]) return 33; // quantity error
    g->board[pile] -= count;
    return 0;
}

int game_over(Game *g) {
    for (int i = 0; i < 5; i++) if (g->board[i] > 0) return 0;
    return 1;
}

void *game_start(void *arg) {
    Game *g = (Game *)arg;
    char buf[1024];
    char fields[5][128];

    // send NAME messages
    sprintf(fields[0], "1");
    strcpy(fields[1], g->p2->name);
    player_send(g->p1, player_build("NAME", fields, 2));

    sprintf(fields[0], "2");
    strcpy(fields[1], g->p1->name);
    player_send(g->p2, player_build("NAME", fields, 2));

    while (!game_over(g)) {
        Player *curr = (g->turn == 1) ? g->p1 : g->p2;
        Player *opp  = (g->turn == 1) ? g->p2 : g->p1;

        char state[128];
        snprintf(state, sizeof(state), "%d %d %d %d %d",
            g->board[0], g->board[1], g->board[2], g->board[3], g->board[4]);

        sprintf(fields[0], "%d", g->turn);
        strcpy(fields[1], state);
        char *msg = player_build("PLAY", fields, 2);
        player_send(curr, msg);
        player_send(opp, msg);
        free(msg);

        int n = player_receive(curr, buf, sizeof(buf));
        if (n <= 0) { // forfeit
            sprintf(fields[0], "%d", (g->turn == 1) ? 2 : 1);
            strcpy(fields[1], state);
            strcpy(fields[2], "Forfeit");
            char *m = player_build("OVER", fields, 3);
            player_send(opp, m);
            free(m);
            return NULL;
        }

        int count = player_parse(buf, fields, 5);
        if (count >= 3 && strcmp(fields[2], "MOVE") == 0 && !curr->begun) {
            player_send_fail(curr, "24 NOT PLAYING");
            player_destroy(curr);
            return NULL;
        }

        if (count >= 3 && strcmp(fields[2], "OPEN") == 0) {
            player_send_fail(curr, "23 ALREADY OPENED");
            player_destroy(curr);
            return NULL;
        }

        

        if (count < 5 || strcmp(fields[2], "MOVE") != 0) {
            char *msg = player_build("FAIL", (char[][128]){"10 Invalid"}, 1);
            player_send(curr, msg);
            free(msg);
            continue;
        }

        int pile = atoi(fields[3]);
        int qty  = atoi(fields[4]);
        int err  = game_move(g, g->turn, pile, qty);
        if (err != 0) {
            char msg[128];
            sprintf(msg, "%d", err);
            char fields[1][128];
            strcpy(fields[0], msg);
            char *message = player_build("FAIL", fields, 1);
            player_send(curr, message);
            free(message);
            continue;
        }

        g->turn = (g->turn == 1) ? 2 : 1;
    }

    char state[128];
    snprintf(state, sizeof(state), "%d %d %d %d %d",
        g->board[0], g->board[1], g->board[2], g->board[3], g->board[4]);

    sprintf(fields[0], "%d", (g->turn == 1) ? 2 : 1); // winner
    strcpy(fields[1], state);
    strcpy(fields[2], "");


    char *msg = player_build("OVER", fields, 3);
    player_send(g->p1, msg);
    player_send(g->p2, msg);
    free(msg);

    game_destroy(g);
    return NULL;
}

// server logic
Player *waiting_players[Q_SIZE];
int wait_count = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

void handler(int sn) {
     (void) sn;
     active = 0;
     }

void install_handlers() {
    struct sigaction act;
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
}

int name_exists(const char *name) {
    for (int i = 0; i < wait_count; i++)
        if (strcmp(waiting_players[i]->name, name) == 0) return 1;
    return 0;
}

int open_listener(const char *port, int qsize) {
    struct addrinfo hints, *res;
    int listener;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(NULL, port, &hints, &res) != 0) return -1;

    listener = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (listener < 0) return -1;
    int opt = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(listener, res->ai_addr, res->ai_addrlen) < 0) return -1;
    if (listen(listener, qsize) < 0) return -1;
    freeaddrinfo(res);
    return listener;
}

void *client_thread(void *arg) {
    int client = *(int *)arg;
    free(arg);

    Player *p = player_create(client);
    if (player_receive_open(p) < 0) {
        player_destroy(p);
        return NULL;
    }

    if (strlen(p->name) > 72) {
        player_send_fail(p, "21 Long Name");
        player_destroy(p);
        return NULL;
    }

    pthread_mutex_lock(&queue_mutex);
    if (name_exists(p->name)) {
        pthread_mutex_unlock(&queue_mutex);
        player_send_fail(p, "22 Already Playing");
        player_destroy(p);
        return NULL;
    }
    pthread_mutex_unlock(&queue_mutex);

    player_send_wait(p);
    p->begun = 1; //game has begun

    pthread_mutex_lock(&queue_mutex);
    if (wait_count < Q_SIZE) waiting_players[wait_count++] = p;
    else {
        pthread_mutex_unlock(&queue_mutex);
        player_send_fail(p, "Server full");
        player_destroy(p);
        return NULL;
    }

    if (wait_count >= 2) {
        Player *p1 = waiting_players[0];
        Player *p2 = waiting_players[1];
        for (int i = 2; i < wait_count; i++)
            waiting_players[i - 2] = waiting_players[i];
        wait_count -= 2;
        pthread_mutex_unlock(&queue_mutex);

        Game *g = game_create(p1, p2);
        pthread_t tid;
        pthread_create(&tid, NULL, game_start, g);
        pthread_detach(tid);
    } else {
        pthread_mutex_unlock(&queue_mutex);
    }

    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        printf("Specify only the port number\n");
        exit(EXIT_FAILURE);
    }

    install_handlers();
    int listener = open_listener(argv[1], Q_SIZE);
    if (listener < 0) {
        perror("open_listener");
        exit(EXIT_FAILURE);
    }

    printf("Server running on port %s...\n", argv[1]);

    while (active) {
        struct sockaddr_storage remote_host;
        socklen_t len = sizeof(remote_host);

        int client = accept(listener, (struct sockaddr *)&remote_host, &len);
        if (!active) break;
        if (client < 0) {
            if (errno == EINTR) continue;
            continue;
        }

        printf("Client connected.\n");

        pthread_t tid;
        int *c = malloc(sizeof(int));
        *c = client;
        pthread_create(&tid, NULL, client_thread, c);
        pthread_detach(tid);
    }

    printf("Server shutting down.\n");
    shutdown(listener, SHUT_RDWR);
    close(listener);
    return 0;
}
