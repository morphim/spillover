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

#ifndef SPO_LIST_H
#define SPO_LIST_H

#include "pstdint.h"
#include "common.h"

typedef struct spo_list_item
{
    struct spo_list_item *next_item;
    struct spo_list_item *prev_item;
    void *data;
} spo_list_item_t;

typedef struct
{
    spo_list_item_t *head;
    spo_list_item_t *tail;
    uint32_t length;
} spo_list_t;

#define SPO_LIST_FIRST(list) ((list)->head)
#define SPO_LIST_NEXT(list, current) ((current)->next_item)
#define SPO_LIST_VALID(list, current) ((current) != NULL)

void spo_list_init(spo_list_t *list);
void spo_list_destroy(spo_list_t *list);
spo_list_item_t *spo_list_add_item(spo_list_t *list, void *data);
spo_list_item_t *spo_list_remove_item(spo_list_t *list, spo_list_item_t *item);
void spo_list_remove_items_by_data(spo_list_t *list, void *data);

#endif
