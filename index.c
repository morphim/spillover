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
#include <string.h>
#include "index.h"

#define SPO_INDEX_ALLOCATION_INCREMENT 10

/* unsigned arithmetic does all the magic */
#define SPO_WRAPPED_LESS(a, b) ((int32_t)((a)-(b)) < 0)

void spo_index_init(spo_index_t *index)
{
    index->items = NULL;
    index->length = 0;
    index->size = 0;
}

void spo_index_destroy(spo_index_t *index)
{
    if (index->items != NULL)
        free(index->items);

    index->items = NULL;
    index->length = 0;
    index->size = 0;
}

spo_index_item_t *spo_index_find_pos_by_key(spo_index_t *index, uint32_t key)
{
    spo_index_item_t *item;
    int middle;
    int low = 0;
    int high = index->length - 1;

    while (low <= high)
    {
        middle = (low + high) / 2;
        item = index->items + middle;

        if (item->key == key)
            return item;

        if (SPO_WRAPPED_LESS(key, item->key))
            high = middle - 1;
        else
            low = middle + 1;
    }

    if (high >= 0)
        return index->items + high;

    return NULL;
}

spo_index_item_t *spo_index_insert_item_after(spo_index_t *index, spo_index_item_t *after_item, uint32_t key, void *data)
{
    spo_index_item_t *new_item;

    if (index->size == index->length)
    {
        uint32_t new_size = index->size + SPO_INDEX_ALLOCATION_INCREMENT;
        spo_index_item_t *new_items = (spo_index_item_t *)realloc(index->items, new_size * sizeof(spo_index_item_t));

        if (new_items == NULL)
            return NULL;

        if (after_item == NULL)
            new_item = new_items; /* insert as head */
        else
            new_item = new_items + (after_item - index->items) + 1; /* insert regular item */

        index->items = new_items;
        index->size = new_size;
    }
    else
    {
        if (after_item == NULL)
            new_item = index->items; /* insert as head */
        else
            new_item = after_item + 1; /* insert regular item */
    }

    memmove(new_item + 1, new_item, (index->length - (new_item - index->items)) * sizeof(spo_index_item_t));

    new_item->key = key;
    new_item->data = data;

    ++index->length;
    return new_item;
}

spo_index_item_t *spo_index_remove_item(spo_index_t *index, spo_index_item_t *item)
{
    spo_index_item_t *next_item = item + 1;

    memmove(item, next_item, (index->length - (next_item - index->items)) * sizeof(spo_index_item_t));

    --index->length;
    return item;
}
