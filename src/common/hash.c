#include <uthash.h>
#include <pthread.h>

#include <minos.h>
#include <minos_internal.h>

pthread_rwlock_t req_lock;

int req_init(void)
{
	return pthread_rwlock_init(&req_lock, NULL);
}

int req_add(mcl_request** table, uint32_t key, mcl_request* r)
{
	r->key = key;

	VDprintf("  Added request %u.",r->key);
	pthread_rwlock_wrlock(&req_lock);
	HASH_ADD_INT(*table, key, r);
	pthread_rwlock_unlock(&req_lock);
	
	return 0;
}

mcl_request* req_del(mcl_request** table, uint32_t key)
{
	mcl_request*    req = NULL;

	pthread_rwlock_wrlock(&req_lock);
	HASH_FIND_INT(*table, &key, req);
	if(req)
		HASH_DEL(*table, req);
	pthread_rwlock_unlock(&req_lock);
	
	return req;
}

uint32_t req_count(mcl_request** table)
{
	uint32_t ret;

	pthread_rwlock_rdlock(&req_lock);
	ret = HASH_COUNT(*table);
	pthread_rwlock_unlock(&req_lock);

	return ret;
}

int req_clear(mcl_request** table)
{
	pthread_rwlock_wrlock(&req_lock);
	HASH_CLEAR(hh, *table);
	pthread_rwlock_unlock(&req_lock);

	return pthread_rwlock_destroy(&req_lock);
}

mcl_request* req_search(mcl_request** table, uint32_t key)
{
	mcl_request*       req = NULL;

	pthread_rwlock_rdlock(&req_lock);
	HASH_FIND_INT(*table, &key, req);
	pthread_rwlock_unlock(&req_lock);

	if(req)
		VDprintf("  Found Req: %u (%p)", req->key, req);
	
	return req;
}


void req_print(mcl_request** table)
{
    return;

	mcl_request* req = NULL;
	mcl_request* tmp = NULL;

	pthread_rwlock_rdlock(&req_lock);
	HASH_ITER(hh, *table, req, tmp){
		iprintf("\tReq %u: %s", req->key, req->tsk->kernel->name);
	}
	pthread_rwlock_unlock(&req_lock);
}
