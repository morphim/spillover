/*
Copyright (c) 2015 drugaddicted - c17h19no3 AT openmailbox DOT org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdlib.h>
#include "list.h"

void spo_list_init(spo_list_t *list)
{
    list->head = NULL;
    list->tail = NULL;
    list->length = 0;
}

void spo_list_destroy(spo_list_t *list)
{
    spo_list_item_t *next;
    spo_list_item_t *current = list->head;

    while (current != NULL)
    {
        next = current->next_item;
        free(current);
        current = next;
    }

    list->head = NULL;
    list->tail = NULL;
    list->length = 0;
}

spo_list_item_t *spo_list_add_item(spo_list_t *list, void *data)
{
    spo_list_item_t *current = list->tail;
    spo_list_item_t *new_item = (spo_list_item_t *)malloc(sizeof(spo_list_item_t));

    if (new_item == NULL)
        return NULL;

    new_item->data = data;

    if (current == NULL) /* list is empty */
    {
        new_item->next_item = NULL;
        new_item->prev_item = NULL;

        list->head = new_item;
        list->tail = new_item;
        list->length = 1;
        return new_item;
    }

    /* insert tail */
    new_item->prev_item = current;
    new_item->next_item = NULL;
    current->next_item = new_item;

    list->tail = new_item;

    ++list->length;
    return new_item;
}

spo_list_item_t *spo_list_remove_item(spo_list_t *list, spo_list_item_t *item)
{
    spo_list_item_t *next = item->next_item;
    spo_list_item_t *prev = item->prev_item;

    if (next != NULL)
        next->prev_item = prev;
    else
    {
        /* replace tail */
        list->tail = prev;
    }

    if (prev != NULL)
        prev->next_item = next;
    else
    {
        /* replace head */
        list->head = next;
    }

    free(item);

    --list->length;
    return next;
}

void spo_list_remove_items_by_data(spo_list_t *list, void *data)
{
    spo_list_item_t *current = list->head;

    while (current != NULL)
    {
        if (current->data == data)
            current = spo_list_remove_item(list, current);
        else
            current = current->next_item;
    }
}
