#include <stdlib.h>
#include <stdio.h>

#include "list.h"

list *list_create()
{
    return NULL;
}

list *list_append(list *l, void *data)
{
    list *ptr = l;
    list *aux = NULL;

    while (ptr != NULL) 
    {        
        aux = ptr;
        ptr = ptr->next;
    } 

    list *new = (list *)malloc(sizeof(list));
    new->next = NULL;
    new->data = data;

    if (aux != NULL)
    {
        aux->next = new;
    }

    else
    {
        l = new;
    }
    
    return l;
}

list *list_insert_at_index(list *l, void *data, int index)
{
    int counter = 0;

    list *ptr = l;
    list *aux = NULL;

    while (ptr != NULL && counter < index)
    {
        counter++;
        aux = ptr;
        ptr = ptr->next;
    }

    list *new = (list *)malloc(sizeof(list));
    new->data = data;

    if (aux == NULL)
    {
        l = new;
    }

    else
    {
        aux->next = new;
    }
    
    new->next = ptr;    
    
    return l;
}

list *list_insert(list *l, void *data)
{
    return list_insert_at_index(l, data, 0);
}

void list_print(list *l)
{
    int counter = 0;
    list *ptr = l;

    while (ptr != NULL)
    {
        printf("%d: %d\n", counter, *(int *)ptr->data);
        ptr = ptr->next;
        counter++;
    }
}

list *list_pop(list *l, void **v)
{
    if(l == NULL)
    {
        return l;
    }

    else
    {
        *v = l->data;
        l = l->next;
    }

    return l;
}

void *list_get(list *l, int index)
{
    int counter = 0;

    list *ptr = l;

    while (ptr != NULL && counter < index)
    {
        counter++;
        ptr = ptr->next;
    }

    if(ptr == NULL) return NULL;

    return ptr->data;
}

int list_lenght(list *l)
{
    int counter = 0;
    list *ptr = l;
    while (ptr != NULL)
    {
        counter++;
        ptr = ptr->next;
    }

    return counter;
}