#include <uthash.h>
#include <pthread.h>
#include <atomics.h>
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdio.h>
#include <assert.h>

#include <minos.h>
#include <minos_internal.h>
#include <mem_list.h>

#include "shared_mem.h"

#ifdef _DEBUG
static inline int shm_print(void)
{
	mcl_shm_entry_t*    s;
	Dprintf("\tShared Memory Table:");
	for(s=shm_hash; s != NULL; s=(mcl_shm_entry_t*)(s->hh.next))
		Dprintf("\t\t Entry %s", s->shared_mem->name);
	return 0;
}
#else
#define shm_print()
#endif

static inline uint32_t get_sid(void)
{
	return ainc(&curr_sid);
}

static inline char* _create_name(pid_t pid)
{
    char* name = malloc(25);
    snprintf(name, 25, "%11d_mcl_shm", pid);
    return name;
}

int mcl_shm_init()
{
    pthread_rwlock_init(&shm_tbl_lock, NULL);
    pid_t pid = getpid();
    char* name = _create_name(pid);
    shm_unlink(name);

	Dprintf("Creating shared memory object %s", name);
	int fd = shm_open(name, O_RDWR|O_CREAT|O_EXCL, S_IRWXU|S_IRGRP|S_IROTH);
	if(fd == -1){
		eprintf("Error creating shared memory descriptor. Aborting.");
		perror("shm_open");
		goto err;
	}

	if(ftruncate(fd, (MCL_SHM_POOL_SIZE * sizeof(shared_hdl_t)) + sizeof(shared_hdl_pool_data_t))){
	  eprintf("Error truncating shared memory file. Aborting.");
	  perror("ftruncate");
	  goto err;
	}

    shared_hdl_pool = mmap(NULL, (MCL_SHM_POOL_SIZE * sizeof(shared_hdl_t)) + sizeof(shared_hdl_pool_data_t), PROT_READ|PROT_WRITE,
						  MAP_SHARED, fd, 0);

	if(!shared_hdl_pool){
		eprintf("Error mapping shared memory object. Aborting.");
		perror("mmap");
		goto err_shm_fd;
	}

    pthread_rwlockattr_t psharedm;
    pthread_rwlockattr_init(&psharedm);
    pthread_rwlockattr_setpshared(&psharedm, PTHREAD_PROCESS_SHARED);
    pthread_rwlock_init(&shared_hdl_pool->lock, &psharedm);

    pthread_rwlock_wrlock(&shared_hdl_pool->lock);
    shared_hdl_pool->size = MCL_SHM_POOL_SIZE;
	memset((void*)shared_hdl_pool->pool, 0, MCL_SHM_POOL_SIZE * sizeof(shared_hdl_t));
    for(int i = 0; i < MCL_SHM_POOL_SIZE - 1; i++){
        shared_hdl_pool->pool[i].next = i+1;
        shared_hdl_pool->pool[i].shared_id = -1;
    }
    shared_hdl_pool->pool[MCL_SHM_POOL_SIZE-1].next = -1;
    shared_hdl_pool->pool[MCL_SHM_POOL_SIZE-1].shared_id = -1;
    shared_hdl_head = &shared_hdl_pool->pool[0];
    shared_hdl_pool->refs = 0;
    shared_hdl_pool->ready = 1;
    msync(shared_hdl_pool, (MCL_SHM_POOL_SIZE*sizeof(shared_hdl_t)) + sizeof(struct shared_hdl_pool_data), MS_ASYNC);
    pthread_rwlock_unlock(&shared_hdl_pool->lock);

    close(fd);
    free(name);

    curr_sid = 0;
    return 0;

err_shm_fd:
    close(fd);
err:
    free(name);
    return -1;
}

static int shared_hdl_attatch_to_process(pid_t process)
{
    Dprintf("Trying to attatch to process: %d", process);
    char* name = _create_name(process);

	Dprintf("Opening shared memory object %s", name);
	int fd = shm_open(name, O_RDWR, S_IRWXU|S_IRGRP|S_IROTH);
	if(fd == -1){
		eprintf("Error opening shared memory descriptor. Aborting.");
		perror("shm_open");
        // exit(1);
		goto err;
	}
    Dprintf("Successfully opened shared memory object.");

    shared_hdl_pool_data_t pool_data_init, *pool_data;
    int ret = read(fd, &pool_data_init, sizeof(shared_hdl_pool_data_t));
    if(ret == -1){
        eprintf("Could not read from shared handle file.");
        goto err;
    }

    pool_data = mmap(NULL, (pool_data_init.size * sizeof(shared_hdl_t)) + sizeof(shared_hdl_pool_data_t),
                    PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if(!pool_data){
		eprintf("Error mapping shared memory object. Aborting.");
		perror("mmap");
		goto err_shm_fd;
	}

    pool_entry_t* pool_ent = malloc(sizeof(pool_entry_t));
    pool_ent->key = process;
    pool_ent->pool = pool_data;
    pool_ent->mapped_size = pool_data_init.size;
    HASH_ADD(hh, pools_hash, key, sizeof(pid_t), pool_ent);

    while(!(pool_data->ready == 1)){
        sched_yield();
    }
    ainc(&pool_data->refs);
    Dprintf("Added shared memory object to hash table. Ref counter %"PRIu64"", pool_data->refs);
    msync(&pool_data->refs, sizeof(uint64_t), MS_ASYNC);
    close(fd);
    free(name);
    return 0;

err_shm_fd:
    close(fd);
err:
    free(name);
    return -1;
}

mcl_handle* mcl_allocate_shared_hdl(void)
{
    Dprintf("Allocating handle in shared memory.");
    pthread_rwlock_wrlock(&shared_hdl_pool->lock);
    if(shared_hdl_head == NULL) {
        /**  TODO: need to expand shared memory segment **/
        size_t old_size = (shared_hdl_pool->size);
        munmap(shared_hdl_pool, sizeof(shared_hdl_pool_data_t) + (old_size*sizeof(shared_hdl_t)));

        pid_t pid = getpid();
        char* name = _create_name(pid);
        int fd = shm_open(name, O_RDWR, S_IRWXU);
	    int ret = ftruncate(fd, (old_size * 2 * sizeof(shared_hdl_t)) + sizeof(shared_hdl_pool_data_t));
        if(ret){
            eprintf("Could not truncate file for shared handle memory.");
            pthread_rwlock_unlock(&shared_hdl_pool->lock);
            return NULL;
        }
        shared_hdl_pool = mmap(
            NULL, (old_size * 2 * sizeof(shared_hdl_t)) + sizeof(shared_hdl_pool_data_t), 
            PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0
        );
        close(fd);
        free(name);

        shared_hdl_pool->size = old_size * 2;

        for(int i = old_size; i < (shared_hdl_pool->size) - 1; i++){
            shared_hdl_pool->pool[i].next = i+1;
            shared_hdl_pool->pool[i].shared_id = -1;
        }
        shared_hdl_pool->pool[(shared_hdl_pool->size) - 1].next = -1;
        shared_hdl_pool->pool[(shared_hdl_pool->size) - 1].shared_id = -1;

        shared_hdl_head = &shared_hdl_pool->pool[old_size];
        msync(shared_hdl_pool, sizeof(shared_hdl_pool_data_t) + (shared_hdl_pool->size * sizeof(shared_hdl_t)),
                 MS_ASYNC);
    }
    uint32_t shared_hdl_id = get_sid();
    shared_hdl_t* s_hdl = shared_hdl_head;
    s_hdl->shared_id = shared_hdl_id;
    s_hdl->hdl.flags = MCL_HDL_SHARED;
    Dprintf("Allocated shared handle: %"PRIu32"", shared_hdl_id);
    
    shared_hdl_head = shared_hdl_head->next >= 0 ? &shared_hdl_pool->pool[shared_hdl_head->next] : NULL;
    msync(s_hdl, sizeof(shared_hdl_t), MS_ASYNC | MS_INVALIDATE);
    pthread_rwlock_unlock(&shared_hdl_pool->lock);

    return &s_hdl->hdl;
}

void mcl_free_shared_hdl(mcl_handle* m_hdl)
{
    shared_hdl_t* s_hdl = (shared_hdl_t*)((char*)m_hdl - offsetof(shared_hdl_t, hdl)); 
    //s_hdl->shared_id = -1;
    //msync(s_hdl, sizeof(shared_hdl_t), MS_ASYNC | MS_INVALIDATE);

    pthread_rwlock_wrlock(&shared_hdl_pool->lock);
    int32_t head_idx = shared_hdl_head  ? ((char*)shared_hdl_head - (char*)(&shared_hdl_pool->pool[0]))/sizeof(shared_hdl_t) : -1;
    s_hdl->next = head_idx;
    shared_hdl_head = s_hdl;
    pthread_rwlock_unlock(&shared_hdl_pool->lock);
}

uint32_t mcl_get_shared_hdl_id(mcl_handle* m_hdl)
{
    if((uint64_t)m_hdl < (uint64_t)(&shared_hdl_pool->pool[0]) || (uint64_t) m_hdl > (uint64_t)(&shared_hdl_pool->pool[shared_hdl_pool->size])) {
        return -1;
    }
    shared_hdl_t* s_hdl = (shared_hdl_t*)((char*)m_hdl - offsetof(shared_hdl_t, hdl)); 
    return s_hdl->shared_id;
}

mcl_handle* mcl_get_shared_hdl(pid_t pid, uint32_t sid)
{
    pool_entry_t* pool_ent;
    HASH_FIND(hh, pools_hash, &pid, sizeof(pid_t), pool_ent);
    if(!pool_ent){
        if(shared_hdl_attatch_to_process(pid)){
            Dprintf("Failed to attatch to process");
            return NULL;
        }
            
        Dprintf("May have successfully attatched to process.");
        HASH_FIND(hh, pools_hash, &pid, sizeof(pid_t), pool_ent);
        Dprintf("Executed hash find");
        if(!pool_ent){
            eprintf("Attatched to process incorrectly.");
            return NULL;
        }
    }

    Dprintf("Trying to lock shared handle pool.");
    pthread_rwlock_rdlock(&pool_ent->pool->lock);
    
    if(pool_ent->mapped_size != pool_ent->pool->size){
        size_t new_size = pool_ent->pool->size;
        munmap(pool_ent->pool, (pool_ent->mapped_size * sizeof(shared_hdl_t)) + sizeof(shared_hdl_pool_data_t));
        char* name = _create_name(pid);
        int fd = shm_open(name, O_RDWR, S_IRWXU|S_IRGRP|S_IROTH);
        if(fd == -1){
            eprintf("Error opening shared memory descriptor. Aborting.");
            perror("shm_open");
            return NULL;
        }
        pool_ent->pool = mmap(NULL, (new_size * sizeof(shared_hdl_t)) + sizeof(shared_hdl_pool_data_t),
                        PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        pool_ent->mapped_size = new_size;
    }
    
    /** Need some better data structure here so I don't have to perform a linear search **/
    shared_hdl_t* s_hdl = NULL;
    Dprintf("Looking for process with sid %d.", sid);
    for(int i = 0; i < pool_ent->pool->size; i++){
        if(pool_ent->pool->pool[i].shared_id == sid){
            s_hdl = &pool_ent->pool->pool[i];
            Dprintf("Found process with sid.");
            break;
        }
    }
    pthread_rwlock_unlock(&pool_ent->pool->lock);
    if(!s_hdl){
        Dprintf("Could not find sid");
        return NULL;
    }
    return &(s_hdl->hdl);
}

void* mcl_request_shared_mem(const char* name, size_t size, uint64_t flags)
{
    int name_len = strlen(name);
    if(!(name_len < MCL_MAX_NAME_LEN))
    {
        eprintf("Invalid name, name length is too long.");
        goto ERROR;
    }

    int open_flags = O_RDWR;
    int created_buffer = 0;

    if(flags & MCL_SHARED_MEM_DEL_OLD){
        Dprintf("Deleting old buffer with name: %s", name);
        shm_unlink(name);
        for(uint64_t i = 0; i < mcl_desc.info->ndevs; i++) {
            char buf_name[MCL_MAX_NAME_LEN + MCL_HDL_NAME_EXT_SIZE + 11];
            sprintf(buf_name, "%s%s%"PRIu64"", name, MCL_HDL_NAME_EXT, i);
            shm_unlink(buf_name);    
        }
    }

    if(flags & MCL_SHARED_MEM_NEW){
        Dprintf("Specified new buffer with name: %s", name);
        open_flags |= O_EXCL | O_CREAT;
        created_buffer = 1;
    }

    int fd = shm_open(name, open_flags, S_IRUSR | S_IWUSR);
    if(fd < 0)
    {
        eprintf("Unable to open shared memory. Returned error code: %d", errno);
        goto ERROR;
    }
    if(created_buffer){
        if(ftruncate(fd, sizeof(mcl_shm_t) + size) == -1)
        {
            eprintf("Unable to size shared memory. Returned error code: %d", errno);
            goto ERROR_FD;
        }
    }
    
    mcl_shm_t* shm = mmap(NULL, sizeof(mcl_shm_t) + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(!shm)
    {
        eprintf("Unable to open map memory. Returned error code: %d", errno);
        goto ERROR_FD;
    }
    if(created_buffer){
        pthread_mutexattr_t psharedm;
        pthread_mutexattr_init(&psharedm);
        pthread_mutexattr_setpshared(&psharedm, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&shm->lock, &psharedm);
        pthread_mutex_lock(&shm->lock);
        shm->ref_counter = 0;
        memcpy(shm->name, name, name_len+1);
        shm->devs = 0;
        shm->size = size;
        shm->original_pid = getpid();
        for(uint64_t i = 0; i < CL_MAX_DEVICES; i++){
            //Keep track of which process owns the memory on each device
            shm->creator_pid[i] = -1;
        }
        list_init(&shm->subbuffers);
        shm->mem_id = get_mem_id();
        pthread_mutex_unlock(&shm->lock);
    }
    ainc(&shm->ref_counter);

    mcl_shm_entry_t* el = malloc(sizeof(mcl_shm_entry_t));
    memset(el->key, 0, sizeof(mcl_shm_entry_t));
    if(!el) {
        eprintf("Unable to allocate table entry.");
        goto ERROR_SHM;
    }
    memcpy(el->key, name, name_len+1);
    el->devs = 0;
#ifdef MCL_USE_POCL_SHARED_MEM
    el->hdls = calloc(mcl_desc.info->ndevs, sizeof(cl_shm_hdl));
#endif
    el->hdls_opened = calloc(mcl_desc.info->ndevs, sizeof(uint8_t));
    el->device_ptrs = calloc(mcl_desc.info->ndevs, sizeof(cl_mem));
    el->shared_mem = shm;
    el->ref_counter = 1;

#ifdef MCL_USE_POCL_SHARED_MEM
    for(uint64_t i = 0; i < mcl_desc.info->ndevs; i++){
        if(shm->creator_pid[i] != -1){
            cl_int err;
            pthread_mutex_lock(&shm->lock);
            char name[MCL_MAX_NAME_LEN + MCL_HDL_NAME_EXT_SIZE + 11];
            sprintf(name, "%s%s%"PRIu64"", shm->name, MCL_HDL_NAME_EXT, i);
            el->hdls[i] = clShmOpen(name, size, 0, &err);
            pthread_mutex_unlock(&shm->lock);
            if(err != CL_SUCCESS) {
                eprintf("Detected memory had been created but could not open cl handle");
                goto ERROR_SHM;
            }
        }
    }
#endif

	pthread_rwlock_wrlock(&shm_tbl_lock);
    mcl_shm_entry_t* r;
	HASH_FIND(hh, shm_hash, &(el->key), MCL_MAX_NAME_LEN, r);
	if(r) {
		free(el);
	} else {
        HASH_ADD(hh, shm_hash, key, MCL_MAX_NAME_LEN, el);            
    }
	pthread_rwlock_unlock(&shm_tbl_lock);
    close(fd);
    rdata_add(&shm->data, shm->mem_id, shm->size,  ((flags | MCL_ARG_RESIDENT) & 0xfff) | MCL_ARG_SHARED);
    shm_print();
    return shm->data;

ERROR_SHM:
    munmap(shm, sizeof(mcl_shm_t) + size);
ERROR_FD:
    close(fd);
ERROR:
    return NULL;
}

/**
 * @brief Synchronizes the rdata information with the information in the shared memory table
 *  TODO: This currently takes O(n) time for the number of subbuffers. Is there a faster way?
 * @param rdata
 * @param new_dev Location of new buffer, only used if direcion is -1
 * @param offset
 * @param size
 * @param direction 1: transfer from shared memory to rdata, -1 transfer from rdata to shared mem
 * @return int 
 */
int mcl_update_shared_mem(mcl_rdata* rdata, uint64_t new_dev, uint64_t size, uint64_t offset, int direction){
  
    mcl_shm_t* shm = (mcl_shm_t*)((char*)(rdata->key.addr) - offsetof(mcl_shm_t, data));
    mcl_shm_entry_t* el = NULL;
#ifndef MCL_USE_POCL_SHARED_MEM
    pid_t this_process = getpid();
#endif
    pthread_rwlock_rdlock(&shm_tbl_lock);
    Dprintf("Looking for %s in shared memory hash table.", shm->name);
    HASH_FIND(hh, shm_hash, shm->name, MCL_MAX_NAME_LEN, el);
    if(!el)
    {
        eprintf("Unable to find buffer, not in shared buffer table.");
        pthread_rwlock_unlock(&shm_tbl_lock);
        return -1;
    }
    pthread_rwlock_unlock(&shm_tbl_lock);

    cl_int err;
    cl_mem_flags flags = arg_flags_to_cl_flags(rdata->flags);

    if(direction == 1) {
        pthread_mutex_lock(&shm->lock);
        Dprintf("Successfully locked shared memory lock.");
#ifdef MCL_USE_POCL_SHARED_MEM    
        for(uint64_t device = 0; device < mcl_desc.info->ndevs; device++){
            if(shm->creator_pid[device] != -1 && !(rdata->devices  & (1 << device))){
                char name[MCL_MAX_NAME_LEN + MCL_HDL_NAME_EXT_SIZE + 11];
                sprintf(name, "%s%s%"PRIu64"", shm->name, MCL_HDL_NAME_EXT, device);
                if(!el->hdls[device])
                    el->hdls[device] = clShmOpen(name, shm->size, 0, &err);
                cl_context ctx = res_getClCtx(device);
                el->device_ptrs[device] = clShmGet(ctx, el->hdls[device], flags, NULL, &err);
                rdata->clBuffers[device] = el->device_ptrs[device];
                rdata->devices |= (1 << device);
            }
        }
#endif
    }

    if(rdata->flags & MCL_ARG_DYNAMIC){
        // We should update the subbuffers as well
        mcl_partition_t area = {0, 0, offset+size-1, -1, -1};
        int64_t cur_idx = list_search_prev(&shm->subbuffers, &area);
        mcl_partition_t* cur = list_get(&shm->subbuffers, cur_idx);

        while(cur && cur->offset + cur->size > offset){
            Dprintf("Running iteration with index: %"PRId64", direction: %d, device: %"PRIu64"", cur_idx, direction, cur->dev); 
            if(direction == 1){
                mcl_subbuffer* s = malloc(sizeof(mcl_subbuffer));
                s->offset = cur->offset;
                s->device = cur->dev;
                s->size = cur->size;
                s->refs = 0;

                Node cur_node = Tree_SearchNode(rdata->children, s);
                if(!cur_node){
                    s->offset += cur->size - 1;
                    cur_node = Tree_SearchNode2(rdata->children, s);
                    mcl_subbuffer* overlapping = cur_node ? (mcl_subbuffer*) Node_GetData(cur_node) : NULL;
                    while(overlapping && overlapping->offset + overlapping->size > cur->offset){
                        Dprintf("Deleting subbuffer from tree based on shared buffer list.");
                        Node temp = Tree_PrevNode(rdata->children, cur_node);
                        clReleaseMemObject(overlapping->clBuffer);
                        Tree_DeleteNode(rdata->children, cur_node);
                        free(overlapping);
                        cur_node = temp;
                        overlapping = cur_node ? (mcl_subbuffer*) Node_GetData(cur_node) : NULL;
                    }

                    s->offset = cur->offset;
#ifndef MCL_USE_POCL_SHARED_MEM
                    if(cur->cur_process == this_process){
#else
                    if(1){
#endif
                        cl_buffer_region info = {cur->offset, cur->size};
                        Dprintf("Creating a subbuffer for shared memory.");
                        s->clBuffer = clCreateSubBuffer(el->device_ptrs[cur->dev], flags, CL_BUFFER_CREATE_TYPE_REGION, &info, &err);
                        if(err != CL_SUCCESS){
                            eprintf("Could not create subbuffer, error code: %d", err);
                        }
                    } else {
                        s->device = -1;
                    }                    
                    
                    Tree_Insert(rdata->children, s);
                }
#ifndef MCL_USE_POCL_SHARED_MEM
                else if(cur->cur_process != this_process){
                    mcl_subbuffer* s = (mcl_subbuffer*) Node_GetData(cur_node);
                    clReleaseMemObject(s->clBuffer);
                    Tree_DeleteNode(rdata->children, cur_node);
                    free(s);
                }
#endif
                cur_idx = cur->prev;
            } else {
                if(cur->offset < offset){
                    cur->size = offset - cur->offset;
                    if(cur->offset + cur->size > offset + size) {
                        cur->size = cur->offset + cur->size - (offset + size);
                        cur->offset = offset + size;
                    } 
                    cur_idx = cur->prev;
                } else if(cur->offset + cur->size > offset + size) {
                    cur->size = cur->offset + cur->size - (offset + size);
                    cur->offset = offset + size;
                    cur_idx = cur->prev;
                } else {
                    cur_idx = cur->prev;
                    list_delete(&shm->subbuffers, cur);
                }
            }
            cur = list_get(&shm->subbuffers, cur_idx);
        }

        if(direction == -1) {
            mcl_partition_t partition = {new_dev, size, offset, -1, -1};
#ifndef MCL_USE_POCL_SHARED_MEM
            partition.cur_process = this_process;
#endif
            list_insert(&shm->subbuffers, &partition);
        }
    }

    if(direction == -1)
        pthread_mutex_unlock(&shm->lock);

    return 0;
}

int mcl_delete_shared_mem_subbuffer(void* addr, uint64_t size, uint64_t offset){
    mcl_shm_t* shm = (mcl_shm_t*)((char*)(addr) - offsetof(mcl_shm_t, data));
    Dprintf("Trying to delete subbuffer.");
    pthread_mutex_lock(&shm->lock);
    Dprintf("Successfully locked shm lock.");
    mcl_partition_t area = {0, size, offset, -1, -1};
    mcl_partition_t* cur = list_search(&shm->subbuffers, &area);
    if(!cur){
        eprintf("Trying to delete invalid subbuffer from shared memory");
        pthread_mutex_unlock(&shm->lock);
        return -1;
    }
    list_delete(&shm->subbuffers, cur);
    pthread_mutex_unlock(&shm->lock);
    return 0;
}

/**
 * @brief Returns a shared version of the rdata buffer on the desired device
 * 
 * @param rdata 
 * @param dev 
 * @param flags 
 * @return cl_mem 
 */
cl_mem mcl_get_shared_mem(mcl_rdata* rdata, uint64_t dev, uint64_t flags)
{
    /** Currently we would implement data movement in MCL like we do for all other buffers.
     * Potentially, implementing the movement for shared buffers inside of POCL would allow us 
     * to exploit faster data movement methods (i.e. GPU direct). Ther current struggle with that
     * approach is creating synchronization across processes or across heterogeneous devices.
     */

    cl_int err;
    mcl_shm_t* shm = (mcl_shm_t*)((char*)(rdata->key.addr) - offsetof(mcl_shm_t, data));
    mcl_shm_entry_t* el = NULL;

    pthread_rwlock_rdlock(&shm_tbl_lock);
    Dprintf("Looking for %s in shared memory hash table.", shm->name);
    HASH_FIND(hh, shm_hash, shm->name, MCL_MAX_NAME_LEN, el);
    if(!el)
    {
        eprintf("Unable to find buffer, not in shared buffer table.");
        pthread_rwlock_unlock(&shm_tbl_lock);
        return NULL;
    }
    pthread_rwlock_unlock(&shm_tbl_lock);

#ifdef MCL_USE_POCL_SHARED_MEM
    uint64_t hdl_flags = 0;
    pid_t pid = getpid();
    if(cas(&shm->creator_pid[dev], -1, pid) == 0) {
        eprintf("Memory created on another device. Need to call mcl_shm_update not get");
    }
    shm->devs |= (1 << dev);
    char name[MCL_MAX_NAME_LEN + MCL_HDL_NAME_EXT_SIZE + 11];
    sprintf(name, "%s%s%"PRIu64"", shm->name, MCL_HDL_NAME_EXT, dev);
    hdl_flags |= O_CREAT | O_TRUNC;

    Dprintf("Opening shared memory handle.");
    el->hdls[dev] = clShmOpen(name, shm->size, hdl_flags, &err);
    el->hdls_opened[dev] = 1;

    Dprintf("Creating device pointers for shared memory.");
    el->devs |= (1 << dev);
    cl_context ctx = res_getClCtx(dev);
    el->device_ptrs[dev] = clShmGet(ctx, el->hdls[dev], CL_MEM_READ_WRITE, NULL, &err);
#else
    el->hdls_opened[dev] = 1;
    Dprintf("Creating device pointers for shared memory.");
    el->devs |= (1 << dev);
    cl_context ctx = res_getClCtx(dev);
    el->device_ptrs[dev] = clCreateBuffer(ctx, CL_MEM_READ_WRITE, shm->size, NULL, &err);
#endif
    return el->device_ptrs[dev];
}

int mcl_release_shared_mem(void* buffer)
{
    mcl_shm_t* shm = (mcl_shm_t*)((char*)buffer - offsetof(mcl_shm_t, data));
    mcl_shm_entry_t* el = NULL;

    pthread_rwlock_rdlock(&shm_tbl_lock);
    Dprintf("Looking for %s in shared memory hash table.", shm->name);
    HASH_FIND(hh, shm_hash, shm->name, MCL_MAX_NAME_LEN, el);
    if(!el)
    {
        eprintf("Unable to find buffer, not in shared buffer table.");
        pthread_rwlock_unlock(&shm_tbl_lock);
        return -1;
    }
    pthread_rwlock_unlock(&shm_tbl_lock);

    Dprintf("Decrementing reference counter on shared memory: %s.", shm->name);
    adec(&el->ref_counter);
    adec(&shm->ref_counter);
    if(shm->ref_counter == 0){
        /** TODO: Can we free the GPU memory here **/
    }
    return 0;
}

int mcl_is_shared_mem_owner(void* buffer, uint64_t dev){
    pid_t pid = getpid();
    mcl_shm_t* shm = (mcl_shm_t*)((char*)buffer - offsetof(mcl_shm_t, data));
    return shm->creator_pid[dev] == pid;
}

int mcl_release_device_shared_mem(void* buffer, uint64_t dev)
{
    /** This method will destroy the cl_mem pointer for the device. */
    eprintf("Releasing shared memory.");

    mcl_shm_t* shm = (mcl_shm_t*)((char*)buffer - offsetof(mcl_shm_t, data));
    mcl_shm_entry_t* el = NULL;

    pthread_rwlock_wrlock(&shm_tbl_lock);
    HASH_FIND(hh, shm_hash, shm->name, MCL_MAX_NAME_LEN, el);
    if(!el)
    {
        eprintf("Unable to release shared buffer, not in shared buffer table. Name: %s, This PID: %d", shm->name, getpid());
        pthread_rwlock_unlock(&shm_tbl_lock);
        return 1;
    }
    pthread_rwlock_unlock(&shm_tbl_lock);

    if(!(el->devs & (1 << dev)))
    {
        eprintf("Shared memory is not currently on that device.");
        return 1;
    }

    pid_t pid = getpid();
#ifdef MCL_USE_POCL_SHARED_MEM
    int owner = cas(&shm->creator_pid[dev], pid, -1);
    uint64_t flags = owner ? CL_MEM_SHM_FREE_DEVICE_MEM : 0;
    clShmRelease(el->device_ptrs[dev], el->hdls[dev], flags);
    clShmClose(el->hdls[dev]);
    el->hdls[dev] = NULL;
#else
    clReleaseMemObject(el->device_ptrs[dev]);
#endif
    el->device_ptrs[dev] = NULL;
    el->devs &= ~(1 << dev);

#ifdef MCL_USE_POCL_SHARED_MEM
    if(owner){
        int64_t cur_idx = shm->subbuffers.head;
		mcl_partition_t* cur;
		while(cur_idx >= 0){
			cur = list_get(&shm->subbuffers, cur_idx);
			cur_idx = cur->next;
			if(cur->dev == dev){
				list_delete(&shm->subbuffers, cur);
			}
		}
    }
#else
    int64_t cur_idx = shm->subbuffers.head;
    mcl_partition_t* cur;
    while(cur_idx >= 0){
        cur = list_get(&shm->subbuffers, cur_idx);
        cur_idx = cur->next;
        if(cur->dev == dev  && cur->cur_process == pid){
            list_delete(&shm->subbuffers, cur);
        }
    }
#endif
    return 0;
}

pid_t mcl_get_shared_mem_pid(void* buffer)
{
    return ((mcl_shm_t*)((char*)buffer - offsetof(mcl_shm_t, data)))->original_pid;
}

int mcl_shm_free(void)
{
    int ret = 0;
    Dprintf("Freeing shared memory for process: %d", getpid());

    /** Free shared handle pool **/
    
    pool_entry_t *p_ent, *p_tmp;
    HASH_ITER(hh, pools_hash, p_ent, p_tmp){
        adec(&p_ent->pool->refs);
        Dprintf("Removing shared memory from hash table. Ref counter %"PRIu64"", p_ent->pool->refs);
        munmap(p_ent->pool, sizeof(shared_hdl_pool_data_t) + (p_ent->pool->size * sizeof(shared_hdl_t)));
        HASH_DEL(pools_hash, p_ent);
        free(p_ent);
    }

    Dprintf("Waiting on %"PRIu64" processes to release shared handle pool.", shared_hdl_pool->refs);
    while(shared_hdl_pool->refs)
        sched_yield();

    munmap(shared_hdl_pool, sizeof(shared_hdl_pool_data_t) + (shared_hdl_pool->size * sizeof(shared_hdl_t)));
    char* name = _create_name(getpid());
    shm_unlink(name);

    pthread_rwlock_wrlock(&shm_tbl_lock);

    Dprintf("Freeing shm table...");
    mcl_shm_entry_t *el, *tmp;

    // First remove any references we have to shared memory
    HASH_ITER(hh, shm_hash, el, tmp) {
        shm_print();
        Dprintf("Reference counter for memory: %"PRIu32"", el->ref_counter);
        while(el->ref_counter){
            adec(&el->shared_mem->ref_counter);
            adec(&el->ref_counter);
        }
    }

    
    HASH_ITER(hh, shm_hash, el, tmp) {
        shm_print();
        //eprintf("Waiting on %"PRIu32" processes to release shared memory.", el->shared_mem->ref_counter);
        while(el->shared_mem->ref_counter){
            sched_yield();
        }
        for(int dev = 0; dev < mcl_desc.info->ndevs; dev++) {
            if(!(el->devs & (1 << dev)))
                continue;
            //mcl_release_device_shared_mem(&el->shared_mem->data, dev);
#ifdef MCL_USE_POCL_SHARED_MEM
            pid_t pid = getpid();
            int owner = cas(&el->shared_mem->creator_pid[dev], pid, -1);
            uint64_t flags = owner ? CL_MEM_SHM_FREE_DEVICE_MEM : 0;
            clShmRelease(el->device_ptrs[dev], el->hdls[dev], flags);
            clShmClose(el->hdls[dev]);
#else
            clReleaseMemObject(el->device_ptrs[dev]);
#endif
        }

        HASH_DEL(shm_hash, el);
        free(el->device_ptrs);
#ifdef MCL_USE_POCL_SHARED_MEM
        free(el->hdls);
#endif
        char* name = strdup(el->shared_mem->name);
        munmap(el->shared_mem, sizeof(mcl_shm_t) + el->shared_mem->size);
        shm_unlink(name);
        free(el);
        free(name);
    }
    pthread_rwlock_unlock(&shm_tbl_lock);
    pthread_rwlock_destroy(&shm_tbl_lock);
    Dprintf("Shared memory freed.");
    return ret;
}

