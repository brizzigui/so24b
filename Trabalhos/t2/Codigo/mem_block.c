#include <stdlib.h>
#include <stdbool.h>

#include "memoria.h"
#include "mem_block.h"

// block é de bloco, não de bloqueio

// o array de mem_blocks serve como um rastreador da memória física, permitindo
// saber se um bloco está ocupado ou livre e que processo o ocupa, se há algum

mem_block_t *create_mem_blocks(int tamanho)
{
    // o tamanho é o número de páginas físicas rastreadas

    mem_block_t *blocks = malloc(sizeof(mem_block_t) * tamanho);
    for (size_t i = 0; i < tamanho; i++)
    {
        if (i < 1)
        {
            blocks[i].used = true;
        }
        
        else
        {
            blocks[i].used = false;
            blocks[i].user = 0;
        }
    }

    return blocks;
}