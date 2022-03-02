#include <unistd.h>

#include <minos.h>
#include "utils.h"

int main(void)
{
	uint32_t ndevs, i;
	struct mcl_device_info dev;
	
	mcl_banner("Discovery Test");	
	if(mcl_init(1,MCL_NULL)){
		printf("Error initializing Minos Computing Library. Aborting.\n");
		return -1;
	}

	ndevs = mcl_get_ndev();
	
	printf("Current systems has total of %u devices.\n", ndevs);

	for(i=0; i<ndevs; i++){
		if(!mcl_get_dev(i,&dev))
			printf("Device %" PRIu64 ": vendor: %s type: %" PRIx64
			       " status: 0x%" PRIx64 " PEs: 0x%" PRIx64 " mem: 0x%" PRIx64 "\n",
			       dev.id, dev.vendor, dev.type, dev.status, dev.pes, dev.mem_size);
		else
			printf("Error obtaining information for device %u.\n", i);
	}
	
	mcl_finit();
	mcl_verify(0);

	return 0;
}
