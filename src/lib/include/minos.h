#ifdef __cplusplus  
extern "C" { 
#endif 

#ifndef MINOS_H
#define MINOS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

#define CL_MAX_PLATFORMS            64
#define CL_MAX_DEVICES              64
#define CL_MAX_TEXT                256

#define MCL_NULL                  0x00
#define MCL_START_SCHED           0x01
#define MCL_STOP_SCHED            0x02

#define MCL_TIMEOUT        10000000000L //Time in ns
#define MCL_TASK_NAME_LEN         0x40

#define MCL_REQ_COMPLETED         0x00UL
#define MCL_REQ_ALLOCATED         0x01UL
#define MCL_REQ_PENDING           0x02UL
#define MCL_REQ_INPROGRESS        0x03UL
#define MCL_REQ_FINISHING         0x04UL

#define MCL_RET_UNDEFINED         0x00UL
#define MCL_RET_SUCCESS           0x01UL
#define MCL_RET_ERROR             0x02UL

#define MCL_TASK_NONE             0x00
#define MCL_TASK_CPU              0x01
#define MCL_TASK_GPU              0x02
#define MCL_TASK_FPGA             0x04
#define MCL_TASK_ANY              (MCL_TASK_CPU | MCL_TASK_GPU | MCL_TASK_FPGA)

#define MCL_TASK_TYPE_MASK        0xFF

// This should be MCL_TASK_ANY | others
#define MCL_TASK_DFT_FLAGS        MCL_TASK_CPU

/**
 * @brief Task should be run without the scheduler considering resident memory
 * 
 */
#define MCL_FLAG_NO_RES           0x100

/**
 * @brief Argument hould be copied in to task
 * 
 */
#define MCL_ARG_INPUT             0x001

/**
 * @brief Argument should be copied out
 * 
 */
#define MCL_ARG_OUTPUT            0x002

/**
 * @brief Argument is a scalar
 * 
 */
#define MCL_ARG_SCALAR            0x004

/**
 * @brief Argument is a buffer
 * 
 */
#define MCL_ARG_BUFFER            0x008

/**
 * @brief Buffer should be left in device memory and managed by MCL. 
 * 
 */
#define MCL_ARG_RESIDENT          0x010

/**
 * @brief Data in device memory should be invalidated and copied in again
 * 
 */
#define MCL_ARG_INVALID           0x020

/**
 * @brief Buffer can be declared as read only
 * 
 */
#define MCL_ARG_RDONLY            0x040

/**
 * @brief Buffer can be declared as write only
 * 
 */
#define MCL_ARG_WRONLY            0x080

/**
 * @brief Space holder for shared/local memory shared between work groups, address should be NULL
 * 
 */
#define MCL_ARG_LOCAL             0x100


/**
 * @brief Done flag to indicate this is the last time the memory will be used
 * The device memory will be freed after execution
 * This should only be called on a single instance of this memory and
 * must be the exclusive referenece to that memory at the time of execution
 * 
 */
#define MCL_ARG_DONE              0x200

/**
 * @brief Dynamic memory flag is for memory that is written during kernels
 * with changes that need to persist to other kernels
 * it will create an exclusive copy of the memory that is only transfered when
 * necessary. Applications using dynamic memory need to ensure there is never
 * more than one refence to the memory or there is a risk a MCL could deadlock/livelock
 */
#define MCL_ARG_DYNAMIC           0x400

#define MCL_DEV_NONE              0x00
#define MCL_DEV_READY             0x01
#define MCL_DEV_ALLOCATED         0x02
#define MCL_DEV_ERROR             0x03
#define MCL_DEV_FULL              0x04

#define MCL_DEV_DIMS              0x03

#define MCL_ERR_INVARG            0x01
#define MCL_ERR_MEMALLOC          0x02
#define MCL_ERR_INVREQ            0x03
#define MCL_ERR_INVPES            0x04
#define MCL_ERR_INVKER            0x05
#define MCL_ERR_INVDEV            0x06
#define MCL_ERR_SRVCOMM           0x07
#define MCL_ERR_INVTSK            0x08
#define MCL_ERR_MEMCOPY           0x09
#define MCL_ERR_EXEC              0x0a
#define MCL_ERR_INVPRG            0x0b
#define MCL_ERR_RESDATA           0x0c

typedef struct mcl_device_info{
	uint64_t   id;
	char       name[CL_MAX_TEXT];
	char       vendor[CL_MAX_TEXT];
	uint64_t   type;
	uint64_t   status;
	uint64_t   mem_size;
	uint64_t   pes;
	uint64_t   ndims;
	uint64_t   wgsize;
	size_t*    wisize;
} mcl_dev_info;

typedef struct mcl_handle_struct{
	uint64_t   cmd;
	uint32_t   rid;
	uint64_t   status;

	int        ret;
#ifdef _STATS
  struct timespec stat_submit;
  struct timespec stat_setup;
  struct timespec stat_input;
  struct timespec stat_exec_start;
  struct timespec stat_exec_end;
  struct timespec stat_output;
  struct timespec stat_end;
  int64_t         stat_true_runtime;
#endif
} mcl_handle;

typedef struct mcl_transfer_struct{
    uint64_t    nargs;
    void**      args;
    uint64_t*   sizes;
    uint64_t*   flags;
    uint64_t    ncopies;
    mcl_handle** handles;
} mcl_transfer;

/**
 * @brief Initialize MCL
 * 
 * @param num_workers Number of concurrent workers will pull and execute tasks from the queue
 * @param flags Unimplemented
 * @return int 0 on success, non-zero otherwise
 */
int          mcl_init(uint64_t num_workers, uint64_t flags);

/**
 * @brief Uninitialize MCL.
 * 
 * @return int 0 on success
 */
int          mcl_finit(void);

/**
 * @brief Gets the number of available devices
 * 
 * @return uint32_t The number of devices
 */
uint32_t     mcl_get_ndev(void);

/**
 * @brief Gets information about the specified device
 * 
 * @param devid id of device
 * @param devinfo struct to fill with device info
 * @return int 0 on success, MCL_ERR_INVDEV if devid > mcl_get_ndev()
 */
int          mcl_get_dev(uint32_t devid, mcl_dev_info* devinfo);

/**
 * @brief Create an empty MCL task
 * 
 * @return mcl_handle* The task handle associated with the created task. Can only be used for once task
 */
mcl_handle*  mcl_task_create(void);

/**
 * @brief Creates a new task and initializes it with the specified kernel
 * 
 * @param prg_path Path to *.cl file containing the kernel
 * @param kname The name of the kernel
 * @param nargs Number of arguments
 * @param copts Additional compiler flags
 * @param flags 0 or MCL_FLAG_NO_RES
 * @return mcl_handle* 
 */
mcl_handle*  mcl_task_init(char* prg_path, char* kname, uint64_t nargs, char* copts, unsigned long flags);

/**
 * @brief Initialize a task to run the specified kernel
 * 
 * @param hdl Handle associated with task
 * @param prg_path Path to *.cl file containing the kernel
 * @param kname The name of the kernel
 * @param nargs Number of arguments
 * @param copts Additional compiler flags
 * @param flags 0 or MCL_FLAG_NO_RES
 * @return int 0 on success
 */
int          mcl_task_set_kernel(mcl_handle* hdl, char* prg_path, char* kname, uint64_t nargs, char* copts, unsigned long flags);

/**
 * @brief Set up an argument associated with a task
 * 
 * @param hdl The task handle create by mcl_task_create
 * @param argid The index of the argument
 * @param addr A pointer to the data
 * @param size The size of the argument
 * @param flags Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
 * @return int  0 on success
 */
int          mcl_task_set_arg(mcl_handle* hdl, uint64_t argid, void* addr, size_t size, uint64_t flags);

/**
 * @brief Complete the task without executing  (i.e. trigger dependencies)
 * 
 * @param hdl The task handle created by mcl_task_create
 * @return int 0 on success
 */
int          mcl_null(mcl_handle* hdl);

/**
 * @brief Execute a specified task
 * 
 * @param hdl The task handle created by mcl_task_create
 * @param global_work_dims An array of size MCL_DEV_DIMS containing the number of threads in each dimension
 * @param local_work_dims An array of size MCL_DEV_DIMS contianing the local work dimensions
 * @param flags Additional task flags. Specify compute locations using MCL_TASK_* flags
 * @return int 0 if task is succefully able to be queued
 */
int          mcl_exec(mcl_handle* hdl, uint64_t* global_work_dims, uint64_t* local_work_dims, uint64_t flags);

/**
 * @brief Create a transfer task.
 * A transfer task executes no computation, but can be used to put or remove buffers from devices 
 * (i.e. if a an address needs to be invalidated because it might be reused later in the program 
 * for a different buffer)
 * 
 * @param nargs Number of arguments to transfer
 * @param ncopies Hint to the number of copies to make.
 * @return mcl_transfer* The allocated transfer handle
 */
mcl_transfer*   mcl_transfer_create(uint64_t nargs, uint64_t ncopies);

/**
 * @brief Sets up an argument for a transfer handle. Same as mcl_task_set_arg but for a transfer.
 * 
 * @param t_hdl The transfer handle created by mcl_transfer_create
 * @param idx The index of the argument in the transfer list
 * @param addr Address of the data
 * @param size Size of the data
 * @param flags Argument flags. Same as mcl_task_st_arg
 * @return int 0 on succes, otherwise an error code
 */
int             mcl_transfer_set_arg(mcl_transfer* t_hdl, uint64_t idx, void* addr, size_t size, uint64_t flags);

/**
 * @brief Executes a transfer. Asychronously moves data
 * 
 * @param t_hdl transfer handle created by mcl_transfer_create
 * @param flags Flags to specify devices, same as mcl_exec
 * @return int 0 is task successfully enqued
 */
int             mcl_transfer_exec(mcl_transfer* t_hdl, uint64_t flags);

/**
 * @brief Waits for transfers to complete
 * 
 * @param t_hdl transfer handle created by mcl_transfer_create
 * @return int 0 if task successfully finished, otherwise MCL_ARG_TIMEOUT
 */
int             mcl_transfer_wait(mcl_transfer* t_hdl);

/**
 * @brief Checks the status of a transfer
 * 
 * @param t_hdl transfer handle created by mcl_transfer_create
 * @return int the status of the transfer
 */
int             mcl_transfer_test(mcl_transfer* t_hdl);

/**
 * @brief Frees data associated with the transfer handle
 * 
 * @param t_hdl 
 * @return int 0 on success
 */
int             mcl_transfer_free(mcl_transfer* t_hdl);

/**
 * @brief Free MCL handle and associated task 
 * @pre Must be called after task has finished
 * 
 * @param hdl The handle associated with the task
 * @return int 0 on success
 */
int          mcl_hdl_free(mcl_handle* hdl);

/**
 * @brief Block until the task associated with handle has finished
 * 
 * @param hdl THe handle associated with the task
 * @return int 0 if the task completed, -1 if the wait timed out
 */
int          mcl_wait(mcl_handle* hdl);

/**
 * @brief Wait for all pending mcl tasks
 * 
 * @return int 0 if all the tasks completed
 */
int          mcl_wait_all(void);

/**
 * @brief Check the status of the handle
 * 
 * @return the status of the handle. One of the MCL_REQ_* constants
 */
int          mcl_test(mcl_handle*);

#ifdef __cplusplus
}
#endif

#endif

#ifdef __cplusplus 
} 
#endif 
