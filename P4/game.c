#include <stdlib.h>
#include <string.h>
#include "player.c"

//file that handles game logic
typedef struct {
    Player *p1;
    Player *p2;
    int board[5];      //for simplicity, 5 piles with 1, 3, 5, 7, 9 stones             
    int turn;                    
} Game;

Game *create(Player *p1, Player *p2) { //creates game board 
    Game *g = malloc(sizeof(Game));
    g->p1 = p1;
    g->p2 = p2;
    g->turn = 1;

    g->board[0] = 1;
    g->board[1] = 3;
    g->board[2] = 5;
    g->board[3] = 7;
    g->board[4] = 9; //sets board up

    p1->in_game = 1;
    p2->in_game = 1;

    return g;
}

void destroy(Game *g) { //game ends, free memory
    if (!g) return;
    g->p1->in_game = 0;
    g->p2->in_game = 0;
    free(g);
}

int move(Game *g, int player_num, int pile, int count) { //player makes a move
    if (pile<0 || pile >= 5) return 32; //PILE INDEX
    if (count<=0 || count > g->board[pile]) return 33; //quantity
    g->board[pile] -= count;
    return 0; 
}

int check_if_game_over(Game *g) {
    for (int i = 0; i<5; i++) {
        if (g->board[i] > 0) return 0;
    }
    return 1; //game over if all piles have 0
}

void *start (void *arg) { //main game loop
    Game *g = (Game *)arg;
    char buf[1024];
    char fields[5][128];

    //opponents and player designation
    sprintf(fields[0], "1");
    strcpy(fields[1], g->p2->name);
    send(g->p2, build("NAME", fields, 2));

    sprintf(fields[0], "2");
    strcpy(fields[1], g->p1->name);
    send(g->p2, build("NAME", fields, 2));

    while (!check_if_game_over(g)) {
        Player *curr;
        Player *opp;
        if (g->turn == 1) {
            curr = g->p1;
            opp = g->p2;
        } else {
            curr = g->p2;
            opp = g->p1; 
        }


        char state[128];
        snprintf(state, sizeof(state), "%d %d %d %d %d", 
        g->board[0], g->board[1], g->board[2], g->board[3], g->board[4]); //display board

        sprintf(fields[0], "%d", g->turn);
        strcpy(fields[1], state);
        send(curr, build("PLAY", fields, 2));
        send(opp, build("PLAY", fields, 2));

        int n = receive(curr, buf, sizeof(buf));
        if (n<=0) { //opp wins (forfeit)
            strcpy(fields[0], (g->turn == 1) ? "2" : "1");
            strcpy(fields[1], state);
            strcpy(fields[2], "game over by forfeit");
            send(opp, build("OVER", fields, 3)); //sends the OVER type 
            return;
        }

        int count = parse(buf, fields, 5);
        if (count < 4 || strcmp(fields[2], "MOVE")!=0) { //error handling, if the type isn't MOVE or if there aren't
            send(curr, build("FAIL", (char[][128]) {"10 Invalid"}, 1)); //the correct number of fields
            continue;
        }

        int pile = atoi(fields[3]);
        int quantity = atoi(fields[4]);
        int error = move(g, g->turn, pile, quantity); //makes the move using the parsed message

        if (error !=0) {
            char msg[128];
            sprintf(msg, "%d", error);
            send(curr, build("FAIL", (char[][128]) {msg}, 1)); //error message
            continue;
        }

        g->turn = (g->turn==1) ? 2:1; //swaps turn (if turn==1, then it is player 2's turn)

    }

    //game over
    char state[128];
    snprintf(state, sizeof(state), "%d %d %d %d %d", 
        g->board[0], g->board[1], g->board[2], g->board[3], g->board[4]);

    sprintf(fields[0], "%d", (g->turn == 1) ? 2 : 1); //winner
    strcpy(fields[1], state);
    strcpy(fields[2], "");

    send(g->p1, build_message("OVER", fields, 3));
    send(g->p2, build_message("OVER", fields, 3)); //sends OVER message to both
    return NULL;
}

