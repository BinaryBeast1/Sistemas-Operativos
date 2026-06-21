#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    FIRST_FIT,
    BEST_FIT,
    WORST_FIT
} Policy;

typedef struct Block {
    int is_free;
    char process_id[64];
    size_t size;
    struct Block* prev;
    struct Block* next;
} Block;

Block* head = NULL;
size_t total_memory = 0;
int successful_allocs = 0;

void init_memory(size_t size) {
    head = (Block*)malloc(sizeof(Block));
    if (!head) {
        fprintf(stderr, "Error allocating memory for head\n");
        exit(1);
    }
    head->is_free = 1;
    strcpy(head->process_id, "");
    head->size = size;
    head->prev = NULL;
    head->next = NULL;
    total_memory = size;
}

void split_block(Block* block, size_t size, const char* pid) {
    if (block->size > size) {
        Block* new_block = (Block*)malloc(sizeof(Block));
        if (!new_block) {
            fprintf(stderr, "Error allocating memory for new_block\n");
            exit(1);
        }
        new_block->is_free = 1;
        strcpy(new_block->process_id, "");
        new_block->size = block->size - size;
        new_block->prev = block;
        new_block->next = block->next;
        
        if (block->next != NULL) {
            block->next->prev = new_block;
        }
        
        block->next = new_block;
    }
    block->is_free = 0;
    strcpy(block->process_id, pid);
    block->size = size;
}

int do_alloc(const char* pid, size_t size, Policy policy) {
    Block* curr = head;
    Block* chosen = NULL;

    if (policy == FIRST_FIT) {
        while (curr != NULL) {
            if (curr->is_free && curr->size >= size) {
                chosen = curr;
                break;
            }
            curr = curr->next;
        }
    } else if (policy == BEST_FIT) {
        size_t min_diff = (size_t)-1;
        while (curr != NULL) {
            if (curr->is_free && curr->size >= size) {
                size_t diff = curr->size - size;
                if (diff < min_diff) {
                    min_diff = diff;
                    chosen = curr;
                }
            }
            curr = curr->next;
        }
    } else if (policy == WORST_FIT) {
        size_t max_size = 0;
        while (curr != NULL) {
            if (curr->is_free && curr->size >= size) {
                if (curr->size > max_size) {
                    max_size = curr->size;
                    chosen = curr;
                }
            }
            curr = curr->next;
        }
    }

    if (chosen != NULL) {
        split_block(chosen, size, pid);
        successful_allocs++;
        return 1;
    }
    return 0; // Failed due to fragmentation
}

void coalesce(Block* block) {
    // Merge with next
    if (block->next != NULL && block->next->is_free) {
        Block* next_block = block->next;
        block->size += next_block->size;
        block->next = next_block->next;
        if (next_block->next != NULL) {
            next_block->next->prev = block;
        }
        free(next_block);
    }
    // Merge with prev
    if (block->prev != NULL && block->prev->is_free) {
        Block* prev_block = block->prev;
        prev_block->size += block->size;
        prev_block->next = block->next;
        if (block->next != NULL) {
            block->next->prev = prev_block;
        }
        free(block);
    }
}

void free_process(const char* pid) {
    Block* curr = head;
    while (curr != NULL) {
        Block* next_block = curr->next;
        if (!curr->is_free && strcmp(curr->process_id, pid) == 0) {
            curr->is_free = 1;
            strcpy(curr->process_id, "");
            coalesce(curr);
        }
        curr = next_block;
    }
}

void compact() {
    Block* curr = head;
    size_t free_accum = 0;
    
    Block* new_head = NULL;
    Block* new_tail = NULL;
    
    while (curr != NULL) {
        Block* next_block = curr->next;
        if (curr->is_free) {
            free_accum += curr->size;
            free(curr);
        } else {
            curr->prev = new_tail;
            curr->next = NULL;
            if (new_tail != NULL) {
                new_tail->next = curr;
            } else {
                new_head = curr;
            }
            new_tail = curr;
        }
        curr = next_block;
    }
    
    if (free_accum > 0 || new_head == NULL) {
        Block* free_block = (Block*)malloc(sizeof(Block));
        if (!free_block) {
            fprintf(stderr, "Error allocating memory for free_block\n");
            exit(1);
        }
        free_block->is_free = 1;
        strcpy(free_block->process_id, "");
        free_block->size = (new_head == NULL && free_accum == 0) ? total_memory : free_accum; 
        free_block->prev = new_tail;
        free_block->next = NULL;
        
        if (new_tail != NULL) {
            new_tail->next = free_block;
        } else {
            new_head = free_block;
        }
    }
    
    head = new_head;
}

void print_stats() {
    size_t used_memory = 0;
    size_t max_free_block = 0;
    size_t total_free_memory = 0;
    
    Block* curr = head;
    while (curr != NULL) {
        if (!curr->is_free) {
            used_memory += curr->size;
        } else {
            total_free_memory += curr->size;
            if (curr->size > max_free_block) {
                max_free_block = curr->size;
            }
        }
        curr = curr->next;
    }
    
    double percent_used = 0.0;
    if (total_memory > 0) {
        percent_used = (double)used_memory / (double)total_memory * 100.0;
    }
    
    double f_ext = 0.0;
    if (total_free_memory > 0) {
        f_ext = 1.0 - ((double)max_free_block / (double)total_free_memory);
    }
    
    printf("Procesos asignados: %d\n", successful_allocs);
    printf("Memoria utilizada: %.0f%%\n", percent_used); // or %.2f
    printf("Indice de Fragmentacion Externa: %.5f\n", f_ext);
    printf("Estado final de la memoria:\n");
    
    curr = head;
    while (curr != NULL) {
        if (curr->is_free) {
            printf("[Libre %zu]", curr->size);
        } else {
            printf("[Ocupado %s %zu]", curr->process_id, curr->size);
        }
        if (curr->next != NULL) {
            printf(" -> ");
        }
        curr = curr->next;
    }
    printf("\n");
}

void cleanup() {
    Block* curr = head;
    while (curr != NULL) {
        Block* next_block = curr->next;
        free(curr);
        curr = next_block;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <size> <policy> <trace_file>\n", argv[0]);
        return 1;
    }
    
    size_t size = (size_t)strtoull(argv[1], NULL, 10);
    Policy policy;
    if (strcmp(argv[2], "FIRST_FIT") == 0) {
        policy = FIRST_FIT;
    } else if (strcmp(argv[2], "BEST_FIT") == 0) {
        policy = BEST_FIT;
    } else if (strcmp(argv[2], "WORST_FIT") == 0) {
        policy = WORST_FIT;
    } else {
        fprintf(stderr, "Politica invalida. Use: FIRST_FIT, BEST_FIT o WORST_FIT\n");
        return 1;
    }
    
    init_memory(size);
    
    FILE* file = fopen(argv[3], "r");
    if (!file) {
        fprintf(stderr, "Error: No se pudo abrir el archivo %s\n", argv[3]);
        cleanup();
        return 1;
    }
    
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        char cmd[64] = {0};
        char pid[64] = {0};
        size_t p_size = 0;
        
        if (sscanf(line, "%63s %63s %zu", cmd, pid, &p_size) == 3) {
            if (strcmp(cmd, "ALLOC") == 0) {
                if (!do_alloc(pid, p_size, policy)) {
                    break;
                }
            }
        } else if (sscanf(line, "%63s %63s", cmd, pid) == 2) {
            if (strcmp(cmd, "FREE") == 0) {
                free_process(pid);
            } else if (strcmp(cmd, "COMPACT") == 0) {
                compact();
            }
        } else if (sscanf(line, "%63s", cmd) == 1) {
            if (strcmp(cmd, "COMPACT") == 0) {
                compact();
            }
        }
    }
    
    fclose(file);
    print_stats();
    cleanup();
    
    return 0;
}
