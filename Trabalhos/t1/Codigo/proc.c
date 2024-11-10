#include "so.h"
#include "cpu.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "instrucao.h"
#include "proc.h"

#include <stdlib.h>
#include <stdbool.h>

struct process_t
{
    int id;
    exec_state_t exec_state;

    int A;
    int X;
    int PC;

    int device;
};


process_t *proc_create(int id, int PC)
{
    process_t *process = malloc(sizeof(process_t));
    process->id = id;
    process->PC = PC;
    process->exec_state = PROC_PRONTO;

    return process;
}

int proc_get_PC(process_t* proc)
{
    return proc->PC;
}

int proc_get_A(process_t* proc)
{
    return proc->A;
}

int proc_get_X(process_t* proc)
{
    return proc->X;
}

exec_state_t proc_get_state(process_t *proc)
{
    return proc->exec_state;
}

void proc_set_ID(process_t *proc, int id)
{
    proc->id = id;
}

void proc_set_PC(process_t *proc, int pc)
{
    proc->PC = pc;
}

void proc_set_A(process_t *proc, int a)
{
    proc->A = a;
}

void proc_set_X(process_t *proc, int x)
{
    proc->X = x;
}

void proc_set_state(process_t *proc, exec_state_t state)
{
    proc->exec_state = state;
}


