#include <uthash.h>
#include <pthread.h>

#include <minos.h>
#include <minos_internal.h>
#include <atomics.h>

pthread_rwlock_t rdata_lock;
mcl_rdata*       rdata_hash;

#ifdef _DEBUG
static inline int rdata_print(void)
{
	mcl_rdata*    s;
	
	for(s=rdata_hash; s != NULL; s=(mcl_rdata*)(s->hh.next))
		Dprintf("\t\t Entry <%p,%"PRIu64">", (void*) (s->key.addr), s->key.device);
	return 0;
}
#else
#define rdata_print()
#endif

int rdata_init(void)
{
	Dprintf("Initializing resident data hash table...");
	pthread_rwlock_init(&rdata_lock,NULL);
	rdata_hash = NULL;
	return 0;
}

int rdata_add(mcl_rdata* el, int hold)
{
	mcl_rdata* r;
	
	pthread_rwlock_wrlock(&rdata_lock);
	HASH_FIND(hh, rdata_hash, &(el->key), sizeof(mcl_rdata_key), r);
	if(r){
		pthread_rwlock_unlock(&rdata_lock);
		return 1;
	}

	HASH_ADD(hh, rdata_hash, key, sizeof(mcl_rdata_key), el);
	if(hold){
		ainc(&(el->refs));
		Dprintf("\t\t Ref counter for  <%p,%"PRId64">: %"PRIu32"", (void*) (el->key.addr), el->key.device, el->refs);
	}
	pthread_rwlock_unlock(&rdata_lock);
	Dprintf("\t\t Added <%p,%"PRId64"> to rdata hash table", (void*) (el->key.addr), el->key.device);

 	return 0;
}

static inline mcl_rdata* rdata_get_internal(mcl_rdata* el)
{
	mcl_rdata*  r = NULL;
	
	//	rdata_print();

	HASH_FIND(hh, rdata_hash, &(el->key), sizeof(mcl_rdata_key), r);
#if 0
	if(r)
		Dprintf("\t\t Found <%p,%"PRIu64">", (void*) (r->key.addr), r->key.id);
#endif	
	return r;
}

mcl_rdata* rdata_get(void* addr, int64_t device)
{
	mcl_rdata* ret = NULL;
	mcl_rdata  el;

	memset(&el, 0, sizeof(mcl_rdata));
	el.key.addr = (unsigned long) addr;
	el.key.device = device;
	
	Dprintf("\t Searching for <%p,%"PRId64"> in resident data hash table...", (void*) (el.key.addr), el.key.device);
	pthread_rwlock_rdlock(&rdata_lock);
	ret = rdata_get_internal(&el);
	if(ret){
		ainc(&(ret->refs));
		Dprintf("\t\t Ref counter for  <%p,%"PRId64">: %"PRIu32"", (void*) (ret->key.addr), ret->key.device, ret->refs);
	}
	pthread_rwlock_unlock(&rdata_lock);

	return ret;
}

// Not sure if we need locking here...
// Should we consider removing the buffer if refs=0 and we know the buffer should be removed?
void rdata_put(mcl_rdata* el)
{
	if(!el)
		return;

	adec(&(el->refs));
	Dprintf("\t\t Ref counter for  <%p,%"PRId64">: %"PRIu32"", (void*) (el->key.addr), el->key.device, el->refs);
	
	return;
}

int rdata_rm(void* addr)
{
	mcl_rdata*    s;
	mcl_rdata*    tmp;
    unsigned long laddr = (unsigned long) addr;
	cl_mem        buffers[CL_MAX_DEVICES];
	memset(buffers, 0, CL_MAX_DEVICES * sizeof(cl_mem));
	
	pthread_rwlock_wrlock(&rdata_lock);
    /* TODO: More efficient way of scanning for address */
	HASH_ITER(hh, rdata_hash, s, tmp) {
		if(s->key.addr == laddr){
			Dprintf("\t Removing Entry <%p,%"PRId64">, refs %"PRIu32"", 
                (void*) (s->key.addr), s->key.device, s->refs);

			while(s->refs)
				;
		
			HASH_DEL(rdata_hash, s);
            if(s->key.device >= 0){
                buffers[s->key.device] = s->clBuffer;
            }
			free(s);
		}
	}
	pthread_rwlock_unlock(&rdata_lock);

	for(int i = 0; i < CL_MAX_DEVICES; i++){
		if(buffers[i] != NULL){
			clReleaseMemObject(buffers[i]);
		}
	}
	return 0;
}

int rdata_mv(void* addr, int64_t new_dev, mcl_rdata** old_entry){
    mcl_rdata*    s;
	mcl_rdata*    tmp;
    int           old_dev = -1;
    unsigned long laddr = (unsigned long) addr;
	cl_mem        buffers[CL_MAX_DEVICES];
	memset(buffers, 0, CL_MAX_DEVICES * sizeof(cl_mem));
	
	pthread_rwlock_wrlock(&rdata_lock);
    /* TODO: More efficient way of scanning for address */
	HASH_ITER(hh, rdata_hash, s, tmp) {
		if(s->key.addr == laddr){
			Dprintf("\t Removing Entry <%p,%"PRId64">, refs %"PRIu32"", 
                (void*) (s->key.addr), s->key.device, s->refs);

			while(s->refs)
				;
		
            if(s->key.device >= 0 && s->key.device != new_dev){
                HASH_DEL(rdata_hash, s);
                if(old_entry && old_dev == -1){
                    old_dev = s->key.device;
                    *old_entry = s;
                }else{
                    buffers[s->key.device] = s->clBuffer;
                    free(s);
                }
            }
		}
	}
	pthread_rwlock_unlock(&rdata_lock);

	for(int i = 0; i < CL_MAX_DEVICES; i++){
		if(buffers[i] != NULL){
			clReleaseMemObject(buffers[i]);
		}
	}
        //pthread_rwlock_unlock(&rdata_lock);

    return old_dev;
}

int rdata_free(void)
{
	mcl_rdata*    s;
	mcl_rdata*    tmp;
    int ret = 0;

	Dprintf("Cleaning up resident data hash table...");
    pthread_rwlock_wrlock(&rdata_lock);
	HASH_ITER(hh, rdata_hash, s, tmp) {
		Dprintf("\t Removing Entry <%p,%"PRId64">", (void*) (s->key.addr), s->key.device);
		if(s->refs)
			iprintf("\t Ref counter for  <%p,%"PRId64">: %"PRIu32"", (void*) (s->key.addr), s->key.device, s->refs);
		
        HASH_DEL(rdata_hash, s);
        free(s);
	}
	pthread_rwlock_unlock(&rdata_lock);
		
	return ret;
}
