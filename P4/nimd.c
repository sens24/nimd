#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <ctype.h>
#include <stdbool.h>

#ifndef DEBUG
#define DEBUG
#endif

#define Q_SIZE 128

volatile int active = 1;


typedef struct {
    int fd;
    char name[74];
    int in_game;
    int player_number;
    int has_opened;
    int begun;
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
    if (p->fd >= 0) {
        close(p->fd);
        p->fd = -1;
    }
    free(p);
}

int player_send(Player *p, const char *message) {
    int msg = write(p->fd, message, strlen(message));
    if (msg < 0) perror("write");
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
    snprintf(buf, 105, "0|%02d|%s|%s", length, type, body);
    return buf;
}

void player_send_fail(Player *p, const char *reason) {
    char fields[1][128];
    strncpy(fields[0], reason, 128);
    char *temp = player_build("FAIL", fields, 1);
    player_send(p, temp);
    free(temp);
}


int player_receive(Player *p, char *buf, size_t bufsize) {
    int n = read(p->fd, buf, bufsize - 1);
    if (n <= 0) return n;
    buf[n] = '\0';

    if (n < 5) {
        player_send_fail(p, "10 Invalid");
        return -1;
    }

    if (buf[0] != '0' || buf[1] != '|' || !isdigit(buf[2]) || !isdigit(buf[3]) || buf[4] != '|') {
        player_send_fail(p, "10 Invalid");
        return -1;
    }

    int declared_len = (buf[2] - '0') * 10 + (buf[3] - '0');
    int actual_len = n - 5;

    //length mismatch
    if (declared_len != actual_len) {
        //player_send_fail(p, "10 Invalid message length");
        if (declared_len<actual_len){
            //we received more chars, so truncate and do the rest
            buf[declared_len+5]='\0';

        }
        else{
            //message too short
            player_send_fail(p, "10 Invalid");
            return -1;
        }
    }

     char fields[6][128];
     int field_count = player_parse(buf, fields, 6);
 
     if (field_count < 3) {
         player_send_fail(p, "10 Invalid");
         return -1;
    }

     const char *type = fields[2];

     if (strcmp(type, "OPEN") != 0 &&
        strcmp(type, "MOVE") != 0 &&
        strcmp(type, "FAIL")  != 0 &&
        strcmp(type, "NAME") != 0 &&   // server only
        strcmp(type, "PLAY") != 0 &&   // server only
        strcmp(type, "OVER") != 0) {   // server only

        player_send_fail(p, "10 Invalid"); //invalid message type
        return -1;
    }

    return n;
}

int player_receive_open(Player *p) {
    char buf[128];
    int n = player_receive(p, buf, sizeof(buf));
    if (n <= 0) return -1;

    char fields[6][128];
    int count = player_parse(buf, fields, 6);
    if (count != 4 || strcmp(fields[2], "OPEN") != 0) {
        player_send_fail(p, "10 Invalid"); //invalid OPEN
        return -1;
    }

    strncpy(p->name, fields[3], 73);
    p->name[73] = '\0';
    p->has_opened = 1;
    if (strlen(p->name) == 0){
        player_send_fail(p, "10 Invalid");
        return -1;
    }
    return 0;
}


void player_send_wait(Player *p) {
    char *temp = player_build("WAIT", NULL, 0);
    player_send(p, temp);
    free(temp);
}


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
    player_destroy(g->p1);
    player_destroy(g->p2);

    free(g);
}

int game_move(Game *g, int player_num, int pile, int count) {
    if (player_num != g->turn) return 31;
    if (pile < 0 || pile >= 5) return 32;
    if (count <= 0 || count > g->board[pile]) return 33;
    g->board[pile] -= count;
    return 0;
}

int game_over(Game *g) {
    for (int i = 0; i < 5; i++) if (g->board[i] > 0) return 0;
    return 1;
}

Player *waiting_players[Q_SIZE];
int wait_count = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

void *game_start(void *arg) {
    Game *g = (Game *)arg;
    char buf[1024];
    char fields[6][128];

    sprintf(fields[0], "1");
    strcpy(fields[1], g->p2->name);
    char *temp = player_build("NAME", fields, 2);
    player_send(g->p1, temp);
    free(temp);
    sprintf(fields[0], "2");
    strcpy(fields[1], g->p1->name);
    char *temp2 = player_build("NAME", fields, 2);
    player_send(g->p2, temp2);
    free(temp2);
    
    g->p1->begun = 1; //game begun
    g->p2->begun = 1;

    bool ff = false;
    bool p1_connected = true;
    bool p2_connected = true;

    while (!game_over(g)) {
        Player *curr = (g->turn == 1) ? g->p1 : g->p2;
        Player *opp = (g->turn == 1) ? g->p2 : g->p1;
        bool *curr_connected = (g->turn == 1) ? &p1_connected : &p2_connected;
        bool *opp_connected = (g->turn == 1) ? &p2_connected : &p1_connected;

        char state[128];
        snprintf(state, sizeof(state), "%d %d %d %d %d",
                 g->board[0], g->board[1], g->board[2], g->board[3], g->board[4]);

        sprintf(fields[0], "%d", g->turn);
        strcpy(fields[1], state);
        char *msg = player_build("PLAY", fields, 2);
        if (*curr_connected) player_send(curr, msg);
        if (*opp_connected) player_send(opp, msg);
        free(msg);

        // Extra credit: Use select() to monitor both players
        fd_set readfds;
        int max_fd = (curr->fd > opp->fd) ? curr->fd : opp->fd;
        bool move_received = false;

        while (!move_received && !ff) {
            // If both players disconnected, break immediately
            if (!*curr_connected && !*opp_connected) {
                ff = true;
                break;
            }

            FD_ZERO(&readfds);
            if (*curr_connected) FD_SET(curr->fd, &readfds);
            if (*opp_connected) FD_SET(opp->fd, &readfds);

            // If no FDs, break
            int fd_count = (*curr_connected ? 1 : 0) + (*opp_connected ? 1 : 0);
            if (fd_count == 0) {
                ff = true;
                break;
            }

            int ready = select(max_fd + 1, &readfds, NULL, NULL, NULL);
            if (ready < 0) {
                ff = true;
                break;
            }

            // impatient
            if (*opp_connected && FD_ISSET(opp->fd, &readfds)) {
                int n = player_receive(opp, buf, sizeof(buf));
                if (n <= 0) {
                    // Opponent disconnected, current player wins by forfeit
                    close(opp->fd);
                    opp->fd = -1;
                    *opp_connected = false;
                    ff = true;
                    break;
                }

                int count = player_parse(buf, fields, 6);
                if (count >= 3 && strcmp(fields[2], "MOVE") == 0) {
                    // Opponent sent MOVE during current player's turn, impatient
                    player_send_fail(opp, "31 Impatient");
                } else if (count >= 3 && strcmp(fields[2], "OPEN") == 0) {
                    player_send_fail(opp, "23 Already Open");
                    close(opp->fd);
                    opp->fd = -1;
                    *opp_connected = false;
                    ff = true;
                    break;
                } else {
                    player_send_fail(opp, "10 Invalid");
                }
                // Continue waiting for current player's move
            }

            // if current player sent their move
            if (*curr_connected && FD_ISSET(curr->fd, &readfds)) {
                int n = player_receive(curr, buf, sizeof(buf));
                if (n <= 0) {
                    close(curr->fd);
                    curr->fd = -1;
                    *curr_connected = false;
                    ff = true;
                    break;
                }

                int count = player_parse(buf, fields, 6);

                if (count >= 3 && strcmp(fields[2], "OPEN") == 0) {
                    player_send_fail(curr, "23 Already Open");
                    close(curr->fd);
                    curr->fd = -1;
                    *curr_connected = false;
                    ff = true;
                    break;
                }

                if (count != 5 || strcmp(fields[2], "MOVE") != 0) {
                    char *msg = player_build("FAIL", (char[][128]){"10 Invalid"}, 1);
                    player_send(curr, msg);
                    free(msg);
                    close(curr->fd);
                    curr->fd = -1;
                    *curr_connected = false;
                    ff = true;
                    break;
                }

                int pile = atoi(fields[3]);
                int qty = atoi(fields[4]);
                int err = game_move(g, g->turn, pile, qty);
                if (err != 0) {
                    char msg[128];
                    if (err == 31){
                        //impatient
                        sprintf(msg, "31 Impatient");
                    }
                    else if (err == 32){
                        //pile index
                        sprintf(msg, "32 Pile Index");
                    }
                    else if (err == 33){
                        //quantity
                        sprintf(msg, "33 Quantity");
                    }
                    else{
                        //weird error, not supposed to happen
                        sprintf(msg, "%d", err);
                    }
                    char fail_fields[1][128];
                    strcpy(fail_fields[0], msg);
                    char *message = player_build("FAIL", fail_fields, 1);
                    player_send(curr, message);
                    free(message);
                    continue;
                }

                move_received = true;
            }
        }

        if (ff) break;
        g->turn = (g->turn == 1) ? 2 : 1;
    }

    char state[128];
    snprintf(state, sizeof(state), "%d %d %d %d %d",
             g->board[0], g->board[1], g->board[2], g->board[3], g->board[4]);

    sprintf(fields[0], "%d", (g->turn == 1) ? 2 : 1);
    strcpy(fields[1], state);
    strcpy(fields[2], "");
    if (ff == true) {
        strcpy(fields[2], "Forfeit");
    }

    char *msg = player_build("OVER", fields, 3);
    // Only send OVER to players who are still connected
    if (p1_connected) player_send(g->p1, msg);
    if (p2_connected) player_send(g->p2, msg);
    free(msg);

    pthread_mutex_lock(&queue_mutex);
    for (int i = 0; i < wait_count; i++) {
        if (waiting_players[i] == g->p1) {
            for (int j = i + 1; j < wait_count; j++)
                waiting_players[j - 1] = waiting_players[j];
            wait_count--;
            break;
        }
    }
    for (int i = 0; i < wait_count; i++) {
        if (waiting_players[i] == g->p2) {
            for (int j = i + 1; j < wait_count; j++)
                waiting_players[j - 1] = waiting_players[j];
            wait_count--;
            break;
        }
    }
    pthread_mutex_unlock(&queue_mutex);

    game_destroy(g);
    return NULL;
}


void handler(int sn) {
    (void)sn;
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
int count_players_in_queue() {
    int count = 0;
    for (int i = 0; i < wait_count; i++)
        if (waiting_players[i]->in_game == 0) count++;
    return count;
}

int first_player_in_queue() {
    int count = 0;
    for (int i = 0; i < wait_count; i++) {
        if (waiting_players[i]->in_game == 0) {
            count++;
            if (count==1){
                return i;
            }
        }
    }
    return -1;
}
int second_player_in_queue() {
    int count = 0;
    for (int i = 0; i < wait_count; i++) {
        if (waiting_players[i]->in_game == 0) {
            count++;
            if (count==2){
                return i;
            }
        }
    }
    return -1;
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


    pthread_mutex_lock(&queue_mutex);
    if (wait_count < Q_SIZE) waiting_players[wait_count++] = p;
    else {
        pthread_mutex_unlock(&queue_mutex);
        player_send_fail(p, "Server full");
        player_destroy(p);
        return NULL;
    }

    int queue_count = count_players_in_queue();

    if (queue_count >= 2) {

        Player *p1 = waiting_players[first_player_in_queue()];
        Player *p2 = waiting_players[second_player_in_queue()];
        // do NOT remove from queue yet
        pthread_mutex_unlock(&queue_mutex);

        Game *g = game_create(p1, p2);
        pthread_t tid;
        pthread_create(&tid, NULL, game_start, g);
        pthread_detach(tid);
    } else {
        pthread_mutex_unlock(&queue_mutex);
    }

    // Extra credit: Monitor socket while waiting for game to start
    while (!p->in_game) {
        fd_set readfds;
        struct timeval tv;
        FD_ZERO(&readfds);
        FD_SET(p->fd, &readfds);

        // Use timeout so we can periodically check in_game flag
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // 100ms timeout

        int ready = select(p->fd + 1, &readfds, NULL, NULL, &tv);

        if (ready > 0 && FD_ISSET(p->fd, &readfds)) {
            // Player sent something while waiting - check what it is
            char buf[128];
            int n = player_receive(p, buf, sizeof(buf));
            if (n <= 0) {
                // Player disconnected while waiting - remove from queue
                pthread_mutex_lock(&queue_mutex);
                for (int i = 0; i < wait_count; i++) {
                    if (waiting_players[i] == p) {
                        for (int j = i + 1; j < wait_count; j++)
                            waiting_players[j - 1] = waiting_players[j];
                        wait_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&queue_mutex);
                player_destroy(p);
                return NULL;
            }

            char fields[6][128];
            int count = player_parse(buf, fields, 6);

            if (count >= 3 && strcmp(fields[2], "MOVE") == 0) {
                // MOVE sent before game starts
                player_send_fail(p, "24 Not Playing");
                pthread_mutex_lock(&queue_mutex);
                for (int i = 0; i < wait_count; i++) {
                    if (waiting_players[i] == p) {
                        for (int j = i + 1; j < wait_count; j++)
                            waiting_players[j - 1] = waiting_players[j];
                        wait_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&queue_mutex);
                player_destroy(p);
                return NULL;
            } else if (count >= 3 && strcmp(fields[2], "OPEN") == 0) {
                // OPEN sent second time
                player_send_fail(p, "23 Already Open");
                pthread_mutex_lock(&queue_mutex);
                for (int i = 0; i < wait_count; i++) {
                    if (waiting_players[i] == p) {
                        for (int j = i + 1; j < wait_count; j++)
                            waiting_players[j - 1] = waiting_players[j];
                        wait_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&queue_mutex);
                player_destroy(p);
                return NULL;
            } else {
                // Some other invalid message
                player_send_fail(p, "10 Invalid");
            }
        }
    }

    // Game has started - game_start thread has taken over
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
