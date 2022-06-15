#ifndef MCL_SHARED_MEM_PRVIATE

#define MCL_SHARED_MEM_PRVIATE
#include <uthash.h>
#include <pthread.h>
#include <sys/types.h>

#include <minos.h>
#include <minos_internal.h>
#include <mem_list.h>

#define MCL_SHM_POOL_SIZE     (1UL<<14)

extern   mcl_desc_t      mcl_desc;
extern   mcl_resource_t* mcl_res;

typedef struct shared_hdl_struct {
    mcl_handle hdl;
    int32_t   shared_id;
    int32_t   next; /** Offset of next free block **/
} shared_hdl_t;

typedef struct shared_hdl_pool_data {
    pthread_rwlock_t lock;
    size_t           size;
    uint64_t         refs;
    uint8_t          ready;
    shared_hdl_t     pool[];
} shared_hdl_pool_data_t;


shared_hdl_pool_data_t* shared_hdl_pool;
shared_hdl_t*           shared_hdl_head;

typedef struct pool_entry_struct {
    pid_t                   key;
    shared_hdl_pool_data_t* pool;
    size_t                  mapped_size;
    UT_hash_handle          hh;
} pool_entry_t;

pool_entry_t* pools_hash;
uint32_t      curr_sid;


typedef struct mcl_shm_entry_struct {
    char           key[MCL_MAX_NAME_LEN];
    uint32_t       ref_counter;
    mcl_shm_t*     shared_mem;
    uint64_t       devs;
    uint8_t*       hdls_opened;
#ifdef MCL_USE_POCL_SHARED_MEM
	cl_shm_hdl*    hdls;
#endif
	cl_mem*        device_ptrs;
    List*          partition_list;
    UT_hash_handle hh;
} mcl_shm_entry_t;

pthread_rwlock_t shm_tbl_lock;
mcl_shm_entry_t* shm_hash;

#endif
