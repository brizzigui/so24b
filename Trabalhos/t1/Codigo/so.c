// so.c
// sistema operacional
// simulador de computador
// so24b

// INCLUDES {{{1
#include "so.h"
#include "dispositivos.h"
#include "irq.h"
#include "programa.h"
#include "instrucao.h"
#include "proc.h"
#include "list.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#define MAX_PROC 16

#define SCHEDULER_TYPE 2 // escolha o tipo de escalonador

#define SCHEDULER_TYPE0 0
#define SCHEDULER_TYPE1 1
#define SCHEDULER_TYPE2 2

#define TYPES_OF_IRQS 6

// CONSTANTES DE EXECUÇÃO
#define DEFAULT_QUANTUM 10
#define INTERVALO_INTERRUPCAO 100   // em instruções executadas

struct sys_metrics_t 
{
  int total_processes;
  int total_runtime;
  int total_halted_time;
  int *interrupts;
  int preemptions;
};

struct so_t {
  cpu_t *cpu;
  mem_t *mem;
  es_t *es;
  console_t *console;
  bool erro_interno;

  process_t **process_table;
  process_t *current_process;

  int process_counter;
  int process_slots;

  list *queue;
  int quantum;

  sys_metrics_t metrics;
  int latest_clock;

  // t1: tabela de processos, processo corrente, pendências, etc
};


// função de tratamento de interrupção (entrada no SO)
static int so_trata_interrupcao(void *argC, int reg_A);

// funções auxiliares
// carrega o programa contido no arquivo na memória do processador; retorna end. inicial
static int so_carrega_programa(so_t *self, char *nome_do_executavel);
// copia para str da memória do processador, até copiar um 0 (retorna true) ou tam bytes
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender);

// CRIAÇÃO {{{1

sys_metrics_t so_inicializa_metricas(so_t *self)
{
  sys_metrics_t metrics;
  metrics.total_processes = 0;
  metrics.total_runtime = 0;
  metrics.total_halted_time = 0;
  metrics.interrupts = (int *)malloc(TYPES_OF_IRQS * sizeof(int));
  metrics.preemptions = 0;

  return metrics;
}

so_t *so_cria(cpu_t *cpu, mem_t *mem, es_t *es, console_t *console)
{
  so_t *self = malloc(sizeof(*self));
  if (self == NULL) return NULL;

  self->cpu = cpu;
  self->mem = mem;
  self->es = es;
  self->console = console;
  self->erro_interno = false;

  self->process_slots = MAX_PROC;
  self->process_table = malloc(self->process_slots * sizeof(process_t *));
  self->current_process = NULL;
  self->process_counter = 1;

  self->queue = list_create();  
  self->quantum = DEFAULT_QUANTUM;

  self->metrics = so_inicializa_metricas(self);

  // quando a CPU executar uma instrução CHAMAC, deve chamar a função
  //   so_trata_interrupcao, com primeiro argumento um ptr para o SO
  cpu_define_chamaC(self->cpu, so_trata_interrupcao, self);

  // coloca o tratador de interrupção na memória
  // quando a CPU aceita uma interrupção, passa para modo supervisor, 
  //   salva seu estado à partir do endereço 0, e desvia para o endereço
  //   IRQ_END_TRATADOR
  // colocamos no endereço IRQ_END_TRATADOR o programa de tratamento
  //   de interrupção (escrito em asm). esse programa deve conter a 
  //   instrução CHAMAC, que vai chamar so_trata_interrupcao (como
  //   foi definido acima)
  int ender = so_carrega_programa(self, "trata_int.maq");
  if (ender != IRQ_END_TRATADOR) {
    console_printf("SO: problema na carga do programa de tratamento de interrupção");
    self->erro_interno = true;
  }

  // programa o relógio para gerar uma interrupção após INTERVALO_INTERRUPCAO
  if (es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO) != ERR_OK) {
    console_printf("SO: problema na programação do timer");
    self->erro_interno = true;
  }

  return self;
}

void so_destroi(so_t *self)
{
  cpu_define_chamaC(self->cpu, NULL, NULL);
  free(self);
}


void so_update_metrics(so_t *self, int irq)
{
  // métrica de tipo de interrupção
  self->metrics.interrupts[irq]++;


  // métricas de tempo
  int last_clock = self->latest_clock;
  if(es_le(self->es, D_RELOGIO_INSTRUCOES, &self->latest_clock) != ERR_OK)
  {
    console_printf("Erro na leitura do relógio");
    exit(-1);
  }

  int elapsed_time = self->latest_clock - last_clock;

  for (int i = 1; i < self->process_counter; i++)
  {
    process_t *proc = self->process_table[i];
    if(proc != NULL)
    {
      proc_metrics_t *metrics = proc_get_metrics_ptr(proc);
      switch (proc_get_state(proc))
      {
        case PROC_EXECUTANDO:
          metrics->executing_time += elapsed_time;
          break;

        case PROC_PRONTO:
          metrics->ready_time += elapsed_time;
          break;

        case PROC_BLOQUEADO:
          metrics->blocked_time += elapsed_time;
          break;
        
        default:
          break;
      }
    }
  }
}



// TRATAMENTO DE INTERRUPÇÃO {{{1

// funções auxiliares para o tratamento de interrupção
static void so_salva_estado_da_cpu(so_t *self);
static void so_trata_irq(so_t *self, int irq);
static void so_trata_pendencias(so_t *self);
static void so_escalona(so_t *self);
static int so_despacha(so_t *self);
int so_suicide(so_t *self);
bool is_any_proc_alive(so_t *self);


// função a ser chamada pela CPU quando executa a instrução CHAMAC, no tratador de
//   interrupção em assembly
// essa é a única forma de entrada no SO depois da inicialização
// na inicialização do SO, a CPU foi programada para chamar esta função para executar
//   a instrução CHAMAC
// a instrução CHAMAC só deve ser executada pelo tratador de interrupção
//
// o primeiro argumento é um ponteiro para o SO, o segundo é a identificação
//   da interrupção
// o valor retornado por esta função é colocado no registrador A, e pode ser
//   testado pelo código que está após o CHAMAC. No tratador de interrupção em
//   assembly esse valor é usado para decidir se a CPU deve retornar da interrupção
//   (e executar o código de usuário) ou executar PARA e ficar suspensa até receber
//   outra interrupção
static int so_trata_interrupcao(void *argC, int reg_A)
{
  so_t *self = argC;
  irq_t irq = reg_A;

  // atualiza as métricas do SO
  so_update_metrics(self, irq);

  // esse print polui bastante, recomendo tirar quando estiver com mais confiança
  // console_printf("SO: recebi IRQ %d (%s)", irq, irq_nome(irq));

  // salva o estado da cpu no descritor do processo que foi interrompido
  so_salva_estado_da_cpu(self);
  // faz o atendimento da interrupção
  so_trata_irq(self, irq);
  // faz o processamento independente da interrupção
  so_trata_pendencias(self);
  // escolhe o próximo processo a executar
  so_escalona(self);

  if (!is_any_proc_alive(self))
  {
    return so_suicide(self);
  }

  else
  {
    // recupera o estado do processo escolhido
    return so_despacha(self);
  }

}

static void so_salva_estado_da_cpu(so_t *self)
{
  // t1: salva os registradores que compõem o estado da cpu no descritor do
  //   processo corrente. os valores dos registradores foram colocados pela
  //   CPU na memória, nos endereços IRQ_END_*
  // se não houver processo corrente, não faz nada

  if (self->current_process == NULL)
  {
    return;
  }

  int tmp_A, tmp_X, tmp_PC;
  mem_le(self->mem, IRQ_END_A, &tmp_A);
  mem_le(self->mem, IRQ_END_X, &tmp_X);
  mem_le(self->mem, IRQ_END_PC, &tmp_PC);

  proc_set_A(self->current_process, tmp_A);
  proc_set_X(self->current_process, tmp_X);
  proc_set_PC(self->current_process, tmp_PC);
  
}

int device_calc(int device, int type);

static void so_bloqueia_proc(so_t *self, process_t* proc, int block_type, int block_info)
{
  // console_printf("SO: bloqueei um processo, sua id era %d com causa %d", proc_get_ID(proc), block_type);
  proc_set_state(proc, PROC_BLOQUEADO);
  proc_set_block_type(proc, block_type);
  proc_set_block_info(proc, block_info);

  void *v;
  self->queue = list_pop(self->queue, &v);

  if (self->current_process != NULL)
  {
    proc_calc_priority(self->current_process, self->quantum, DEFAULT_QUANTUM);
  }
}

static void so_desbloqueia_proc(so_t *self, process_t* proc)
{
  // console_printf("SO: desbloqueei um processo, sua id era %d", proc_get_ID(proc));
  proc_set_state(proc, PROC_PRONTO);
  proc_set_block_type(proc, AGUARDA_NADA);
  proc_set_block_info(proc, NULL_ID);

  self->queue = list_append(self->queue, proc);
}

static void so_trata_pendencia_leitura(so_t *self, process_t* proc)
{
  int base_device = proc_get_device(proc);

  int estado;
  if (es_le(self->es, device_calc(base_device, TECLADO_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do teclado");
    self->erro_interno = true;
    return;
  }

  if (estado == 0)
  {
    return;
  }

  int dado;
  if (es_le(self->es, device_calc(base_device, TECLADO), &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }
  
  proc_set_A(proc, dado);
  so_desbloqueia_proc(self, proc);
}

static void so_trata_pendencia_escrita(so_t *self, process_t* proc)
{
  int base_device = proc_get_device(proc);

  int estado;
  if (es_le(self->es, device_calc(base_device, TELA_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado da tela");
    self->erro_interno = true;
    return;
  }

  if (estado == 0)
  {
    return;
  }


  int dado = proc_get_X(proc);
  if (es_escreve(self->es, device_calc(base_device, TELA), dado) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    self->erro_interno = true;
    return;
  }
  proc_set_A(proc, 0);
  so_desbloqueia_proc(self, proc);
}

static void so_trata_pendencia_espera(so_t *self, process_t* proc)
{
  int awaiting_proc = proc_get_block_info(proc);
  exec_state_t state = proc_get_state(self->process_table[awaiting_proc]);

  if(state == PROC_MORTO)
  {
    so_desbloqueia_proc(self, proc);
  }
}

static void so_trata_pendencias(so_t *self)
{
  // t1: realiza ações que não são diretamente ligadas com a interrupção que
  //   está sendo atendida:
  // - E/S pendente
  // - desbloqueio de processos
  // - contabilidades

  for (int i = 1; i < self->process_counter; i++)
  {
    process_t *analyzed = self->process_table[i];
    if (analyzed != NULL && proc_get_state(analyzed) == PROC_BLOQUEADO)
    {
      int block_type = proc_get_block_type(analyzed);

      switch (block_type)
      {
        case AGUARDA_ENTRADA:
          so_trata_pendencia_leitura(self, analyzed);
          break;
        
        case AGUARDA_SAIDA:
          so_trata_pendencia_escrita(self, analyzed);
          break;

        case AGUARDA_PROC:
          so_trata_pendencia_espera(self, analyzed);
          break;
        
        default:
          break;
      }
    }
  }
  

}

static void scheduler_dumb_type0(so_t *self)
{
  if(self->current_process != NULL && proc_get_state(self->current_process) == PROC_EXECUTANDO)
  {
    return;
  }

  else
  {
    self->current_process = NULL;
    for (int i = 1; i < self->process_counter; i++)
    {
      process_t *analyzed = self->process_table[i];
      if (analyzed != NULL && proc_get_state(analyzed) == PROC_PRONTO)
      {
        self->current_process = analyzed;
        return;
      }
      
    }
    
  }
}

static void round_robin_type1(so_t *self)
{
  if(self->quantum == 0)
  {
    void *timed_out;

    self->queue = list_pop(self->queue, &(timed_out));
    self->queue = list_append(self->queue, timed_out);

    if (timed_out	!= NULL)
      proc_increment_preemption(timed_out);
  }

  process_t *chosen_process = list_get(self->queue, 0);

  if(chosen_process == self->current_process)
  {
    self->quantum--;
    self->current_process = chosen_process;
    return;
  }

  else
  {
    self->quantum = DEFAULT_QUANTUM;
    self->current_process = chosen_process;
  }

}

static void round_robin_type2(so_t *self)
{
  if(self->quantum == 0)
  {
    self->quantum = DEFAULT_QUANTUM;
    if (self->current_process != NULL)
    {
      proc_calc_priority(self->current_process, self->quantum, DEFAULT_QUANTUM);
      proc_increment_preemption(self->current_process);
    }
  }

  float min_priority = INFINITY;
  process_t *chosen_process = NULL;

  for (int i = 1; i < self->process_counter; i++)
  {
    process_t *analyzed = self->process_table[i];   
    if (analyzed != NULL && (proc_get_state(analyzed) == PROC_PRONTO || proc_get_state(analyzed) == PROC_EXECUTANDO))
    {
      double cur_priority = proc_get_priority(analyzed);
      if (cur_priority < min_priority)
      {
        min_priority = cur_priority;
        chosen_process = analyzed;
      }
    }
  }

  if (self->current_process == chosen_process)
  {
    self->quantum--;
  }
  
  self->current_process = chosen_process; 
}

int so_suicide(so_t *self)
{


  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0);
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, 0);
  if (e1 != ERR_OK || e2 != ERR_OK) {
    console_printf("SO: não consigo parar VOU DOMINAR O MUNDO EXECUÇÃO ETERNA");
    self->erro_interno = true;
  }

  console_printf("--------------------------------------------------");
  console_printf("--------------------------------------------------");
  console_printf("--------------------------------------------------");
  console_printf("------                                      ------");
  console_printf("------                                      ------");
  console_printf("------     SO: todos os processos mortos    ------");
  console_printf("------        SO: parando a simulação       ------");
  console_printf("------                                      ------");
  console_printf("------       Clique 'F' para continuar.     ------");
  console_printf("------                                      ------");
  console_printf("------                                      ------");
  console_printf("--------------------------------------------------");
  console_printf("--------------------------------------------------");
  console_printf("---------------------------------:)-;)-:D---------");

  return 1;
}

bool is_any_proc_alive(so_t *self)
{
  for (int i = 1; i < self->process_counter; i++)
  {
    process_t *analyzed = self->process_table[i];
    if (analyzed != NULL)
    {
      if (proc_get_state(analyzed) != PROC_MORTO)
      {
        return true;
      }
    }
  }

  return false;
}

static void so_escalona(so_t *self)
{
  process_t *irq_causer = self->current_process;

  switch (SCHEDULER_TYPE)
  {
    case SCHEDULER_TYPE0:
      scheduler_dumb_type0(self);
      break;

    case SCHEDULER_TYPE1:
      round_robin_type1(self);
      break;

    case SCHEDULER_TYPE2:
      round_robin_type2(self);
      break;
  }
 
  if (irq_causer != self->current_process)
  {
    proc_set_state(self->current_process, PROC_EXECUTANDO);
    if (irq_causer != NULL && proc_get_state(irq_causer) == PROC_EXECUTANDO)
    {
      proc_set_state(irq_causer, PROC_PRONTO);   
    }  
  }
  
}

static int so_despacha(so_t *self)
{
  // t1: se houver processo corrente, coloca o estado desse processo onde ele
  //   será recuperado pela CPU (em IRQ_END_*) e retorna 0, senão retorna 1
  // o valor retornado será o valor de retorno de CHAMAC
  if (self->erro_interno || self->current_process == NULL) return 1;

  int a, x, pc;
  a = proc_get_A(self->current_process);
  x = proc_get_X(self->current_process);
  pc = proc_get_PC(self->current_process);

  mem_escreve(self->mem, IRQ_END_A, a);
  mem_escreve(self->mem, IRQ_END_X, x);
  mem_escreve(self->mem, IRQ_END_PC, pc);

  return 0;
}

process_t *so_novo_proc(so_t *self, char* origin)
{
  int ender = so_carrega_programa(self, origin);
  process_t *proc = proc_create(self->process_counter, ender);

  if (self->process_counter == MAX_PROC)
  {
    self->process_slots *= 2;
    self->process_table = realloc(self->process_table, sizeof(process_t *) * self->process_slots);

    if(self->process_table == NULL)
    {
      console_printf("Erro crítico do SO\n");
      exit(-1);
    }

  }
  
  self->process_table[self->process_counter] = proc;
  self->process_counter++;

  self->queue = list_append(self->queue, proc);

  return proc;
}

// TRATAMENTO DE UMA IRQ {{{1

// funções auxiliares para tratar cada tipo de interrupção
static void so_trata_irq_reset(so_t *self);
static void so_trata_irq_chamada_sistema(so_t *self);
static void so_trata_irq_err_cpu(so_t *self);
static void so_trata_irq_relogio(so_t *self);
static void so_trata_irq_desconhecida(so_t *self, int irq);

static void so_trata_irq(so_t *self, int irq)
{
  // verifica o tipo de interrupção que está acontecendo, e atende de acordo
  switch (irq) {
    case IRQ_RESET:
      so_trata_irq_reset(self);
      break;
    case IRQ_SISTEMA:
      so_trata_irq_chamada_sistema(self);
      break;
    case IRQ_ERR_CPU:
      so_trata_irq_err_cpu(self);
      break;
    case IRQ_RELOGIO:
      so_trata_irq_relogio(self);
      break;
    default:
      so_trata_irq_desconhecida(self, irq);
  }
}

// interrupção gerada uma única vez, quando a CPU inicializa
static void so_trata_irq_reset(so_t *self)
{
  // t1: deveria criar um processo para o init, e inicializar o estado do
  //   processador para esse processo com os registradores zerados, exceto
  //   o PC e o modo.
  // como não tem suporte a processos, está carregando os valores dos
  //   registradores diretamente para a memória, de onde a CPU vai carregar
  //   para os seus registradores quando executar a instrução RETI

  process_t *process = so_novo_proc(self, "init.maq");
  self->current_process = process;
  proc_set_state(process, PROC_EXECUTANDO);

  // coloca o programa init na memória
  if (proc_get_PC(process) != 100) {
    console_printf("SO: problema na carga do programa inicial");
    self->erro_interno = true;
    return;
  }


  // altera o PC para o endereço de carga
  // n sei pq comentei mas comentei*************************************
  // mem_escreve(self->mem, IRQ_END_PC, ender);
  // passa o processador para modo usuário
  mem_escreve(self->mem, IRQ_END_modo, usuario);

}

// interrupção gerada quando a CPU identifica um erro
static void so_trata_irq_err_cpu(so_t *self)
{
  // Ocorreu um erro interno na CPU
  // O erro está codificado em IRQ_END_erro
  // Em geral, causa a morte do processo que causou o erro
  // Ainda não temos processos, causa a parada da CPU
  int err_int;
  // t1: com suporte a processos, deveria pegar o valor do registrador erro
  //   no descritor do processo corrente, e reagir de acordo com esse erro
  //   (em geral, matando o processo)
  mem_le(self->mem, IRQ_END_erro, &err_int);
  err_t err = err_int;
  console_printf("SO: IRQ não tratada -- erro na CPU: %s", err_nome(err));
  self->erro_interno = true;
}

// interrupção gerada quando o timer expira
static void so_trata_irq_relogio(so_t *self)
{
  // rearma o interruptor do relógio e reinicializa o timer para a próxima interrupção
  err_t e1, e2;
  e1 = es_escreve(self->es, D_RELOGIO_INTERRUPCAO, 0); // desliga o sinalizador de interrupção
  e2 = es_escreve(self->es, D_RELOGIO_TIMER, INTERVALO_INTERRUPCAO);
  if (e1 != ERR_OK || e2 != ERR_OK) {
    console_printf("SO: problema da reinicialização do timer");
    self->erro_interno = true;
  }
  // t1: deveria tratar a interrupção
  //   por exemplo, decrementa o quantum do processo corrente, quando se tem
  //   um escalonador com quantum
}

// foi gerada uma interrupção para a qual o SO não está preparado
static void so_trata_irq_desconhecida(so_t *self, int irq)
{
  console_printf("SO: não sei tratar IRQ %d (%s)", irq, irq_nome(irq));
  self->erro_interno = true;
}

// CHAMADAS DE SISTEMA {{{1

// funções auxiliares para cada chamada de sistema
static void so_chamada_le(so_t *self);
static void so_chamada_escr(so_t *self);
static void so_chamada_cria_proc(so_t *self);
static void so_chamada_mata_proc(so_t *self);
static void so_chamada_espera_proc(so_t *self);

static void so_trata_irq_chamada_sistema(so_t *self)
{
  // a identificação da chamada está no registrador A
  // t1: com processos, o reg A tá no descritor do processo corrente
  int id_chamada;
  if (mem_le(self->mem, IRQ_END_A, &id_chamada) != ERR_OK) {
    console_printf("SO: erro no acesso ao id da chamada de sistema");
    self->erro_interno = true;
    return;
  }
  //console_printf("SO: chamada de sistema %d", id_chamada);
  switch (id_chamada) {
    case SO_LE:
      so_chamada_le(self);
      break;
    case SO_ESCR:
      so_chamada_escr(self);
      break;
    case SO_CRIA_PROC:
      so_chamada_cria_proc(self);
      break;
    case SO_MATA_PROC:
      so_chamada_mata_proc(self);
      break;
    case SO_ESPERA_PROC:
      so_chamada_espera_proc(self);
      break;
    default:
      console_printf("SO: chamada de sistema desconhecida (%d)", id_chamada);
      // t1: deveria matar o processo
      self->erro_interno = true;
  }
}

int device_calc(int device, int type)
{
  return device + type;
}

// implementação da chamada se sistema SO_LE
// faz a leitura de um dado da entrada corrente do processo, coloca o dado no reg A
static void so_chamada_le(so_t *self)
{
  // implementação com espera ocupada
  //   T1: deveria realizar a leitura somente se a entrada estiver disponível,
  //     senão, deveria bloquear o processo.
  //   no caso de bloqueio do processo, a leitura (e desbloqueio) deverá
  //     ser feita mais tarde, em tratamentos pendentes em outra interrupção,
  //     ou diretamente em uma interrupção específica do dispositivo, se for
  //     o caso
  // implementação lendo direto do terminal A
  //   T1: deveria usar dispositivo de entrada corrente do processo
  int base_device = proc_get_device(self->current_process);

  int estado;
  if (es_le(self->es, device_calc(base_device, TECLADO_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado do teclado");
    self->erro_interno = true;
    return;
  }

  if (estado == 0)
  {
    so_bloqueia_proc(self, self->current_process, AGUARDA_ENTRADA, proc_get_device(self->current_process));
    return;
  }

  int dado;
  if (es_le(self->es, device_calc(base_device, TECLADO), &dado) != ERR_OK) {
    console_printf("SO: problema no acesso ao teclado");
    self->erro_interno = true;
    return;
  }
  // escreve no reg A do processador
  // (na verdade, na posição onde o processador vai pegar o A quando retornar da int)
  // T1: se houvesse processo, deveria escrever no reg A do processo
  // T1: o acesso só deve ser feito nesse momento se for possível; se não, o processo
  //   é bloqueado, e o acesso só deve ser feito mais tarde (e o processo desbloqueado)
  proc_set_A(self->current_process, dado);
}

// implementação da chamada se sistema SO_ESCR
// escreve o valor do reg X na saída corrente do processo
static void so_chamada_escr(so_t *self)
{
  // implementação com espera ocupada
  //   T1: deveria bloquear o processo se dispositivo ocupado
  // implementação escrevendo direto do terminal A
  //   T1: deveria usar o dispositivo de saída corrente do processo
  int base_device = proc_get_device(self->current_process);

  int estado;
  if (es_le(self->es, device_calc(base_device, TELA_OK), &estado) != ERR_OK) {
    console_printf("SO: problema no acesso ao estado da tela");
    self->erro_interno = true;
    return;
  }

  if (estado == 0)
  {
    so_bloqueia_proc(self, self->current_process, AGUARDA_SAIDA, proc_get_device(self->current_process));
    return;
  }

  // está lendo o valor de X e escrevendo o de A direto onde o processador colocou/vai pegar
  // T1: deveria usar os registradores do processo que está realizando a E/S
  // T1: caso o processo tenha sido bloqueado, esse acesso deve ser realizado em outra execução
  //   do SO, quando ele verificar que esse acesso já pode ser feito.
  int dado = proc_get_X(self->current_process);
  if (es_escreve(self->es, device_calc(base_device, TELA), dado) != ERR_OK) {
    console_printf("SO: problema no acesso à tela");
    self->erro_interno = true;
    return;
  }
  proc_set_A(self->current_process, 0);
}

// implementação da chamada se sistema SO_CRIA_PROC
// cria um processo
static void so_chamada_cria_proc(so_t *self)
{
  // ainda sem suporte a processos, carrega programa e passa a executar ele
  // quem chamou o sistema não vai mais ser executado, coitado!
  // T1: deveria criar um novo processo


  // em X está o endereço onde está o nome do arquivo
  int ender_proc;
  char nome[100];

  // t1: deveria ler o X do descritor do processo criador
  if (mem_le(self->mem, IRQ_END_X, &ender_proc) == ERR_OK) 
  {
    if (copia_str_da_mem(100, nome, self->mem, ender_proc)) 
    {
      process_t *process = so_novo_proc(self, nome);

      int ender_carga = proc_get_PC(process);
      if (ender_carga > 0) {
        // t1: deveria escrever no PC do descritor do processo criado
        proc_set_A(self->current_process, proc_get_ID(process));
        return;
      }
    }
  }
  // deveria escrever -1 (se erro) ou o PID do processo criado (se OK) no reg A
  //   do processo que pediu a criação
  proc_set_A(self->current_process, -1);

}

// implementação da chamada se sistema SO_MATA_PROC
// mata o processo com pid X (ou o processo corrente se X é 0)
static void so_chamada_mata_proc(so_t *self)
{
  if(self->current_process == NULL)
  {
    return;
  }

  int read_x = proc_get_X(self->current_process);

  if (read_x == 0)
  {
    proc_set_state(self->current_process, PROC_MORTO);
  }

  else
  {
    proc_set_state(self->process_table[read_x], PROC_MORTO);
  }
  
  self->current_process = NULL;

  void *v;
  self->queue = list_pop(self->queue, &v);

  // T1: deveria matar um processo
}

// implementação da chamada se sistema SO_ESPERA_PROC
// espera o fim do processo com pid X
static void so_chamada_espera_proc(so_t *self)
{
  // T1: deveria bloquear o processo se for o caso (e desbloquear na morte do esperado)
  // ainda sem suporte a processos, retorna erro -1
  int awaits_who = proc_get_X(self->current_process);
  so_bloqueia_proc(self, self->current_process, AGUARDA_PROC, awaits_who);
}

// CARGA DE PROGRAMA {{{1

// carrega o programa na memória
// retorna o endereço de carga ou -1
static int so_carrega_programa(so_t *self, char *nome_do_executavel)
{
  // programa para executar na nossa CPU
  programa_t *prog = prog_cria(nome_do_executavel);
  if (prog == NULL) {
    console_printf("Erro na leitura do programa '%s'\n", nome_do_executavel);
    return -1;
  }

  int end_ini = prog_end_carga(prog);
  int end_fim = end_ini + prog_tamanho(prog);

  for (int end = end_ini; end < end_fim; end++) {
    if (mem_escreve(self->mem, end, prog_dado(prog, end)) != ERR_OK) {
      console_printf("Erro na carga da memória, endereco %d\n", end);
      return -1;
    }
  }

  prog_destroi(prog);
  console_printf("SO: carga de '%s' em %d-%d", nome_do_executavel, end_ini, end_fim);
  return end_ini;
}

// ACESSO À MEMÓRIA DOS PROCESSOS {{{1

// copia uma string da memória do simulador para o vetor str.
// retorna false se erro (string maior que vetor, valor não char na memória,
//   erro de acesso à memória)
// T1: deveria verificar se a memória pertence ao processo
static bool copia_str_da_mem(int tam, char str[tam], mem_t *mem, int ender)
{
  for (int indice_str = 0; indice_str < tam; indice_str++) {
    int caractere;
    if (mem_le(mem, ender + indice_str, &caractere) != ERR_OK) {
      return false;
    }
    if (caractere < 0 || caractere > 255) {
      return false;
    }
    str[indice_str] = caractere;
    if (caractere == 0) {
      return true;
    }
  }
  // estourou o tamanho de str
  return false;
}


/* -------------------------------------------- */
/* --- cálculo e impressão de métricas aqui --- */
/* -------------------------------------------- */

void so_tally(so_t *self)
{
  // finaliza as contagens das métricas de execução da simulação
  self->metrics.total_processes = self->process_counter-1;

  for (int i = 1; i < self->process_counter; i++)
  {
    process_t *proc = self->process_table[i];
    proc_metrics_t *proc_metrics = proc_get_metrics_ptr(proc);

    self->metrics.total_runtime += proc_metrics->executing_time;
    self->metrics.total_halted_time += proc_metrics->blocked_time;
    self->metrics.preemptions += proc_metrics->preemptions;

    proc_internal_tally(proc);
  }
}

void so_show_metrics(so_t *self)
{
  so_tally(self);

  console_printf("\n");
  console_printf("\n");
  console_printf("\n");
  console_printf("##################################################");
  console_printf("#####     Métricas do Sistema Operacional    #####");
  console_printf("##################################################");
  console_printf("\n");
  console_printf("##########       Configuração do SO     ##########");
  console_printf("-> Intervalo interrupção: %d instruções", INTERVALO_INTERRUPCAO);
  console_printf("-> Tempo de quantum:      %d interrupções", DEFAULT_QUANTUM);
  console_printf("-> Escalonador usado:     tipo %d", SCHEDULER_TYPE);
  console_printf("\n");
  console_printf("##########        Métricas Gerais       ##########");
  console_printf("-> Número de processos criados: %d processos", self->metrics.total_processes);
  console_printf("-> Tempo de execução:           %d instruções", self->metrics.total_runtime);
  console_printf("-> Tempo total de ócio:         %d instruções", self->metrics.total_halted_time);
  console_printf("\n");
  console_printf("##########         Interrupções         ##########");
  console_printf("-> Tipo IRQ_RESET:     %d interrupções", self->metrics.interrupts[IRQ_RESET]);
  console_printf("-> Tipo IRQ_ERR_CPU:   %d interrupções", self->metrics.interrupts[IRQ_ERR_CPU]);
  console_printf("-> Tipo IRQ_SISTEMA:   %d interrupções", self->metrics.interrupts[IRQ_SISTEMA]);
  console_printf("-> Tipo IRQ_RELOGIO:   %d interrupções", self->metrics.interrupts[IRQ_RELOGIO]);
  console_printf("-> Tipo IRQ_TECLADO:   %d interrupções", self->metrics.interrupts[IRQ_TECLADO]);
  console_printf("-> Tipo IRQ_TELA:      %d interrupções", self->metrics.interrupts[IRQ_TELA]);
  
  console_printf("\n");
  console_printf("##########           Processos          ##########");
  for (int i = 1; i < self->process_counter; i++)
  {
    process_t *proc = self->process_table[i];
    proc_internal_tally(proc);
    proc_metrics_t *proc_metrics = proc_get_metrics_ptr(proc);

    console_printf("--------------------  ID: #%02d  --------------------", proc_get_ID(proc));
    console_printf("-> Tempo de retorno:        %d instruções", proc_metrics->existence_time);
    console_printf("-> Número de preempções:    %d preempções", proc_metrics->preemptions);
    console_printf("-> Tempo médio de resposta: %.2f instruções", proc_metrics->avg_response_time);
  

    console_printf("-> Entrada em estados:");
    console_printf("----------------------------------------");
    console_printf("|   pronto   | bloqueado  | executando |");
    console_printf("| %10d | %10d | %10d |", proc_metrics->ready_count, proc_metrics->blocked_count, proc_metrics->executing_count);
    console_printf("----------------------------------------");

    console_printf("-> Tempo em estados:");
    console_printf("----------------------------------------");
    console_printf("|   pronto   | bloqueado  | executando |");
    console_printf("| %10d | %10d | %10d |", proc_metrics->ready_time, proc_metrics->blocked_time, proc_metrics->executing_time);
    console_printf("----------------------------------------");
    
    console_printf("\n");
  }

    console_printf("--------------------------------------------------");
    console_printf("--------------------------------------------------");
    console_printf("--------------------------------------------------");
    console_printf("------                                      ------");
    console_printf("------                                      ------");
    console_printf("------         Veja o relatório no          ------");
    console_printf("------       arquivo ./log_da_console       ------");
    console_printf("------                                      ------");
    console_printf("------    Ele estará no final do arquivo.   ------");
    console_printf("------                                      ------");
    console_printf("------                                      ------");
    console_printf("--------------------------------------------------");
    console_printf("--------------------------------------------------");
    console_printf("---------------------------------by-brizzi--------");

}


// vim: foldmethod=marker
