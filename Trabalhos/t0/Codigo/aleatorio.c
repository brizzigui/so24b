#include "aleatorio.h"

#include <stdlib.h>
#include <time.h>
#include <assert.h>

struct aleatorio_t
{
    
};

aleatorio_t* aleatorio_cria()
{
    srand(time(NULL));
    aleatorio_t* ptr = (aleatorio_t *)malloc(sizeof(aleatorio_t));
    return ptr;
}

err_t rand_leitura(void *disp, int id, int *pvalor)
{
    *pvalor = rand();
    return 0;
}
