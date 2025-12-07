#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "player.h"

#define BUF_SIZE 1024


Player *player_create(int fd) { //creates and initializes default
    Player *p = malloc(sizeof(Player));
    p->fd = fd;
    p->name[0] = '\0';
    p->in_game = 0;
    p->player_number = 0;  
    return p;
}

void player_destroy(Player *p) { //destroys player and frees memory
    if (!p) return;
    close(p->fd);
    free(p);
}
 
int player_send(Player *p, const char *message) { //writes a message 
    int msg = write(p->fd, message, strlen(message));
    if (msg<0) {
        perror("write");
    }
    return msg;
}

int player_receive(Player *p, char *buf, size_t bufsize) {
    int msg = read(p->fd, buf, bufsize - 1);
    if (msg > 0) buf[msg] = '\0';
    return msg;
}

int player_parse(const char *msg, char fields[][128], int max_fields) {
    int count = 0;
    const char *s = msg;   //start 

    while (*s && count < max_fields) {
        const char *e = strchr(s, '|'); //end, finds the |
        if (!e) return -1; //message incorrectly formatted

        size_t len = e - s;
        if (len >= 128) return -1;   // cannot be over 128 char

        strncpy(fields[count], s, len);
        fields[count][len] = '\0';
        count++;

        s = e + 1;  //moves to next field
    }

    return count;

}



char *player_build(const char *type, const char fields[][128], int count) {
    static char buf[BUF_SIZE];
    char body[BUF_SIZE];
    body[0] = '\0';

    for (int i = 0; i < count; i++) {
        strcat(body, fields[i]);
        strcat(body, "|");
    }

    int length = strlen(type) + 1 + strlen(body); // type + bar + body
    snprintf(buf, sizeof(buf), "0|%02d|%s|%s", length, type, body); //combines into string to send

    return buf;

}

int player_receive_open(Player *p) {
    char buf[128];
    int n = player_receive(p, buf, sizeof(buf));
    if (n <= 0) return -1;

    char fields[5][128];
    int count = player_parse(buf, fields, 5);
    if (count < 2 || strcmp(fields[0], "OPEN") != 0) return -1;

    strncpy(p->name, fields[1], 72);
    p->name[72] = '\0';
    return 0;
}


void player_send_fail(Player *p, const char *reason) {
    char fields[1][128];
    strncpy(fields[0], reason, 128);
    player_send(p, build("FAIL", fields, 1));
}

void player_send_wait(Player *p) {
    char fields[0][128]; 
    player_send(p, build("WAIT", fields, 0));
}
