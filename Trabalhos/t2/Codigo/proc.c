#include "so.h"
#include "cpu.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "instrucao.h"
#include "proc.h"
#include "tabpag.h"

#include <stdlib.h>
#include <stdbool.h>

struct process_t
{
    int id;
    exec_state_t exec_state;

    int A;
    int X;
    int complemento;
    int PC;
    int erro;

    int device;

    int block_type;
    int block_info;

    double priority;

    proc_metrics_t metrics;

    tabpag_t* page_table;

    int disk_address;
};


process_t *proc_create(int id)
{
    process_t *process = malloc(sizeof(process_t));
    process->id = id;
    process->exec_state = PROC_PRONTO;
    process->device = ((id-1)%4)*4;

    process->block_type = AGUARDA_NADA;
    process->block_info = NULL_ID;

    process->priority = 0.5;


    /* -------- metrics start here -------- */
    process->metrics.existence_time = 0;
    process->metrics.preemptions = 0;

    process->metrics.ready_count = 1;   // it's born ready, ain't it?
    process->metrics.blocked_count = 0;
    process->metrics.executing_count = 0;

    process->metrics.ready_time = 0;
    process->metrics.blocked_time = 0;
    process->metrics.executing_time = 0;
    /* -------- metrics end here -------- */

    process->page_table = tabpag_cria();

    return process;
}

int proc_get_ID(process_t* proc)
{
    return proc->id;
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

int proc_get_complemento(process_t *proc)
{
    return proc->complemento;
}

int proc_get_erro(process_t* proc)
{
    return proc->erro;
}

exec_state_t proc_get_state(process_t *proc)
{   
    return proc->exec_state;
}

int proc_get_device(process_t* proc)
{
    return proc->device;
}

int proc_get_block_type(process_t *proc)
{
    return proc->block_type;
}

int proc_get_block_info(process_t *proc)
{
    return proc->block_info;
}

double proc_get_priority(process_t *proc)
{
    return proc->priority;
}

proc_metrics_t *proc_get_metrics_ptr(process_t *proc)
{
    return &proc->metrics;
}

tabpag_t *proc_get_tab_pag(process_t* proc)
{
    return proc->page_table;
}

int proc_get_disk_address(process_t *proc)
{
    return proc->disk_address;
}

/*---------------------------------------------------------------*/

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

void proc_set_complemento(process_t *proc, int complemento)
{
    proc->complemento = complemento;
}

void proc_set_erro(process_t *proc, int erro)
{
    proc->erro = erro;
}

void proc_set_state(process_t *proc, exec_state_t state)
{
    if(proc == NULL) return;

    proc->exec_state = state;
    switch (proc_get_state(proc))
    {
        case PROC_EXECUTANDO:
            proc->metrics.executing_count += 1;
            break;

        case PROC_PRONTO:
            proc->metrics.ready_count += 1;
            break;

        case PROC_BLOQUEADO:
            proc->metrics.blocked_count += 1;
            break;

        default:
            break;
    }

}

void proc_set_device(process_t *proc, int device)
{
    proc->device = device;
}

void proc_set_block_type(process_t *proc, int block_type)
{
    proc->block_type = block_type;
}

void proc_set_block_info(process_t *proc, int block_info)
{
    proc->block_info = block_info;
}

void proc_set_disk_address(process_t *proc, int disk_address)
{
    proc->disk_address = disk_address;
}

void proc_set_priority(process_t *proc, int priority)
{
    proc->priority = priority;
}

void proc_calc_priority(process_t *proc, int remaining_time, int default_time)
{
    proc->priority = (proc->priority + (double)(default_time-remaining_time)/(double)default_time)/2.0;
}

void proc_increment_preemption(process_t *proc)
{
    proc->metrics.preemptions++;
}

void proc_calc_existence_time(process_t *proc)
{
    proc->metrics.existence_time = (proc->metrics.ready_time + proc->metrics.blocked_time + proc->metrics.executing_time);   
}

void proc_calc_avg_response_time(process_t *proc)
{
    proc->metrics.avg_response_time = (double)proc->metrics.ready_time / proc->metrics.ready_count;
}

void proc_internal_tally(process_t *proc)
{
    proc_calc_existence_time(proc);
    proc_calc_avg_response_time(proc);
}