#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <utlist.h>

#include <minos.h>
#include <minos_internal.h>
#include <debug.h>

extern mcl_desc_t mcl_desc;
mcl_program *prgMap = NULL;
pthread_rwlock_t prgMap_lock;

static inline int pcmp(mcl_program* a, mcl_program* b)
{
    return strcmp(a->path, b->path);
}

int prgMap_init(void)
{
    return pthread_rwlock_init(&prgMap_lock, NULL);
}

static inline char *prg_makeKey(char *p, char *opt)
{
    char *key = (char *)malloc(sizeof(char) * (strlen(p) + strlen(opt)) + 1);

    if (key == NULL)
        return NULL;

    strcpy(key, p);
    strcat(key, opt);

    return key;
}

static inline mcl_program* __prgMap_search(char* key)
{
        mcl_program* e;

        LL_FOREACH(prgMap,e)
		if(strcmp(e->key, key) == 0)
			return e;

        return NULL;
}

mcl_program* prgMap_search(char* key)
{
	mcl_program* e;

        Dprintf("\t Looking for program %s...", key);
        pthread_rwlock_rdlock(&prgMap_lock);
        e = __prgMap_search(key);
        pthread_rwlock_unlock(&prgMap_lock);
	
	return e;
}
				      
mcl_program* prgMap_add(char* path, char* opts, uint64_t flags, uint64_t archs)
{
	mcl_program* p;
	unsigned int ndevs = mcl_desc.info->ndevs;
	int          fd;
	char*        key = NULL;
	
#if 0
	if(opts)
		key = prg_makeKey(path, opts);
	else{
#endif
	key = (char*) malloc(sizeof(char) * strlen(path) + 1);
	if(!key){
		eprintf("Error while allocating memory for program key.");
		goto err;
	}
	strcpy(key,path);
#if 0
	}
#endif
	assert(key != NULL);
	
	p = prgMap_search(key);
	if(p)
		return p;

	Dprintf("\t Program %s not found, adding new one...", key);
	p = (mcl_program*) malloc(sizeof(mcl_program));
	if(!p){
		eprintf("Error allocating memory for new program (%s)!", path);
		goto err;
	}

	p->key = key;
	p->path = (char*) malloc(sizeof(char) * strlen(path) + 1);
	if(p->path == NULL){
		eprintf("Erorr allocating memory for program path. Aborting.");
		goto err_program;	  
	}

	if(opts){
		p->opts = (char*) malloc(sizeof(char) * strlen(opts) + 1);
		if(p->opts == NULL){
			eprintf("Erorr allocating memory for program options. Aborting.");
			goto err_path;	  
		}
	}else
		p->opts = NULL;
	
        p->flags = flags;
        p->targets = archs;
	p->objs = (mcl_pobj*) malloc(sizeof(mcl_pobj) * ndevs);
	if(p->objs == NULL){
		eprintf("Erorr allocating memory for program objects. Aborting.");
		goto err_opts;	  
	}

	fd = open(path, O_RDONLY);
	if(fd == -1){
		eprintf("Error opening OpenCL program. Aborting.\n");
		goto err_objs;
	}
	p->src_len = lseek(fd, 0, SEEK_END);
	
	p->src = (char*) malloc(sizeof(char) * (p->src_len) + 1);
	if(p->src == NULL){
		eprintf("Erorr allocating memory for kernel source. Aborting.");
		goto err_file;
	}

	lseek(fd, 0, SEEK_SET);
	if(read(fd, (void*) (p->src), p->src_len) != p->src_len){
		eprintf("Error loading OpenCL code from %s. Aborting.\n", path);
		goto err_src;
	}

	p->src[p->src_len] = '\0';
	close(fd);
	
	strcpy(p->path, path);
	if(opts) 
		strcpy(p->opts, opts);
	memset((void*) p->objs, 0, sizeof(mcl_pobj) * ndevs);
	
	LL_APPEND(prgMap, p);

	return p;

 err_src:
	free(p->src);
 err_file:
	close(fd);
 err_objs:
	free(p->objs);
 err_opts:
	if(p->opts)
		free(p->opts);
 err_path:
	free(p->path);
 err_program:
	free(p);
 err:
	free(key);
	
	return NULL;
}

static inline void __prgMap_remove(mcl_program *p)
{
    // Dprintf("Removing program %s...", p->path);
    LL_DELETE(prgMap, p);
    free(p->objs);
    free(p->src);
    free(p->path);
    if (p->opts)
        free(p->opts);
    free(p->key);
    free(p);
}

int prgMap_remove(mcl_program *p)
{
    mcl_program *e;

    LL_SEARCH(prgMap, e, p, pcmp);
    if (e)
    {
        __prgMap_remove(p);
        return 0;
    }
    // dprintf("Program %s not found.", p->path);

    return 1;
}

void prgMap_finit(void)
{
	mcl_program* e;
	mcl_program* tmp;
	
	Dprintf("Removing cached programs...");
	LL_FOREACH_SAFE(prgMap, e, tmp){
		__prgMap_remove(e);
	}
}
