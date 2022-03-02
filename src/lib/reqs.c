#include <utlist.h>
#include <pthread.h>

#include <minos.h>
#include <minos_internal.h>


int rlist_init(pthread_rwlock_t* lock)
{
	if(lock)
		return pthread_rwlock_init(lock, NULL);

	return 0;
}

mcl_rlist* rlist_pop(mcl_rlist** head, pthread_rwlock_t* lock)
{
	mcl_rlist* el = NULL;

	if(*head == NULL)
		return NULL;

	if(lock)
		pthread_rwlock_wrlock(lock);
	el = *head;
	DL_DELETE(*head, el);
	if(lock)
		pthread_rwlock_unlock(lock);

	return el;
}

int rlist_append(mcl_rlist** head, pthread_rwlock_t* lock, mcl_rlist* el)
{
	if(lock)
		pthread_rwlock_wrlock(lock);
	DL_APPEND(*head, el);
	if(lock)
		pthread_rwlock_unlock(lock);

	return 0;
}

int rlist_add(mcl_rlist** head, pthread_rwlock_t* lock, mcl_request* r)
{
	mcl_rlist* el;
	
	if(!r){
		eprintf("Invalid argument!");
		return -1;
	}

	el = (mcl_rlist*) malloc(sizeof(mcl_rlist));
	if(!el){
		eprintf("Error allocating memory!");
		return -1;
	}
	el->rid  = r->hdl->rid;
	el->req  = r;
	el->next = NULL;
	
	if(lock)
		pthread_rwlock_wrlock(lock);
	DL_APPEND(*head, el);
	if(lock)
		pthread_rwlock_unlock(lock);
	return 0;
}

mcl_request* rlist_remove(mcl_rlist** head, pthread_rwlock_t* lock, uint32_t rid)
{
	mcl_rlist*   el  = NULL;
	mcl_rlist*   tmp = NULL;
	mcl_request* req = NULL;
	
	if(*head == NULL){
		eprintf("Pending task list empty");
		return NULL;
	}

	if(lock)
		pthread_rwlock_wrlock(lock);
	DL_FOREACH_SAFE(*head, el, tmp)
		if(el->rid == rid){
			DL_DELETE(*head, el);
			break;
		}
	if(lock)
		pthread_rwlock_unlock(lock);

	if(el){
		req = el->req;
		free(el);
	}
	return req;
}

mcl_request* rlist_search(mcl_rlist** head, pthread_rwlock_t* lock, uint32_t rid)
{
	mcl_rlist* el = NULL;

	if(*head == NULL)
		return NULL;

	if(lock)
		pthread_rwlock_rdlock(lock);
	DL_SEARCH_SCALAR(*head, el, rid, rid);
	if(lock)
		pthread_rwlock_unlock(lock);

	if(el)
		return el->req;

	return NULL;
}

int rlist_count(mcl_rlist** head, pthread_rwlock_t* lock)
{
	int ret;
	mcl_rlist* el;

	if(lock)
		pthread_rwlock_rdlock(lock);
	DL_COUNT(*head, el, ret);
	if(lock)
		pthread_rwlock_unlock(lock);

	return ret;
}
