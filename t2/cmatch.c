#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

//  Configuración Global 
int N_PLAYERS = 0;
int K_BOARDS = 0;
int K_ELO = 32;
int MAX_ELO_DIFF = 0;
int TURN_DELAY_MS = 0;
float REENTER_PROBABILITY = 0.0;
char SNAPSHOT_PATH[256] = "";

typedef enum { STATE_WAITING, STATE_PLAYING, STATE_DONE } PlayerState;
typedef enum { BOARD_EMPTY, BOARD_READY, BOARD_PLAYING } BoardState;

typedef struct {
    int id;
    double elo;
    int wins;
    int losses;
    int draws;
    time_t waiting_since;
    PlayerState state;
    int current_board;
} Player;

typedef struct {
    int id;
    int p1_id;
    int p2_id;
    int board_matrix[9];
    BoardState state;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int result; // 1 = p1 gana, 2 = p2 gana, 0 = empate
} Board;

Player *players;
Board *boards;

// Sincronización Global
pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t match_cond = PTHREAD_COND_INITIALIZER;
volatile sig_atomic_t keep_running = 1;
int active_matches = 0;

//  Carga de Configuracion
void load_env(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) { perror("Error al abrir archivo de configuracion .env"); exit(1); }
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        sscanf(line, "N_PLAYERS=%d", &N_PLAYERS);
        sscanf(line, "K_BOARDS=%d", &K_BOARDS);
        sscanf(line, "K_ELO=%d", &K_ELO);
        sscanf(line, "MAX_ELO_DIFF=%d", &MAX_ELO_DIFF);
        sscanf(line, "TURN_DELAY_MS=%d", &TURN_DELAY_MS);
        sscanf(line, "REENTER_PROBABILITY=%f", &REENTER_PROBABILITY);
        sscanf(line, "SNAPSHOT_PATH=%s", SNAPSHOT_PATH);
    }
    fclose(file);
}

// Manejo de Señales
void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
    pthread_cond_broadcast(&match_cond);
    for(int i = 0; i < K_BOARDS; i++) {
        pthread_cond_broadcast(&boards[i].cond);
    }
}

// Lógica del Gato
int check_win(int b[9]) {
    int win_lines[8][3] = {
        {0,1,2}, {3,4,5}, {6,7,8}, // Filas
        {0,3,6}, {1,4,7}, {2,5,8}, // Columnas
        {0,4,8}, {2,4,6}           // Diagonales
    };
    for(int i = 0; i < 8; i++) {
        if(b[win_lines[i][0]] != -1 &&
           b[win_lines[i][0]] == b[win_lines[i][1]] &&
           b[win_lines[i][1]] == b[win_lines[i][2]]) {
            return b[win_lines[i][0]];
        }
    }
    return -1;
}

void play_tic_tac_toe(Board *b) {
    memset(b->board_matrix, -1, sizeof(int)*9);
    int turn = 0;
    int moves = 0;
    b->result = 0;

    while(moves < 9 && keep_running) {
        usleep(TURN_DELAY_MS * 1000); 
        
        pthread_mutex_lock(&b->mutex);
        int move;
        do { move = rand() % 9; } while(b->board_matrix[move] != -1);
        
        b->board_matrix[move] = turn;
        moves++;
        
        int winner = check_win(b->board_matrix);
        pthread_mutex_unlock(&b->mutex);

        if(winner != -1) {
            b->result = winner == 0 ? 1 : 2;
            return;
        }
        turn = 1 - turn;
    }
}

// Actualización de ELO
void update_elo(int p1_id, int p2_id, int result) {
    pthread_mutex_lock(&global_mutex); 
    double eloA = players[p1_id].elo;
    double eloB = players[p2_id].elo;
    
    double ea = 1.0 / (1.0 + pow(10.0, (eloB - eloA) / 400.0));
    double eb = 1.0 / (1.0 + pow(10.0, (eloA - eloB) / 400.0));
    
    double sa = (result == 1) ? 1.0 : ((result == 0) ? 0.5 : 0.0);
    double sb = (result == 2) ? 1.0 : ((result == 0) ? 0.5 : 0.0);
    
    players[p1_id].elo += K_ELO * (sa - ea);
    players[p2_id].elo += K_ELO * (sb - eb);
    
    if (result == 1) { players[p1_id].wins++; players[p2_id].losses++; }
    else if (result == 2) { players[p2_id].wins++; players[p1_id].losses++; }
    else { players[p1_id].draws++; players[p2_id].draws++; }
    
    pthread_mutex_unlock(&global_mutex);
}

// thread de Tablero 
void *board_thread(void *arg) {
    int id = *(int*)arg;
    Board *b = &boards[id];
    
    while(keep_running) {
        pthread_mutex_lock(&b->mutex);
        while(b->state == BOARD_EMPTY && keep_running) {
            pthread_cond_wait(&b->cond, &b->mutex); 
        }
        
        if (!keep_running) {
            pthread_mutex_unlock(&b->mutex);
            break;
        }
        
        b->state = BOARD_PLAYING;
        pthread_mutex_unlock(&b->mutex);

        play_tic_tac_toe(b);
        update_elo(b->p1_id, b->p2_id, b->result);
        
        pthread_mutex_lock(&global_mutex);
        b->state = BOARD_EMPTY;
        active_matches--;
        players[b->p1_id].state = STATE_WAITING;
        players[b->p2_id].state = STATE_WAITING;
        pthread_cond_broadcast(&match_cond); 
        pthread_mutex_unlock(&global_mutex);
        
        pthread_mutex_lock(&b->mutex);
        pthread_cond_broadcast(&b->cond); 
        pthread_mutex_unlock(&b->mutex);
    }
    return NULL;
}

// thread de Jugador
void *player_thread(void *arg) {
    int id = *(int*)arg;
    Player *me = &players[id];
    
    while(keep_running) {
        pthread_mutex_lock(&global_mutex);
        
        if(me->state == STATE_WAITING) {
            me->waiting_since = time(NULL);
            int best_opponent = -1;
            time_t oldest_time = time(NULL) + 1000;
            
            for(int i = 0; i < N_PLAYERS; i++) {
                if(i != id && players[i].state == STATE_WAITING) {
                    if(fabs(me->elo - players[i].elo) <= MAX_ELO_DIFF) {
                        if(players[i].waiting_since < oldest_time) {
                            best_opponent = i;
                            oldest_time = players[i].waiting_since;
                        }
                    }
                }
            }
            
            int board_idx = -1;
            if (best_opponent != -1 && active_matches < K_BOARDS) {
                for(int i = 0; i < K_BOARDS; i++) {
                    if(boards[i].state == BOARD_EMPTY) { board_idx = i; break; }
                }
            }
            
            if(board_idx != -1) {
                me->state = STATE_PLAYING;
                players[best_opponent].state = STATE_PLAYING;
                me->current_board = board_idx;
                players[best_opponent].current_board = board_idx;
                active_matches++;
                
                pthread_mutex_lock(&boards[board_idx].mutex);
                boards[board_idx].p1_id = id;
                boards[board_idx].p2_id = best_opponent;
                boards[board_idx].state = BOARD_READY;
                pthread_cond_signal(&boards[board_idx].cond);
                pthread_mutex_unlock(&boards[board_idx].mutex);
                
                pthread_cond_broadcast(&match_cond);
            } else {
                pthread_cond_wait(&match_cond, &global_mutex); 
            }
        }
        pthread_mutex_unlock(&global_mutex);
        
        if (me->state == STATE_PLAYING && keep_running) {
            Board *b = &boards[me->current_board];
            pthread_mutex_lock(&b->mutex);
            while(b->state != BOARD_EMPTY && keep_running) {
                pthread_cond_wait(&b->cond, &b->mutex);
            }
            pthread_mutex_unlock(&b->mutex);
            
            if (!keep_running) break;

            float r = (float)rand() / (float)RAND_MAX;
            if (r > REENTER_PROBABILITY) {
                me->state = STATE_DONE;
                break;
            }
        }
    }
    return NULL;
}

// Funciones de Monitoreo 
void player_stats(int player_id) {
    if(player_id < 0 || player_id >= N_PLAYERS) {
        printf("Jugador no valido.\n"); return;
    }
    pthread_mutex_lock(&global_mutex);
    printf("Jugador %d | ELO: %.2f | Ganadas: %d | Perdidas: %d | Empates: %d\n", 
           player_id, players[player_id].elo, players[player_id].wins, 
           players[player_id].losses, players[player_id].draws);
    pthread_mutex_unlock(&global_mutex);
}

void current_matches() {
    pthread_mutex_lock(&global_mutex);
    printf("Partidas activas: %d\n", active_matches);
    for(int i = 0; i < K_BOARDS; i++) {
        if(boards[i].state == BOARD_PLAYING || boards[i].state == BOARD_READY) {
            printf(" - Match ID %d: J%d vs J%d\n", i, boards[i].p1_id, boards[i].p2_id);
        }
    }
    pthread_mutex_unlock(&global_mutex);
}

void match_status(int game_id) {
    if(game_id < 0 || game_id >= K_BOARDS) {
        printf("ID de match no valido.\n"); return;
    }
    pthread_mutex_lock(&boards[game_id].mutex);
    printf("Estado del Match ID %d:\n", game_id);
    if(boards[game_id].state == BOARD_EMPTY) {
        printf(" - Estado: Inactivo\n");
    } else {
        printf(" - Jugadores: J%d (X) vs J%d (O)\n", boards[game_id].p1_id, boards[game_id].p2_id);
        printf(" - Tablero:\n");
        for(int i = 0; i < 9; i++) {
            char c = boards[game_id].board_matrix[i] == 0 ? 'X' : (boards[game_id].board_matrix[i] == 1 ? 'O' : '-');
            printf(" %c ", c);
            if((i + 1) % 3 == 0) printf("\n");
        }
    }
    pthread_mutex_unlock(&boards[game_id].mutex);
}

//  Main
int main() {
    srand(time(NULL));
    load_env(".env");
    
    // Atrapar Ctrl+C 
    signal(SIGINT, handle_sigint);
    
    players = calloc(N_PLAYERS, sizeof(Player));
    boards = calloc(K_BOARDS, sizeof(Board));
    
    // Restauración de estado
    FILE *snap_in = fopen(SNAPSHOT_PATH, "rb");
    if(snap_in) {
        fread(players, sizeof(Player), N_PLAYERS, snap_in);
        fclose(snap_in);
        printf("Estado restaurado exitosamente desde '%s'.\n", SNAPSHOT_PATH);
    } else {
        for(int i = 0; i < N_PLAYERS; i++) {
            players[i].id = i;
            players[i].elo = 1200.0;
            players[i].wins = 0; players[i].losses = 0; players[i].draws = 0;
            players[i].state = STATE_WAITING;
        }
    }
    
    for(int i = 0; i < K_BOARDS; i++) {
        boards[i].id = i;
        boards[i].state = BOARD_EMPTY;
        pthread_mutex_init(&boards[i].mutex, NULL);
        pthread_cond_init(&boards[i].cond, NULL);
    }
    
    pthread_t *p_threads = malloc(N_PLAYERS * sizeof(pthread_t));
    pthread_t *b_threads = malloc(K_BOARDS * sizeof(pthread_t));
    int *p_ids = malloc(N_PLAYERS * sizeof(int));
    int *b_ids = malloc(K_BOARDS * sizeof(int));
    
    for(int i = 0; i < K_BOARDS; i++) {
        b_ids[i] = i;
        pthread_create(&b_threads[i], NULL, board_thread, &b_ids[i]);
    }
    for(int i = 0; i < N_PLAYERS; i++) {
        p_ids[i] = i;
        pthread_create(&p_threads[i], NULL, player_thread, &p_ids[i]);
    }
    
    printf("\n--- Sistema de Matchmaking cmatch iniciado ---\n");
    printf("Comandos disponibles:\n - stats <id>\n - matches\n - status <id>\n - salir\n\n");

    char cmd[64];
    while(keep_running) {
        if(fgets(cmd, sizeof(cmd), stdin) != NULL) {
            if(strncmp(cmd, "stats", 5) == 0) {
                int pid = -1; sscanf(cmd, "stats %d", &pid); player_stats(pid);
            } else if(strncmp(cmd, "matches", 7) == 0) {
                current_matches();
            } else if(strncmp(cmd, "status", 6) == 0) {
                int mid = -1; sscanf(cmd, "status %d", &mid); match_status(mid);
            } else if(strncmp(cmd, "salir", 5) == 0 || strncmp(cmd, "exit", 4) == 0) {
                // Activar Graceful Shutdown manualmente
                keep_running = 0;
                pthread_cond_broadcast(&match_cond);
                for(int i = 0; i < K_BOARDS; i++) {
                    pthread_cond_broadcast(&boards[i].cond);
                }
                break; // Romper el ciclo de comandos
            }
        }
    }
    
    printf("\nIniciando Graceful Shutdown. Esperando a que finalicen las partidas en curso...\n");
    
    for(int i = 0; i < N_PLAYERS; i++) pthread_join(p_threads[i], NULL);
    for(int i = 0; i < K_BOARDS; i++) pthread_join(b_threads[i], NULL);
    
    // Guardado de Snapshot
    FILE *snap_out = fopen(SNAPSHOT_PATH, "wb");
    if(snap_out) {
        fwrite(players, sizeof(Player), N_PLAYERS, snap_out);
        fclose(snap_out);
        printf("Estado del sistema guardado con exito en '%s'.\n", SNAPSHOT_PATH);
    } else {
        printf("Error: No se pudo guardar el archivo %s\n", SNAPSHOT_PATH);
    }
    
    free(players); free(boards); free(p_threads); free(b_threads);
    free(p_ids); free(b_ids);
    
    printf("Sistema cerrado limpiamente. Hasta luego!\n");
    return 0;
}