#ifndef LIST_H
#define LIST_H

typedef struct list list;
struct list
{
    list *next;
    void *data;
};

list *list_create();
list *list_append(list *l, void *data);
list *list_insert_at_index(list *l, void *data, int index);
list *list_insert(list *l, void *data);
void list_print(list *l);
list *list_pop(list *l, void **v);
void *list_get(list *l, int index);
int list_lenght(list *l);

#endif