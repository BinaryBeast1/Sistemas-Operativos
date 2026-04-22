#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

// --- DEFINICIONES DE COMUNICACIÓN ---
#define REQ_FIFO "/tmp/toadd_req.fifo"
#define RES_FIFO "/tmp/toadd_res.fifo"

typedef enum { CMD_START, CMD_STOP, CMD_PS, CMD_KILL, CMD_STATUS, CMD_ZOMBIE } CommandType;

typedef struct {
    CommandType type;
    int iid;
    char bin_path[256];
} Request;

typedef struct {
    char payload[4096];
} Response;
// ------------------------------------

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <comando> [argumentos]\n", argv[0]);
        return 1;
    }

    Request req;
    memset(&req, 0, sizeof(Request));

    // Empaquetar la petición
    if (strcmp(argv[1], "start") == 0 && argc == 3) {
        req.type = CMD_START;
        strncpy(req.bin_path, argv[2], sizeof(req.bin_path) - 1);
    } else if (strcmp(argv[1], "ps") == 0) {
        req.type = CMD_PS;
    } else if (strcmp(argv[1], "stop") == 0 && argc == 3) {
        req.type = CMD_STOP;
        req.iid = atoi(argv[2]);
    } else if (strcmp(argv[1], "kill") == 0 && argc == 3) {
        req.type = CMD_KILL;
        req.iid = atoi(argv[2]);
    } else if (strcmp(argv[1], "status") == 0 && argc == 3) {
        req.type = CMD_STATUS;
        req.iid = atoi(argv[2]);
    } else if (strcmp(argv[1], "zombie") == 0) {
        req.type = CMD_ZOMBIE;
    } else {
        fprintf(stderr, "Comando no reconocido o argumentos invalidos.\n");
        return 1;
    }

    // Enviar al Demonio
    int req_fd = open(REQ_FIFO, O_WRONLY);
    if (req_fd == -1) {
        fprintf(stderr, "Error: toadd no esta corriendo.\n");
        return 1;
    }
    write(req_fd, &req, sizeof(Request));
    close(req_fd);

    // Leer respuesta
    int res_fd = open(RES_FIFO, O_RDONLY);
    Response res;
    read(res_fd, &res, sizeof(Response));
    close(res_fd);

    // Imprimir
    printf("%s", res.payload);

    return 0;
}