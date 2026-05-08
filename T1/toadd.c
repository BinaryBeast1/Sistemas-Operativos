#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

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

#define MAX_PROCESSES 100
#define MAX_RESTARTS 5 // Configuración del número máximo de intentos

typedef struct {
    bool active;
    int iid;
    pid_t pid;
    char bin_path[256];
    char state[16];
    time_t start_time;
    
    // --- VARIABLES PARA EL BONUS ---
    int restarts;
    bool explicit_stop; 
} ProcessInfo;

ProcessInfo process_table[MAX_PROCESSES];
int next_iid = 2;

void daemonize() {
    pid_t pid = fork();
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS); 
    
    if (setsid() < 0) exit(EXIT_FAILURE); 
    
    pid = fork(); 
    if (pid < 0) exit(EXIT_FAILURE);
    if (pid > 0) exit(EXIT_SUCCESS);
    
    freopen("/dev/null", "r", stdin);
    freopen("/dev/null", "w", stdout);
    freopen("/dev/null", "w", stderr);
}

void get_uptime(time_t start, char* buffer, size_t max_size) {
    time_t now = time(NULL);
    int diff = (int)difftime(now, start);
    int h = diff / 3600;
    int m = (diff % 3600) / 60;
    int s = diff % 60;
    snprintf(buffer, max_size, "%02d:%02d:%02d", h, m, s);
}

int find_process_by_iid(int iid) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].active && process_table[i].iid == iid) {
            return i;
        }
    }
    return -1;
}

int main() {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        process_table[i].active = false;
    }

    daemonize();

    mkfifo(REQ_FIFO, 0666);
    mkfifo(RES_FIFO, 0666);

    int req_fd = open(REQ_FIFO, O_RDONLY | O_NONBLOCK);
    struct pollfd pfd;
    pfd.fd = req_fd;
    pfd.events = POLLIN;

    while (true) {
        int status;
        pid_t dead_pid;
        
        while ((dead_pid = waitpid(-1, &status, WNOHANG)) > 0) {
            for (int i = 0; i < MAX_PROCESSES; i++) {
                if (process_table[i].active && process_table[i].pid == dead_pid) {
                    
                    if (process_table[i].explicit_stop) {
                        strcpy(process_table[i].state, "STOPPED");
                    } 
                    else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                        strcpy(process_table[i].state, "ZOMBIE");
                    } 
                    else {
                        if (process_table[i].restarts < MAX_RESTARTS) {
                            process_table[i].restarts++;
                            
                            pid_t new_pid = fork();
                            if (new_pid == 0) {
                                setpgid(0, 0); 
                                char* args[] = { process_table[i].bin_path, NULL };
                                execvp(process_table[i].bin_path, args);
                                exit(EXIT_FAILURE); 
                            } else if (new_pid > 0) {
                                process_table[i].pid = new_pid;
                                strcpy(process_table[i].state, "RUNNING");
                                process_table[i].start_time = time(NULL);
                            }
                        } else {
                            strcpy(process_table[i].state, "FAILED");
                        }
                    }
                    break;
                }
            }
        }

        int ret = poll(&pfd, 1, 500); 
        
        if (ret > 0 && (pfd.revents & POLLIN)) {
            Request req;
            int bytes_read = read(req_fd, &req, sizeof(Request));
            
            if (bytes_read == sizeof(Request)) {
                Response res;
                memset(&res, 0, sizeof(Response));
                int offset = 0;

                if (req.type == CMD_START) {
                    pid_t pid = fork();
                    if (pid == 0) {
                        setpgid(0, 0); 
                        char* args[] = { req.bin_path, NULL };
                        execvp(req.bin_path, args);
                        exit(EXIT_FAILURE); 
                    } else if (pid > 0) {
                        for (int i = 0; i < MAX_PROCESSES; i++) {
                            if (!process_table[i].active) {
                                process_table[i].active = true;
                                process_table[i].iid = next_iid++;
                                process_table[i].pid = pid;
                                strcpy(process_table[i].bin_path, req.bin_path);
                                strcpy(process_table[i].state, "RUNNING");
                                process_table[i].start_time = time(NULL);
                                
                                process_table[i].restarts = 0;
                                process_table[i].explicit_stop = false;
                                
                                snprintf(res.payload, sizeof(res.payload), "IID: %d\n", process_table[i].iid);
                                break;
                            }
                        }
                    }
                } 
                else if (req.type == CMD_PS) {
                    offset += snprintf(res.payload + offset, sizeof(res.payload) - offset, "IID\tPID\tSTATE\tUPTIME\t\tBINARY\n");
                    for (int i = 0; i < MAX_PROCESSES; i++) {
                        if (process_table[i].active) {
                            char uptime[20];
                            get_uptime(process_table[i].start_time, uptime, sizeof(uptime));
                            offset += snprintf(res.payload + offset, sizeof(res.payload) - offset, "%d\t%d\t%s\t%s\t%s\n",
                                               process_table[i].iid, process_table[i].pid, process_table[i].state, uptime, process_table[i].bin_path);
                        }
                    }
                }
                else if (req.type == CMD_STATUS) {
                    int idx = find_process_by_iid(req.iid);
                    if (idx != -1) {
                        char uptime[20];
                        get_uptime(process_table[idx].start_time, uptime, sizeof(uptime));
                        snprintf(res.payload, sizeof(res.payload), 
                                 "IID: %d\nPID: %d\nBINARY: %s\nSTATE: %s\nUPTIME: %s\nRESTARTS: %d\n",
                                 process_table[idx].iid, process_table[idx].pid, process_table[idx].bin_path, 
                                 process_table[idx].state, uptime, process_table[idx].restarts);
                    } else {
                        snprintf(res.payload, sizeof(res.payload), "Error: IID no encontrado.\n");
                    }
                }
                else if (req.type == CMD_ZOMBIE) {
                    offset += snprintf(res.payload + offset, sizeof(res.payload) - offset, "IID\tPID\tSTATE\tUPTIME\t\tBINARY\n");
                    int found = 0;
                    for (int i = 0; i < MAX_PROCESSES; i++) {
                        if (process_table[i].active && strcmp(process_table[i].state, "ZOMBIE") == 0) {
                            char uptime[20];
                            get_uptime(process_table[i].start_time, uptime, sizeof(uptime));
                            offset += snprintf(res.payload + offset, sizeof(res.payload) - offset, "%d\t%d\t%s\t%s\t%s\n",
                                               process_table[i].iid, process_table[i].pid, process_table[i].state, uptime, process_table[i].bin_path);
                            found++;
                        }
                    }
                    if (found == 0) {
                        snprintf(res.payload, sizeof(res.payload), "No hay procesos en estado ZOMBIE.\n");
                    }
                }
                else if (req.type == CMD_STOP || req.type == CMD_KILL) {
                    int idx = find_process_by_iid(req.iid);
                    if (idx != -1) {
                        process_table[idx].explicit_stop = true; 
                        
                        pid_t target_pid = process_table[idx].pid;
                        int sig = (req.type == CMD_STOP) ? SIGTERM : SIGKILL;
                        if (req.type == CMD_KILL) killpg(target_pid, sig);
                        else kill(target_pid, sig);
                        
                        snprintf(res.payload, sizeof(res.payload), "Senal enviada al IID %d.\n", req.iid);
                    } else {
                        snprintf(res.payload, sizeof(res.payload), "Error: IID no encontrado.\n");
                    }
                }

                int res_fd = open(RES_FIFO, O_WRONLY);
                write(res_fd, &res, sizeof(Response));
                close(res_fd);
            }
        }
    }
    return 0;
}
