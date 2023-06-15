#include <errno.h>
#include <string.h>

#include <minos.h>
#include <minos_internal.h>

static int ndev = 0;

int msg_setup(int devs){
    ndev = devs;
    return 0;
}

static inline int msg_assemble(struct mcl_msg_struct* msg, uint64_t* data)
{
	if(!msg || !data){
		eprintf("Invalid arguments.");
		return -1;
	}
	data[0] =            (msg->cmd   << MSG_CMD_SHIFT)   & MSG_CMD_MASK;
	data[0] = data[0] | ((msg->type  << MSG_TYPE_SHIFT)  & MSG_TYPE_MASK);
	data[0] = data[0] | ((msg->flags << MSG_FLAGS_SHIFT) & MSG_FLAGS_MASK);
	data[0] = data[0] | ((msg->rid   << MSG_RID_SHIFT)   & MSG_RID_MASK);
	data[1] =            (msg->pes   << MSG_PES_SHIFT)   & MSG_PES_MASK;
	data[1] = data[1] | ((msg->mem   << MSG_MEM_SHIFT)   & MSG_MEM_MASK);
	data[1] = data[1] | ((msg->nres  << MSG_NRES_SHIFT)   & MSG_NRES_MASK);
	data[2] =           ((msg->taskid  << MSG_TASKID_SHIFT)   & MSG_TASKID_MASK);

	Dprintf("Number of dependencies inside message send: %"PRIu32"", msg->ndependencies);
	((uint32_t*)data)[6] = msg->ndependencies;
	if(msg->ndependencies){
		memcpy(&((uint32_t*)data)[7], msg->dependencies, sizeof(uint32_t) * MCL_MAX_DEPENDENCIES);
	}

    data += 3 + ((MCL_MAX_DEPENDENCIES + 1)/2);
    for(int i=0; i<MCL_DEV_DIMS; i++){
		data[i] = msg->pesdata.pes[i];
		data[MCL_DEV_DIMS+i] = msg->pesdata.lpes[i];
	}
	
    data += (2*MCL_DEV_DIMS);
    for(int i = 0, j = 0; i < msg->nres; i += 1, j += 2){
        data[j] =           ((msg->resdata[i].mem_id   << MSG_MEMID_SHIFT)   & MSG_MEMID_MASK);
		data[j] = data[j] | ((msg->resdata[i].pid      << MSG_PID_SHIFT)     & MSG_PID_MASK);
		data[j] = data[j] | ((msg->resdata[i].flags    << MSG_MEMFLAG_SHIFT) & MSG_MEMFLAG_MASK);
		data[j+1] =             ((msg->resdata[i].overall_size << MSG_OVERALL_SHIFT) & MSG_OVERALL_MASK);
        data[j+1] = data[j+1] | ((msg->resdata[i].mem_size << MSG_MEMSIZE_SHIFT) & MSG_MEMSIZE_MASK);
		data[j+1] = data[j+1] | ((msg->resdata[i].mem_offset << MSG_OFFSET_SHIFT) & MSG_OFFSET_MASK);
    }
	return 0;
}

static inline int msg_disassemble(uint64_t* data, struct mcl_msg_struct* msg, const char* src)
{
	if(!msg || !data || !src){
		eprintf("Invalid arguments.");
		return -1;
	}

        if(msg_init(msg)){
                eprintf("Could not initialize message");
                return -1;
        }

	msg->cmd   = (data[0] & MSG_CMD_MASK)   >> MSG_CMD_SHIFT;
	msg->type  = (data[0] & MSG_TYPE_MASK)  >> MSG_TYPE_SHIFT;
	msg->flags = (data[0] & MSG_FLAGS_MASK) >> MSG_FLAGS_SHIFT;
	msg->rid   = (data[0] & MSG_RID_MASK)   >> MSG_RID_SHIFT;
	msg->pes   = (data[1] & MSG_PES_MASK)   >> MSG_PES_SHIFT;
	msg->mem   = (data[1] & MSG_MEM_MASK)   >> MSG_MEM_SHIFT;
	msg->nres  =  (data[1] & MSG_NRES_MASK) >> MSG_NRES_SHIFT;
	msg->taskid  =  (data[2] & MSG_TASKID_MASK) >> MSG_TASKID_SHIFT;

	msg->ndependencies = ((uint32_t*)data)[6];
	memcpy(msg->dependencies, &((uint32_t*)data)[7], sizeof(uint32_t) * msg->ndependencies);
	Dprintf("Number of dependencies inside message recieve: %"PRIu32"", msg->ndependencies);

    for(int i=0; i<MCL_DEV_DIMS; i++){
        msg->pesdata.pes[i] = data[2+i];
        msg->pesdata.lpes[i] = data[2+MCL_DEV_DIMS+i];
    }

    if(msg->nres) {
		Dprintf("Number of resident memory inside message receive: %"PRIu64"", msg->nres);
        msg->resdata = (msg_arg_t*)malloc(msg->nres * sizeof(msg_arg_t));
        if(!msg->resdata){
            eprintf("Unable to allocate memory");
            return -1;
        }
        for(int i = 0, j = 3 + ((MCL_MAX_DEPENDENCIES + 1)/2) + (2*MCL_DEV_DIMS); i < msg->nres; i += 1, j += 2){
            msg->resdata[i].mem_id   = (data[j] & MSG_MEMID_MASK)     >> MSG_MEMID_SHIFT;
			msg->resdata[i].pid      = (data[j] & MSG_PID_MASK)       >> MSG_PID_SHIFT;
            msg->resdata[i].flags    = (data[j] & MSG_MEMFLAG_MASK)   >> MSG_MEMFLAG_SHIFT;
			msg->resdata[i].overall_size = (data[j+1] & MSG_OVERALL_MASK) >> MSG_OVERALL_SHIFT;
			msg->resdata[i].mem_size     = (data[j+1] & MSG_MEMSIZE_MASK) >> MSG_MEMSIZE_SHIFT;
			msg->resdata[i].mem_offset   = (data[j+1] & MSG_OFFSET_MASK) >> MSG_OFFSET_SHIFT;
        }
    }
	
	return 0;
}

int msg_init(struct mcl_msg_struct* m)
{
	m->cmd   = MSG_CMD_NULL;
	m->type  = 0x0;
	m->pes   = 0x0;
	m->mem   = 0x0;
	m->rid   = 0x0;
	m->flags = 0x0;
    m->nres  = 0x0;
	
	m->taskid = 0x0;
	memset(m->dependencies, 0, sizeof(uint32_t) * MCL_MAX_DEPENDENCIES);
    memset(&(m->pesdata), 0x0, sizeof(msg_pes_t));
    m->ndependencies = 0;
    m->resdata = NULL;
	return 0;
}

/*
 * Return:
 *    0 on success (message sent)
 *   -1 on failure (message not sent)
 *    1 no message has been sent because the operation would have blocked
 *      and the socket is open in non-blocking mode. Need to try again
*/
int msg_send(struct mcl_msg_struct* msg, int fd, struct sockaddr_un* dst)
{
    ssize_t data_size = MCL_MSG_SIZE + (sizeof(uint64_t) * msg->nres * 2);
	uint64_t*  data = (uint64_t*)malloc(data_size);
    if(!data){
        eprintf("Error allocating message memory.");
    }
	ssize_t len = data_size, ret;
	
	Dprintf("Sending msg cmd: 0x%" PRIx64 " to %s",msg->cmd,dst->sun_path);
	if(msg_assemble(msg, data)){
		eprintf("Error assembling message.");
                free(data);
		return -1;
	}

	ret = sendto(fd, (void*)data, len, 0, (struct sockaddr*) dst,
		     sizeof(struct sockaddr_un));
        free(data);

	if(ret == len)
		return 0;

	if(ret == -1 && errno == EAGAIN)
		return 1;
	else{
		eprintf("Error sending message 0x%" PRIx64 ".", msg->cmd);
		perror("sendto");
		return -1;
	}
	
	return 0;
}

/*
 *  Return:
 *     0 on success (message recevied)
 *    -1 on failure (no message received, something went wrong)
 *     1 if no message has been received but no error was detected (EGAIN, non-blocking)
 */
int msg_recv(struct mcl_msg_struct* msg, int fd, struct sockaddr_un* src)
{
	uint64_t data[8 + ((MCL_MAX_DEPENDENCIES+1)/2) + (2 * MCL_RES_ARGS_MAX)];
	uint64_t data_size = MCL_MAX_MSG_SIZE;
	socklen_t len;
	ssize_t   bytes;

	len = sizeof(struct sockaddr_un);
	bytes = recvfrom(fd, (void*) data, data_size, 0, (struct sockaddr*) src,
			 &len);

	if(bytes == -1 && errno == EAGAIN)
		return 1;
	else{
		if(bytes < 0){
			eprintf("Error receiving message");
			perror("recvfrom");
			return -1;
		}
	}

	Dprintf("Received %ld bytes from %s", bytes, src->sun_path);
	if(msg_disassemble(data, msg, src->sun_path)){
		eprintf("Error disassembling message.");
		return -1;
	}
	
	return 0;
}

void msg_free(struct mcl_msg_struct* msg) {
    free(msg->resdata);
    msg->resdata = NULL;
}
