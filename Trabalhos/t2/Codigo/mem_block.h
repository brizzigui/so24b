#ifndef MEM_BLOCK_H
#define MEM_BLOCK_H

struct mem_block_t
{
  bool used;
  int user;
};

typedef struct mem_block_t mem_block_t;
mem_block_t *create_mem_blocks(int tamanho);

#endif