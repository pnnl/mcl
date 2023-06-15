#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <utlist.h>

#include <minos.h>
#include <minos_internal.h>
#include <debug.h>

extern mcl_desc_t mcl_desc;
extern mcl_resource_t* mcl_res;
mcl_kernel* kerMap = NULL;
pthread_rwlock_t kerMap_lock;

static inline int pcmp(mcl_kernel* a, mcl_kernel* b)
{
	return strcmp(a->name, b->name);
}

int kerMap_init(void)
{
	return pthread_rwlock_init(&kerMap_lock, NULL);
}

static inline mcl_kernel* __kerMap_search(char* key)
{
        mcl_kernel* e;

        LL_FOREACH(kerMap,e)
		if(strcmp(e->name, key) == 0)
			return e;

        return NULL;
}

mcl_kernel* kerMap_search(char* key)
{
	mcl_kernel* e;

        Dprintf("\t Looking for kernel %s...", key);
        pthread_rwlock_rdlock(&kerMap_lock);
        e = __kerMap_search(key);
        pthread_rwlock_unlock(&kerMap_lock);
	
	return e;
}

int kerMap_add_prg(mcl_kernel* k, mcl_program* p)
{
        struct kernel_program* kprg;

        kprg = (struct kernel_program*) malloc(sizeof(struct kernel_program));
        if(!kprg){
                eprintf("Error allocating memory for kernel program for kernel %s", k->name);
                return -1;
        }

        pthread_rwlock_wrlock(&(k->lock));
        kprg->p = p;
        LL_APPEND(k->prg, kprg);
        k->targets |= p->targets;
        pthread_rwlock_unlock(&(k->lock));
        Dprintf("\t Added program %s to kernel %s (targets = 0x%lx)", p->key, k->name, k->targets);
        return 0;
}

mcl_program* kerMap_get_prg(mcl_kernel* k, uint64_t device)
{
        mcl_device_t* dev = res_getDev(device);
        struct kernel_program* kprg;
        mcl_program* e = NULL;

        assert(k->targets & dev->type);

        pthread_rwlock_rdlock(&(k->lock));
        LL_FOREACH(k->prg, kprg){
                Dprintf("\t Checking program %s (prg targets 0x%lx, ker targets 0x%lx dev type 0x%lx)",
                kprg->p->key, kprg->p->targets, k->targets, dev->type);
                if(kprg->p->targets & dev->type){
                        e = kprg->p;
                        break;
                }
        }
        pthread_rwlock_unlock(&(k->lock));
        if(e) 
                Dprintf("\t Found program %s", e->key);

        return e;
}

mcl_kernel* kerMap_add(char* name)
{
	mcl_kernel* k   = NULL;
	
	k = kerMap_search(name);
	if(k)
		return k;

	Dprintf("\t Kernel %s not found, adding new one...", name);
	k = (mcl_kernel*) malloc(sizeof(mcl_kernel));
	if(!k){
		eprintf("Error allocating memory for new program (%s)!", name);
		goto err;
	}

	k->name = (char*) malloc(strlen(name) + 1);
        if(k->name == NULL){
                eprintf("Error allocating memory for kernel name (%s)", name);
                goto kernel;
        }
        strcpy(k->name, name);
        k->targets = 0x0;
        k->prg     = NULL;
        pthread_rwlock_init(&(k->lock), NULL);

        pthread_rwlock_wrlock(&kerMap_lock);
	LL_APPEND(kerMap, k);
        pthread_rwlock_unlock(&kerMap_lock);

        Dprintf("\t Kernel %s added (targets = 0x%lx)", k->name, k->targets);
	return k;

kernel:
	free(k);
err:	
	return NULL;
}

static inline void __kerMap_remove(mcl_kernel* k)
{
	Dprintf("Removing kernel %s...", k->name);
	LL_DELETE(kerMap, k);
	free(k->name);
        free(k);
}

int kerMap_remove(mcl_kernel* k)
{
	mcl_kernel* e;

        pthread_rwlock_wrlock(&kerMap_lock);
	LL_SEARCH(kerMap, e, k, pcmp);
	if(e){
		__kerMap_remove(k);
		return 0;
	}
        pthread_rwlock_unlock(&kerMap_lock);
	Dprintf("Kernel %s not found.", k->name);
	
	return 1;
}

void kerMap_finit(void)
{
	mcl_kernel* e;
	mcl_kernel* tmp;
	
	Dprintf("Removing cached kernel...");
	LL_FOREACH_SAFE(kerMap, e, tmp){
		__kerMap_remove(e);
	}
}
