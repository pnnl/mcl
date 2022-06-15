#include <uthash.h>
#include <pthread.h>

#include <minos.h>
#include <minos_internal.h>
#include <minos_sched.h>
#include <atomics.h>

pthread_rwlock_t rdata_lock;
sched_rdata*       rdata_hash;

#ifdef _DEBUG
static inline int rdata_print(void)
{
	sched_rdata*    s;
	
	for(s=rdata_hash; s != NULL; s=(sched_rdata*)(s->hh.next))
		Dprintf("\t\t Entry <%"PRIu64",%"PRIu64">", s->key[0], s->key[1]);
	return 0;
}
#else
#define rdata_print()
#endif

int process_cmp(process_t* p1, process_t* p2){ 
	return p1->pid - p2->pid;
}

int sched_rdata_init(void)
{
	Dprintf("Initializing resident data hash table...");
	pthread_rwlock_init(&rdata_lock,NULL);
	rdata_hash = NULL;
	return 0;
}

static inline sched_rdata* rdata_get_internal(sched_rdata* el)
{
	sched_rdata*  r = NULL;
	HASH_FIND(hh, rdata_hash, &(el->key),2*sizeof(uint64_t), r);
	return r;
}

int sched_rdata_add(sched_rdata* el)
{
	sched_rdata* r = NULL;
	pthread_rwlock_wrlock(&rdata_lock);
    HASH_FIND(hh, rdata_hash, &(el->key),2*sizeof(uint64_t), r);
	if(r){
		pthread_rwlock_unlock(&rdata_lock);
		return 1;
	}
    HASH_ADD(hh, rdata_hash, key, 2*sizeof(uint64_t), el);
	pthread_rwlock_unlock(&rdata_lock);

	Dprintf("\t\t Added <%d,%"PRIu64"> to rdata hash table", 
        el->pid, el->mem_id);
 	return 0;
}

sched_rdata* sched_rdata_get(uint64_t mem_id, pid_t pid)
{
	sched_rdata* ret = NULL;
	sched_rdata  el;

	memset(&el, 0, sizeof(sched_rdata));
	el.pid = pid;
	el.mem_id  = mem_id;
	
	Dprintf("\t Searching for <%d,%"PRIu64"> in resident data hash table...", 
        el.pid, el.mem_id);

	pthread_rwlock_rdlock(&rdata_lock);
	ret = rdata_get_internal(&el);
	pthread_rwlock_unlock(&rdata_lock);

	return ret;
}

sched_rdata* sched_rdata_rm(uint64_t mem_id, pid_t pid)
{
	Dprintf("\t Removing Entry <%d,%"PRIu64">", 
        pid, mem_id);

    sched_rdata* ret = NULL;
	sched_rdata  el;
	memset(&el, 0, sizeof(sched_rdata));
    el.key[0] = 0;
    el.key[1] = 0;
	el.pid = pid;
	el.mem_id  = mem_id;

    pthread_rwlock_wrlock(&rdata_lock);

	ret = rdata_get_internal(&el);
    if(ret){
        HASH_DEL(rdata_hash, ret);
    }    

	pthread_rwlock_unlock(&rdata_lock);
	
	return ret;
}

int sched_rdata_rm_pid(pid_t pid, uint64_t* mem_freed, uint64_t ndevs)
{
	sched_rdata*    s;
	sched_rdata*    tmp;
    memset(mem_freed, 0, ndevs*sizeof(uint64_t));
	
	Dprintf("Removing PID from hash table: %d", pid);
    pthread_rwlock_wrlock(&rdata_lock);
	HASH_ITER(hh, rdata_hash, s, tmp) {
        if(s->pid == pid){
            Dprintf("\t Removing Entry <%d,%"PRIu64">", s->pid, s->mem_id);
            HASH_DEL(rdata_hash, s);
            for(int i = 0; i < ndevs; i++){
                if(((s->devs >> i) & 0x01)){
                    VDprintf("\t\t Memory to free on device %d: %"PRIu64"", i, s->size);
                    mem_freed[i] += s->size;
                }
            }
            free(s);
        }
	}
	pthread_rwlock_unlock(&rdata_lock);
	
	return 0;
}

int sched_rdata_free(void)
{
	sched_rdata*    s;
	sched_rdata*    tmp;
	
	Dprintf("Cleaning up resident data hash table...");
    pthread_rwlock_wrlock(&rdata_lock);
	HASH_ITER(hh, rdata_hash, s, tmp) {
		Dprintf("\t Removing Entry <%"PRIu64",%"PRIu64">", s->key[0], s->key[1]);
		
        HASH_DEL(rdata_hash, s);
        free(s);
	}

	pthread_rwlock_unlock(&rdata_lock);
		
	return 0;
}

int sched_rdata_add_device(sched_rdata* el, int dev)
{
    Dprintf("\t Adding device %d for <%d,%"PRIu64"> in hash table...", 
        dev, el->pid, el->mem_id);

    int cur_devs = el->devs;
    el->devs |= (0x01 << dev);
    if((cur_devs >> dev) & 0x01){
        return 1;
    }
    el->ndevs += 1;
    return 0;

}

int sched_rdata_rm_device(sched_rdata* el, int dev)
{
    int cur_devs = el->devs;
    el->devs &= ~(0x01 << dev);
    if(((cur_devs >> dev) & 0x01)){
        el->ndevs -= 1;
		int64_t cur_idx = el->subbuffers.head;
		mcl_partition_t* cur;
		while(cur_idx >= 0){
			cur = list_get(&el->subbuffers, cur_idx);
			cur_idx = cur->next;
			if(cur->dev == dev){
				list_delete(&el->subbuffers, cur);
			}
		}
    }

    return ((cur_devs >> dev) & 0x01);
}

int sched_rdata_on_device(sched_rdata* el, int dev)
{
    return ((el->devs >> dev) & 0x01);
}