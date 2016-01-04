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

#ifndef SPO_INDEX_H
#define SPO_INDEX_H

#include "pstdint.h"
#include "common.h"

typedef struct
{
    uint32_t key;
    void *data;
} spo_index_item_t;

typedef struct
{
    spo_index_item_t *items;
    uint32_t length;
    uint32_t size; /* allocated size */
} spo_index_t;

#define SPO_INDEX_FIRST(index) ((index)->items)
#define SPO_INDEX_NEXT(index, current) ((current) + 1)
#define SPO_INDEX_VALID(index, current) ((current) < (index)->items + (index)->length) /* fails if index is empty */

void spo_index_init(spo_index_t *index);
void spo_index_destroy(spo_index_t *index);
spo_index_item_t *spo_index_find_pos_by_key(spo_index_t *index, uint32_t key);
spo_index_item_t *spo_index_insert_item_after(spo_index_t *index, spo_index_item_t *after_item, uint32_t key, void *data);
spo_index_item_t *spo_index_remove_item(spo_index_t *index, spo_index_item_t *item);

#endif
