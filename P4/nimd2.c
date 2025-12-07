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

#include "player.h"
#include "game.h"

#ifndef DEBUG
#define DEBUG
#endif

#define Q_SIZE 8

volatile int active = 1; // indication if server should keep running

Player *waiting_players[Q_SIZE];
int wait_count = 0;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;

void handler(int sn) {
    active = 0; // server should stop running
}

// no reap() needed for threads
void install_handlers() {
    struct sigaction act;
    act.sa_handler = handler;
    sigemptyset(&act.sa_mask);
    sigaddset(&act.sa_mask, SIGINT);
    sigaddset(&act.sa_mask, SIGHUP);
    sigaddset(&act.sa_mask, SIGTERM);

    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGTERM, &act, NULL);
}

// check if a name already exists in waiting queue
int name_exists(const char *name) {
    for (int i = 0; i < wait_count; i++) {
        if (strcmp(waiting_players[i]->name, name) == 0) return 1;
    }
    return 0; // TODO: also check active games if needed
}

void *client_thread(void *arg) { 
    int client = *(int *)arg;
    free(arg);

    Player *p = player_create(client);

    // receive OPEN message from client
    if (player_receive_open(p) < 0) {
        player_destroy(p);
        return NULL;
    }

    // validate name length
    if (strlen(p->name) > 72) {
        player_send_fail(p, "21 Long Name");
        player_destroy(p);
        return NULL;
    }

    // check for duplicate name
    pthread_mutex_lock(&queue_mutex);
    if (name_exists(p->name)) {
        pthread_mutex_unlock(&queue_mutex);
        player_send_fail(p, "22 Already Playing");
        player_destroy(p);
        return NULL;
    }
    pthread_mutex_unlock(&queue_mutex);

    // send WAIT message
    player_send_wait(p);

    // add to waiting queue
    pthread_mutex_lock(&queue_mutex);
    if (wait_count < Q_SIZE) {
        waiting_players[wait_count++] = p;
    } else {
        pthread_mutex_unlock(&queue_mutex);
        player_send_fail(p, "Server full");
        player_destroy(p);
        return NULL;
    }

    // start a game if at least 2 players are ready
    if (wait_count >= 2) {
        Player *p1 = waiting_players[0];
        Player *p2 = waiting_players[1];

        // shift remaining players in queue
        for (int i = 2; i < wait_count; i++)
            waiting_players[i - 2] = waiting_players[i];
        wait_count -= 2;

        pthread_mutex_unlock(&queue_mutex);

        // create a new game and run it in its own thread
        Game *g = game_create(p1, p2);
        pthread_t tid;
        pthread_create(&tid, NULL, (void *(*)(void *))start, g);
        pthread_detach(tid);
    } else {
        pthread_mutex_unlock(&queue_mutex);
    }

    return NULL;
}

int main (int argc, char **argv) {

    if (argc != 2) {  // should only have one argument (port) 
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
