#include <uthash.h>
#include <pthread.h>

#include <minos.h>
#include <minos_internal.h>
#include <atomics.h>

#include <nbhashmap.h>

extern mcl_desc_t         mcl_desc;
extern mcl_resource_t*    mcl_res;

/** FIXME: both rdata hash and memid_map only grows and never shrinks, possibly creating lots of memory overhead.
 * This is useful in making both of them relatively lock free. There are other hashmap implementations that
 * are both lock free and allow deletions, but they are more complex and there doesn't seem to be an available c implementation for
 * them. Writing that data structure is definately a TODO**/
HashMap *rdata_hash;

pthread_rwlock_t memid_map_lock;
unsigned long* memid_map;
uint32_t	   memid_map_size;
uint32_t	   max_memid;

#define rdata_print()

void memid_map_resize(){
	pthread_rwlock_unlock(&memid_map_lock);
	pthread_rwlock_wrlock(&memid_map_lock);
	memid_map = realloc(memid_map, 2 * memid_map_size * sizeof(unsigned long));
	memid_map_size *= 2;
	pthread_rwlock_unlock(&memid_map_lock);
	pthread_rwlock_rdlock(&memid_map_lock);
}

unsigned int rdata_hash_fcn(void* key)
{
	unsigned hashv;
	HASH_JEN(key, sizeof(mcl_rdata_key), hashv);
	return hashv;
}

int rdata_key_equals(void* left, void* right)
{
	if(!left || !right) return left == right;
	mcl_rdata_key* l_key = (mcl_rdata_key*) left;
	mcl_rdata_key* r_key = (mcl_rdata_key*) right;

	if(l_key->addr == 0x0 || r_key->addr == 0x0)
		return 0;
	
	return (l_key->addr == r_key->addr);
}

void rdata_key_free(void* key)
{
	mcl_rdata_key* k = (mcl_rdata_key*) key;
	k->addr = 0x0;
	free(k);
}

int rdata_avl_comp(void* data1, void* data2)
{
	return ((mcl_subbuffer*)data1)->offset - ((mcl_subbuffer*)data2)->offset;
}

void rdata_avl_print(void* data)
{
	mcl_subbuffer* s = (mcl_subbuffer*) data;
	printf("{%"PRIu64", %"PRId64"} : %"PRIu64"", s->offset, s->size, s->device);
}

int rdata_init(void)
{
	rdata_hash = hashmap_new(rdata_key_equals, rdata_hash_fcn, rdata_key_free);
	memid_map_size = 256;
	memid_map = malloc(memid_map_size * sizeof(unsigned long));
	pthread_rwlock_init(&memid_map_lock, NULL);
	return 0;
}

mcl_rdata* rdata_add(void* addr, uint32_t id, size_t size, uint64_t flags)
{
	mcl_rdata* rdata = malloc(sizeof(mcl_rdata));
	rdata->key.addr = (unsigned long) addr;
	void* key = malloc(sizeof(mcl_rdata_key));
	memcpy(key, &rdata->key, sizeof(mcl_rdata_key));

	if(hashmap_putif(rdata_hash, (void*)key, rdata, 0)){
		eprintf("Could not push new value. Key already exists in hash table.");
		free(rdata);
		return NULL;
	}

	rdata->devices = 0;
	rdata->flags = flags;
	rdata->host_is_valid = 1;
	rdata->size = size;
	rdata->id = id;
	memset(&rdata->clBuffers[0], 0, CL_MAX_DEVICES * sizeof(cl_mem));
	rdata->num_partitions = 0;
	rdata->refs = 1;
	rdata->evicted = 0;

	rdata->children = Tree_New(rdata_avl_comp, rdata_avl_print);
	pthread_rwlock_init(&rdata->tree_lock, NULL);

	pthread_rwlock_rdlock(&memid_map_lock);
	if(rdata->id >= memid_map_size){
		memid_map_resize();
	}
	memid_map[rdata->id] = rdata->key.addr;
	if(rdata->id > max_memid)
		max_memid = rdata->id;

	pthread_rwlock_unlock(&memid_map_lock);

	Dprintf("Added %p to rdata memory with id %"PRIu32".", (void*) rdata->key.addr, rdata->id);

 	return rdata;
}

static inline mcl_rdata* rdata_get_internal(mcl_rdata_key* key)
{
	//Dprintf("\t\t Looking for <%p>", (void*)key->addr);
	mcl_rdata*  r = hashmap_get(rdata_hash, key);
	if(!r) Dprintf("\t\t  Did not find key in hashtable or key was deleted.");
	return r;
}

mcl_rdata* rdata_get(void* addr, int hold)
{
	mcl_rdata* ret = NULL;
	mcl_rdata_key  key = {(unsigned long) addr, };
	
	if(!(ret = rdata_get_internal(&key))) return NULL;
	
	if(hold) {
		ainc(&(ret->refs));
		//Dprintf("\t\t Ref counter for memory %"PRIu32": %"PRIu32"", ret->id, ret->refs);
	}

	return ret;
}

void rdata_move_device_memory(mcl_rdata* rdata, mcl_subbuffer* src, uint64_t dest_dev, cl_command_queue dest_q, cl_int* err)
{
	if(src->device == dest_dev)
		return;
		
	//This shouldn't really ever happen although I don't know that we can ensure it won't
	//pthread_rwlock_unlock(&rdata->tree_lock);
	//while(src->refs > 0){
	//	sched_yield();
	//}
	//pthread_rwlock_wrlock(&rdata->tree_lock);

	
	Dprintf("\t\tMoving buffer with Id: %"PRIu32", Offset:%"PRId64", Size: %"PRIu64", Source device:%"PRId64" Destination device:%"PRIu64"",
			rdata->id, src->offset, src->size, src->device, dest_dev);
    if(src->device >= 0){
        cl_command_queue src_q = __get_queue(src->device);
        cl_event write_event;
        cl_mem old_mem = src->clBuffer;
        *err = clEnqueueReadBuffer(src_q, src->clBuffer, CL_FALSE, 0, src->size, (void*)rdata->key.addr, 0, NULL, &write_event);
        clWaitForEvents(1, &write_event);
        clReleaseMemObject(old_mem);
    }
	*err |= clEnqueueWriteBuffer(dest_q, rdata->clBuffers[dest_dev], 0, src->offset, src->size, (void*)rdata->key.addr, 0, NULL, NULL);
}

cl_mem rdata_get_mem(mcl_rdata* rdata, uint64_t device, size_t size, off_t offset, uint64_t flags, cl_command_queue queue, cl_int* err)
{
	Dprintf("Getting device memory for memory %"PRIu32", on device %"PRIu64" at offset %"PRIu64"",
			rdata->id, device, offset);
        pthread_rwlock_wrlock(&rdata->tree_lock);
	if(rdata->flags & MCL_ARG_SHARED)
		mcl_update_shared_mem(rdata, 0, size, offset, 1);
    Dprintf("Rdata devices: %"PRIx64"", rdata->devices);

	if(!(rdata->flags & MCL_ARG_DYNAMIC)){
		if(rdata->size != size){
			eprintf("Subbuffers are not allowed for read only memory. flags: %"PRIx64", rdata size: %"PRIu64", size: %"PRIu64"", rdata->flags, rdata->size, size);
			if(rdata->flags & MCL_ARG_SHARED)
				mcl_update_shared_mem(rdata, 0, 0, 0, -1);
			pthread_rwlock_unlock(&rdata->tree_lock);
			return NULL;
		}

		cl_context context = res_getClCtx(device);
		uint64_t cl_flags = arg_flags_to_cl_flags(rdata->flags);

		if(rdata->devices & (1 << device)){
			if(rdata->flags & MCL_ARG_SHARED)
				mcl_update_shared_mem(rdata, device, size, offset, -1);

			Dprintf("\tGetting buffer for memory: %"PRIu32", device: %"PRIu64".", rdata->id, device);
			if(flags & MCL_ARG_REWRITE){
				Dprintf("\t\tWriting memory.");
				*err = clEnqueueWriteBuffer(queue, rdata->clBuffers[device], 0, 0, rdata->size, 
						(void*)rdata->key.addr, 0, NULL, NULL);
			}
            pthread_rwlock_unlock(&rdata->tree_lock);
			return rdata->clBuffers[device];
		}
		
		/** TODO: If shared memory, this needs to change **/
		if(rdata->flags & MCL_ARG_SHARED){
			rdata->clBuffers[device] = mcl_get_shared_mem(rdata, device, flags);
		} else {
			Dprintf("\tCreating buffer for memory: %"PRIu32", device: %"PRIu64".", rdata->id, device);
			rdata->clBuffers[device] = clCreateBuffer(context, cl_flags, rdata->size, NULL, err);
		}
		if(flags & MCL_ARG_INPUT || rdata->evicted || (flags & MCL_ARG_REWRITE)){
			Dprintf("\t\tWriting memory.");
			*err = clEnqueueWriteBuffer(queue, rdata->clBuffers[device], 0, 0, rdata->size, 
					(void*)rdata->key.addr, 0, NULL, NULL);
			rdata->evicted = 0;
		}

		rdata->devices |= (1 << device);
		if(rdata->flags & MCL_ARG_SHARED)
			mcl_update_shared_mem(rdata, device, size, offset, -1);
		pthread_rwlock_unlock(&rdata->tree_lock);
		return rdata->clBuffers[device];
	}

	uint64_t cl_flags = arg_flags_to_cl_flags(flags);

	mcl_subbuffer tosearch = { offset, 0, 0, 0 };
	Node n = Tree_SearchNode(rdata->children, (void*) &tosearch);
	if(n != NULL){
		Dprintf("\tFound subbuffer for memory: %"PRIu32", device: %"PRIu64", offset: %"PRId64" size: %"PRIu64".",
				rdata->id, device, offset, size);
		mcl_subbuffer* existing = (mcl_subbuffer*) Node_GetData(n);
		if(existing->size == size && existing->device == device){
			ainc(&existing->refs);
			Dprintf("\tReferences for offset %"PRIu64": %"PRIu64"", existing->offset, existing->refs);
			if (flags & MCL_ARG_REWRITE) {
				clEnqueueWriteBuffer(queue, rdata->clBuffers[device], CL_FALSE, offset, size, 
							(void*)rdata->key.addr + offset, 0, NULL, NULL);
			}
			if(rdata->flags & MCL_ARG_SHARED)
				mcl_update_shared_mem(rdata, device, size, offset, -1);
			pthread_rwlock_unlock(&rdata->tree_lock);
			return existing->clBuffer;
		} else if (existing->size == size) {
			if(rdata->clBuffers[device] == NULL){
				cl_context ctx = res_getClCtx(device);
				if(rdata->flags & MCL_ARG_SHARED)
					rdata->clBuffers[device] = mcl_get_shared_mem(rdata, device, flags);
				else
					rdata->clBuffers[device] = clCreateBuffer(ctx, cl_flags, rdata->size, NULL, err);
				rdata->devices |= (1 << device);
				Dprintf("Rdata devices: %"PRIx64"", rdata->devices);
			}
			if (flags & MCL_ARG_REWRITE) {
				clEnqueueWriteBuffer(queue, rdata->clBuffers[device], CL_FALSE, offset, size, 
							(void*)rdata->key.addr + offset, 0, NULL, NULL);
			} else {
				rdata_move_device_memory(rdata, existing, device, queue, err);
			}
			
			cl_buffer_region info = {offset, size};
			existing->clBuffer = clCreateSubBuffer(rdata->clBuffers[device], cl_flags, CL_BUFFER_CREATE_TYPE_REGION, &info, err);
			existing->device = device;
			ainc(&existing->refs);
			Dprintf("\tReferences for offset %"PRIu64": %"PRIu64"", existing->offset, existing->refs);
			
			if(rdata->flags & MCL_ARG_SHARED)
				mcl_update_shared_mem(rdata, device, size, offset, -1);
			pthread_rwlock_unlock(&rdata->tree_lock);
			return existing->clBuffer;
		}
	}

	//Create the large buffer on the device
    Dprintf("Device ptr %"PRIu64": %p", device, rdata->clBuffers[device]);
	if(rdata->clBuffers[device] == NULL){
		cl_context ctx = res_getClCtx(device);
		if(rdata->flags & MCL_ARG_SHARED){
			rdata->clBuffers[device] = mcl_get_shared_mem(rdata, device, flags);
		} else {
			rdata->clBuffers[device] = clCreateBuffer(ctx, cl_flags, rdata->size, NULL, err);
		}
		rdata->devices |= (1 << device);
        Dprintf("Rdata devices: %"PRIx64"", rdata->devices);
	}

	tosearch.offset = offset + size - 1;
	n = Tree_SearchNode2(rdata->children, (void*) &tosearch);
	Node temp;
	mcl_subbuffer* data = n ? (mcl_subbuffer*)Node_GetData(n) : NULL;
	cl_int err_2;

	if(!(n && data->offset + data->size > offset)){
		if(flags & MCL_ARG_INPUT || flags & MCL_ARG_REWRITE){
			Dprintf("\tWriting memory: %"PRIu32", device: %"PRIu64", offset: %"PRId64" size: %"PRIu64".",
				rdata->id, device, offset, size);
			clEnqueueWriteBuffer(queue, rdata->clBuffers[device], CL_FALSE, offset, size, (void*)rdata->key.addr + offset, 0, NULL, NULL);
		} 
		/** Should I 0 the memory here? **/
	} else {
		int64_t previous_offset = data->offset;
		while(n && data->offset + data->size > offset){
			if(previous_offset - (int64_t)(data->offset + data->size) > 0) {
				uint64_t begin = data->offset + data->size;
				uint64_t cur_size = previous_offset - (data->offset + data->size);
				if(flags & MCL_ARG_INPUT || flags & MCL_ARG_REWRITE){
                                        Dprintf("\t\tWriting to buffer from host memory.");
					clEnqueueWriteBuffer(queue, rdata->clBuffers[device], CL_FALSE, begin, cur_size, 
							(void*)rdata->key.addr + begin, 0, NULL, NULL);
				}
			}

			if(data->offset < offset){
                Dprintf("Splitting the beginning off a subbuffer. Existing: (size: %"PRIu64", offset: %"PRIu64") New: (size: %"PRIu64", offset: %"PRIu64"", data->size, data->offset, size, offset);
				Tree_DeleteNode(rdata->children, n);
				
				clReleaseMemObject(data->clBuffer);
				mcl_subbuffer* beginning = malloc(sizeof(mcl_subbuffer));
				memcpy(beginning, data, sizeof(mcl_subbuffer));
				beginning->size = offset - data->offset;
				cl_buffer_region info = {beginning->offset, beginning->size};
				beginning->clBuffer = clCreateSubBuffer(rdata->clBuffers[beginning->device], cl_flags,
													CL_BUFFER_CREATE_TYPE_REGION, &info, err);
				Tree_Insert(rdata->children, beginning);

				data->offset = offset;
				data->size = data->size - beginning->size;
				info.size = data->offset;
				info.size = data->size;
				data->clBuffer = clCreateSubBuffer(rdata->clBuffers[data->device], cl_flags,
													CL_BUFFER_CREATE_TYPE_REGION, &info, err);
				Tree_Insert(rdata->children, data);
				n = Tree_SearchNode(rdata->children, data);
				ainc(&rdata->num_partitions);
			}
			if(data->offset + data->size > offset + size){
				Dprintf("Splitting the end off a subbuffer. Existing: (size: %"PRIu64", offset: %"PRIu64") New: (size: %"PRIu64", offset: %"PRIu64"", data->size, data->offset, size, offset);
				Tree_DeleteNode(rdata->children, n);
				mcl_subbuffer* end = malloc(sizeof(mcl_subbuffer));
				memcpy(end, data, sizeof(mcl_subbuffer));
				end->offset = offset + size;
				end->size = (data->offset + data->size) - (offset + size);
				cl_buffer_region info = {end->offset, end->size};
				end->clBuffer = clCreateSubBuffer(rdata->clBuffers[end->device], cl_flags, 
													CL_BUFFER_CREATE_TYPE_REGION, &info, err);
				Tree_Insert(rdata->children, end);

				data->size = data->size - end->size;
				info.size = data->offset;
				info.size = data->size;
				data->clBuffer = clCreateSubBuffer(rdata->clBuffers[data->device], cl_flags, 
													CL_BUFFER_CREATE_TYPE_REGION, &info, err);
				Tree_Insert(rdata->children, data);
				n = Tree_SearchNode(rdata->children, data);
				ainc(&rdata->num_partitions);
			}

			if(data->device != device && !(flags & MCL_ARG_REWRITE)){
				rdata_move_device_memory(rdata, data, device, queue, &err_2);
			} else if (flags & MCL_ARG_REWRITE) {
				clReleaseMemObject(data->clBuffer);
			}

			Dprintf("\t\tMoving to next element in the loop");
			//*err |= err_2;
			temp = Tree_PrevNode(rdata->children, n);
			free(data);
			data = temp ? (mcl_subbuffer*)Node_GetData(temp) : NULL;
			Tree_DeleteNode(rdata->children, n);
			n = data ? Tree_SearchNode(rdata->children, data) : NULL;
			adec(&rdata->num_partitions);
			Dprintf("\t\tFinished one iteration of loop. Node: %p", n);
		}
	}
	if (flags & MCL_ARG_REWRITE) {
		clEnqueueWriteBuffer(queue, rdata->clBuffers[device], CL_FALSE, offset, size, 
					(void*)rdata->key.addr + offset, 0, NULL, NULL);
	}

	// Create the desired subbuffer
	mcl_subbuffer* ret = malloc(sizeof(mcl_subbuffer));
	ret->offset = offset;
	ret->size = size;
	ret->refs = 1;
	ret->device = device;
	cl_buffer_region info = {offset, size};
	Dprintf("Creating subbuffer: %"PRIu32", device: %"PRIu64", offset: %"PRId64" size: %"PRIu64".", rdata->id, device, offset, size);
	ret->clBuffer = clCreateSubBuffer(rdata->clBuffers[device], cl_flags, CL_BUFFER_CREATE_TYPE_REGION, &info, err);
    if(*err != CL_SUCCESS){
		eprintf("Could not create subbuffer, error code %d", *err);
	}
	Dprintf("Inserting into tree.");
    Tree_Insert(rdata->children, ret);
	ainc(&rdata->num_partitions);

	Dprintf("Updating Shared Memory.");
	if(rdata->flags & MCL_ARG_SHARED)
		mcl_update_shared_mem(rdata, device, size, offset, -1);

    pthread_rwlock_unlock(&rdata->tree_lock);

    Dprintf("Returning Buffer, references: %"PRIu64".", ret->refs);
	return ret->clBuffer;
}

int rdata_put_mem(mcl_rdata* rdata, off_t offset)
{
	if(!(rdata->flags & MCL_ARG_DYNAMIC)){
		return 0;
	}

	mcl_subbuffer tosearch = {offset, 0, 0, 0 };
	pthread_rwlock_rdlock(&rdata->tree_lock);
	Node n = Tree_SearchNode(rdata->children, (void*) &tosearch);
	if(!n){
		eprintf("Could not find buffer to release.");
		pthread_rwlock_unlock(&rdata->tree_lock);
		return -1;
	}
	mcl_subbuffer* existing = (mcl_subbuffer*)Node_GetData(n);
	adec(&existing->refs);
	Dprintf("\tReferences for offset %"PRIu64": %"PRIu64"", existing->offset, existing->refs);
	pthread_rwlock_unlock(&rdata->tree_lock);
	return 0;
}

int rdata_remove_subbuffers(mcl_rdata* rdata, size_t size, off_t offset)
{
	if(!(rdata->flags & MCL_ARG_DYNAMIC)){
		rdata->host_is_valid = 1;
		return 0;
	}

	pthread_rwlock_wrlock(&rdata->tree_lock);
	mcl_subbuffer tosearch;
	tosearch.offset = offset;
	Node n = Tree_SearchNode(rdata->children, (void*) &tosearch);
	if(n == NULL){
		pthread_rwlock_unlock(&rdata->tree_lock);
		return -1;
	}
	mcl_subbuffer* existing;
	existing = (mcl_subbuffer*) Node_GetData(n);

	if(existing->refs){
		Dprintf("Waiting on references for memory %"PRIu32" at offset %"PRIu64": %"PRIu64"",  
			rdata->id, existing->offset, existing->refs);
		pthread_rwlock_unlock(&rdata->tree_lock);
		return 0;
	}
	while(existing->refs){
		sched_yield();
	}
	Tree_DeleteNode(rdata->children, n);
	adec(&rdata->num_partitions);
    clReleaseMemObject(existing->clBuffer);

	if(rdata->flags & MCL_ARG_SHARED)
		mcl_delete_shared_mem_subbuffer((void*)rdata->key.addr, existing->size, existing->offset);
	free(existing);
	pthread_rwlock_unlock(&rdata->tree_lock);
	return 0;
}

void rdata_put(mcl_rdata* el)
{
	if(!el)
		return;

	adec(&(el->refs));
	Dprintf("\t\t Ref counter for memory %"PRIu32": %"PRIu32"", el->id, el->refs);
	return;
}

int rdata_del(mcl_rdata* rdata)
{
	mcl_rdata_key* key = malloc(sizeof(mcl_rdata_key));
	key->addr = rdata->key.addr;
	hashmap_putif(rdata_hash, key, 0, IGNORE);

	if(rdata->num_partitions != 0){
		pthread_rwlock_wrlock(&rdata->tree_lock);
		Node cur;
		while(rdata->num_partitions){
			cur = Tree_FirstNode(rdata->children);
			mcl_subbuffer* data = (mcl_subbuffer*)Node_GetData(cur);
			Tree_DeleteNode(rdata->children, cur);
			adec(&rdata->num_partitions);

			Dprintf("\tReleasing Subbuffer for rdata %"PRIu32", at offset %"PRIu64"", rdata->id, data->offset);
			clReleaseMemObject(data->clBuffer);
			free(data);
		}
		pthread_rwlock_unlock(&rdata->tree_lock);
		
	}
	free(rdata->children);
	pthread_rwlock_destroy(&rdata->tree_lock);

	if(rdata->flags & MCL_ARG_SHARED){
		mcl_release_shared_mem((void*)rdata->key.addr);
	} else {
		for(int i = 0; i < CL_MAX_DEVICES; i++){
			if(rdata->devices  & (1 << i)){
				clReleaseMemObject(rdata->clBuffers[i]);
			}
		}
	}

	free(rdata);
	return 0;

}

int rdata_release_mem(mcl_rdata* rdata, uint64_t dev)
{
	if(!(rdata->devices |= dev)){
		return 0;
	}

	if(rdata->num_partitions != 0){
		pthread_rwlock_wrlock(&rdata->tree_lock);
		Node cur = Tree_FirstNode(rdata->children);
		while(cur){
			Node next = Tree_NextNode(rdata->children, cur);
			mcl_subbuffer* data = (mcl_subbuffer*)Node_GetData(cur);
			if(data->device == dev){
				clReleaseMemObject(data->clBuffer);
				Tree_DeleteNode(rdata->children, cur);
				free(data);
				adec(&rdata->num_partitions);
			}
			cur = next;
		}
		pthread_rwlock_unlock(&rdata->tree_lock);
	}
	if(rdata->flags & MCL_ARG_SHARED)
		mcl_release_device_shared_mem((void*)rdata->key.addr, dev);
	else
		clReleaseMemObject(rdata->clBuffers[dev]);
	rdata->devices &= (~(1 << dev));
	return 0;
	
}

int rdata_release_mem_by_id(uint32_t mem_id, uint64_t dev) {
	mcl_rdata_key* key = malloc(sizeof(mcl_rdata_key));

	pthread_rwlock_rdlock(&memid_map_lock);
	key->addr = memid_map[mem_id];
	pthread_rwlock_unlock(&memid_map_lock);

	mcl_rdata* rdata = hashmap_get(rdata_hash, key);
	if(!rdata){
		Dprintf("Tried to evict memory which is not tracked by this process");
		return -1;
	}
	
	cl_event* events = malloc(sizeof(cl_event) * rdata->num_partitions);
	int transfers = 0;
	
	cl_command_queue queue = __get_queue(dev);
	if(rdata->flags & MCL_ARG_DYNAMIC){
		pthread_rwlock_wrlock(&rdata->tree_lock);
		Node cur = Tree_FirstNode(rdata->children);
		while(cur){
			Node next = Tree_NextNode(rdata->children, cur);
			mcl_subbuffer* data = (mcl_subbuffer*)Node_GetData(cur);
			if(data->device == dev){
				if(!(rdata->flags & MCL_ARG_SHARED) || mcl_is_shared_mem_owner((void*)rdata->key.addr, dev)) {
					clEnqueueReadBuffer(queue, data->clBuffer, CL_FALSE, 0, data->size, 
								(void*)rdata->key.addr + data->offset, 0, NULL, &events[transfers++]);
				}
				clReleaseMemObject(data->clBuffer);
                data->device = -1;
			}
			cur = next;
		}
		pthread_rwlock_unlock(&rdata->tree_lock);
	}
	clWaitForEvents(transfers, events);
	free(events);

	if(rdata->flags & MCL_ARG_SHARED)
		mcl_release_device_shared_mem((void*)rdata->key.addr, dev);
	else
		clReleaseMemObject(rdata->clBuffers[dev]);
	rdata->devices &= (~(1 << dev));
	return 0;
}

int rdata_invalidate_gpu_mem(mcl_rdata* rdata) {
	
	if(rdata->flags & MCL_ARG_SHARED){
		mcl_update_shared_mem(rdata, 0, rdata->size, 0, 1);
	}

	if(rdata->flags & MCL_ARG_DYNAMIC){
		Node n; 
		while(rdata->num_partitions){
			n = Tree_FirstNode(rdata->children);
			mcl_subbuffer* s = Node_GetData(n);
			clReleaseMemObject(s->clBuffer);
			Tree_DeleteNode(rdata->children, n);
			free(s);
			adec(&rdata->num_partitions);
		}
	} else {
        rdata->evicted = 1;
    }

	if(rdata->flags & MCL_ARG_SHARED){
		mcl_update_shared_mem(rdata, 0, rdata->size, 0, -1);
		mcl_delete_shared_mem_subbuffer((void*)rdata->key.addr, rdata->size, 0);
	}
	return 0;
}

int rdata_free(void)
{
    int hash_size = hashmap_size(rdata_hash);
	Dprintf("Freeing rdata hash, entries remaining: %d", hash_size);
	mcl_rdata_key key;
	for(uint64_t i = 0; i < max_memid; i++){
		key.addr = memid_map[i];
		mcl_rdata* rdata = hashmap_get(rdata_hash, &key);
		if(rdata) {
            //eprintf("Freeing rdata %"PRIu64"", i);
            //free(rdata);
			//rdata_del(rdata);
        }
	}
    hashmap_free(rdata_hash);
    Dprintf("Returning from rdata free.");
	return hash_size;
}
