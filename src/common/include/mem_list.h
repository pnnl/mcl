#ifndef MEM_LIST_H
#define MEM_LIST_H

#include <inttypes.h>
#include <minos_internal.h>

#define MCL_MAX_PARTITIONS 255

//Forward declaration
typedef struct mcl_partition_struct {
	uint64_t dev;
	size_t size;
	off_t  offset;
	int64_t next;
	int64_t prev;
#ifndef MCL_USE_POCL_SHARED_MEM
    pid_t cur_process;
#endif
} mcl_partition_t;

typedef struct List {
    int64_t head;
    int64_t tail;
	int64_t free;
    uint64_t length;
    struct mcl_partition_struct memory[MCL_MAX_PARTITIONS];
} List;



List*                list_create(mcl_partition_t* memory, uint64_t max_size, uint64_t cur_len);
int                  list_init(List* list);
mcl_partition_t*     list_search(List* list, mcl_partition_t* data);
int64_t              list_search_prev(List* list, mcl_partition_t* data);
mcl_partition_t*     list_insert(List* list, mcl_partition_t* data);
int                  list_delete(List* list, mcl_partition_t* data);
mcl_partition_t*     list_get(List* list, int64_t idx);

#endif