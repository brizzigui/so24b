#include "aleatorio.h"

#include <stdlib.h>
#include <time.h>
#include <assert.h>

err_t rand_leitura(void *disp, int id, int *pvalor)
{
    *pvalor = rand();
    return 0;
}
