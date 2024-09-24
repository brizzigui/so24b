#ifndef ALEATORIO_H
#define ALEATORIO_H

#include "err.h"

typedef struct aleatorio_t aleatorio_t;
aleatorio_t* aleatorio_cria();

// Função para dispositivo de número aleatório
err_t rand_leitura(void *disp, int id, int *pvalor);

#endif