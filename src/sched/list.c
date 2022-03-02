#include <utlist.h>

#include <minos.h>
#include <minos_internal.h>
#include <atomics.h>

extern mcl_sched_t mcl_desc;

/*
 * Return -1,0,1 as for strcmp. Note that it should never be the case that
 * a = b because client's PID are supposed to be different.
 */
static inline int cli_cmp(struct mcl_client_struct* a, struct mcl_client_struct* b)
{
	return (a->pid > b->pid) ? -1 : 1;
}

int cli_remove(struct mcl_client_struct** head, pid_t pid)
{
	struct mcl_client_struct *el = NULL;

	if(*head == NULL){
		eprintf("Client list is empty!");
		return -1;
	}

	DL_FOREACH(*head, el){
		if(el->pid == pid){
			DL_DELETE(*head, el);
			
			adec(&mcl_desc.nclients);
			Dprintf("Client %d removed from list", pid);
            free(el);
			return 0;
		}
		else if(el->pid < pid)
			goto out;
		
	}

 out:
	Dprintf("Client %d not found!", pid);
	return -1;
}

int cli_add(struct mcl_client_struct** head, struct mcl_client_struct* el)
{
	if(!el){
		eprintf("Invalid arguments");
		return -1;
	}

	DL_INSERT_INORDER(*head, el, cli_cmp);
	
	Dprintf("Added %d to client list",el->pid);

	ainc(&mcl_desc.nclients);
	
	return 0;
}

struct mcl_client_struct* cli_search(struct mcl_client_struct** head, pid_t pid)
{
	struct mcl_client_struct* el;

	DL_SEARCH_SCALAR(*head,el,pid,pid);

	return el;
}

uint64_t cli_count(struct mcl_client_struct** head)
{
	uint64_t ret;
	struct mcl_client_struct* el;
	
	DL_COUNT(*head,el,ret);
	
	return ret;
}

pid_t cli_get_pid(const char* src)
{
	char* ptr;

	ptr = strstr(src,".");
	
	if(!ptr || ptr == src){
		eprintf("Invalide separator or socket");
		return 0;
	}

	ptr++; 	//Skip the "." in the extracted string

	return atoi(ptr);
}
