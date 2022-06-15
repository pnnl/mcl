/** Class for implementing a fixed memory sorted linked list **/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include <minos.h>
#include <minos_internal.h>
#include "mem_list.h"

#ifdef _DEBUG
#include <assert.h>
void verify_list(List* list){
    uint64_t count = 0;
    int64_t cur_offset = -1;
    int64_t cur_idx = list->head;
    Dprintf("Memory list status:");
    while(cur_idx >= 0){
        dprintf("Cur Idx: %"PRId64"\n", cur_idx);
        assert(list->memory[cur_idx].offset > cur_offset);
        dprintf("\t%"PRId64" Offset: %"PRId64", Size: %"PRIu64", Device:%"PRIu64"\n", 
            cur_idx+1, list->memory[cur_idx].offset, list->memory[cur_idx].size, list->memory[cur_idx].dev);

        cur_offset = list->memory[cur_idx].offset;
        cur_idx = list->memory[cur_idx].next;
        count += 1;        
    }
    if(count != list->length){
        eprintf("List count: %"PRIu64", list length: %"PRIu64"", count, list->length);
        _abort();
    }
    dprintf("Returning from list verify\n");
}
#else
#define verify_list(_l)
#endif


int list_init(List* list){
    uint64_t i = 0;
    for (; i < MCL_MAX_PARTITIONS; i++){
        list->memory[i].prev = i-1;
        list->memory[i].next = i+1;
    }
    list->memory[i-1].next = -1;
    list->head = -1;
    list->tail = -1;
    list->free = 0;
    list->length = 0;
    return 0;
}

mcl_partition_t* list_search(List* list, mcl_partition_t* data){
    int64_t cur = list->head;
    for(uint64_t i = 0; i < list->length; i++, cur = list->memory[cur].next){
        if(list->memory[cur].offset == data->offset)
            return &list->memory[cur];
        if(list->memory[cur].offset > data->offset)
            break;
    }
    return NULL;
}

int64_t list_search_prev(List* list, mcl_partition_t* data){
    int64_t cur = list->head;
    for(;cur >= 0; cur = list->memory[cur].next){
        if(list->memory[cur].offset == data->offset){
            Dprintf("Queried for offset: %"PRIu64" found %"PRId64"", data->offset, cur);
            return cur;
        }
            
        if(list->memory[cur].offset > data->offset)
            break;
    }
    Dprintf("Queried for offset: %"PRIu64" found %"PRId64"", data->offset, cur);
    verify_list(list);
    if(cur >= 0)
        return list->memory[cur].prev;
    else
        return list->tail;
}


mcl_partition_t* list_insert(List* list, mcl_partition_t* data) {
    Dprintf("Inserting into partition list of length %"PRIu64"", list->length);
    if(list->length == MCL_MAX_PARTITIONS){
        return NULL;
    }
    int64_t idx = list->free;
    list->free = list->memory[list->free].next;
    memcpy(list->memory + idx, data, sizeof(mcl_partition_t));
    data = list->memory + idx;
    dprintf("\tIndex of memory: %"PRId64"\n", idx);

    int64_t prev = list_search_prev(list, data);
    dprintf("\tIndex of previous node: %"PRId64"\n", prev);
    data->prev = prev;
    if(prev != -1){
        data->next = list->memory[prev].next;
        list->memory[prev].next = idx;
    } else{
        dprintf("\tUpdating head to index\n");
        data->next = list->head;
        list->head = idx;
    }

    if(list->tail == prev){
        list->tail = idx;
    }
    
    if(data->next != -1)
        list->memory[data->next].prev = idx;

    

    list->length += 1;
    verify_list(list);
    return &list->memory[idx];
}

int list_delete(List* list, mcl_partition_t* data) {
    Dprintf("Deleting from partition list of length %"PRIu64"", list->length);

    if(data->prev != -1)
        list->memory[data->prev].next = data->next;
    else
        list->head = data->next;

    if(data->next != -1)
        list->memory[data->next].prev = data->prev;
    else
        list->tail = data->prev;

    uint64_t idx = ((char*)data - (char*)&list->memory[0]) / sizeof(mcl_partition_t);
    list->memory[idx].next = list->free;
    list->memory[list->free].prev = idx;
    list->free = idx;
    list->length -= 1;
    verify_list(list);
    return 0;
}

mcl_partition_t* list_get(List* list, int64_t idx){
    Dprintf("\t\tGetting index %"PRId64"", idx);
    if(idx == -1 || idx == list->memory[list->tail].next)
        return NULL;
    return &list->memory[idx];
}

