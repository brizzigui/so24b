#ifndef PROC_H
#define PROC_H

typedef struct process_t process_t;
typedef int exec_state_t;

#define PROC_EXECUTANDO 0
#define PROC_PRONTO 1
#define PROC_BLOQUEADO 2
#define PROC_MORTO 3

#define NULL_ID 0

#define AGUARDA_NADA 0
#define AGUARDA_ENTRADA 1
#define AGUARDA_SAIDA 2
#define AGUARDA_PROC 3

process_t *proc_create(int id, int PC);

int proc_get_PC(process_t* proc);
int proc_get_A(process_t* proc);
int proc_get_X(process_t* proc);
int proc_get_ID(process_t* proc);
exec_state_t proc_get_state(process_t *proc);
int proc_get_device(process_t* proc);
int proc_get_block_type(process_t *proc);
int proc_get_block_info(process_t *proc);


void proc_set_ID(process_t *proc, int id);
void proc_set_PC(process_t *proc, int pc);
void proc_set_A(process_t *proc, int a);
void proc_set_X(process_t *proc, int x);
void proc_set_state(process_t *proc, exec_state_t state);
void proc_set_device(process_t *proc, int device);
void proc_set_block_type(process_t *proc, int block_type);
void proc_set_block_info(process_t *proc, int block_info);


#endif