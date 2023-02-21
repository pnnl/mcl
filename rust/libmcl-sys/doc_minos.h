/**
 * @file minos.h
 * @author Roberto Gioiosa (roberto.gioiosa@pnnl.gov)
 * @author Alok Kamatar
 * @brief Header file containing the external API, struct definitions, and bitmaps for the Minos Computing Library
 * @version 0.5
 * @date 2022-05-23
 * 
 */

#ifndef MINOS_H
#define MINOS_H

#ifdef __cplusplus
extern "C"
{
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/types.h>

// #ifdef __APPLE__
// #include <OpenCL/cl.h>
// #else
// #include <CL/cl.h>
// #endif

#define CL_MAX_PLATFORMS 64
#define CL_MAX_DEVICES 64
#define CL_MAX_TEXT 256

#define MCL_NULL 0x00
#define MCL_START_SCHED 0x01
#define MCL_STOP_SCHED 0x02

#define MCL_TIMEOUT 10000000000L // Time in ns
#define MCL_TASK_NAME_LEN 0x40

#define MCL_SET_BIND_WORKERS 0x01

/**
 * @defgroup General MCL General API
 * 
 * Interface for submitting requests and handling dependencies
 * 
 * @{
 */
#define MCL_REQ_COMPLETED 0x00UL
#define MCL_REQ_ALLOCATED 0x01UL
#define MCL_REQ_PENDING 0x02UL
#define MCL_REQ_INPROGRESS 0x03UL
#define MCL_REQ_EXECUTING 0x04UL
#define MCL_REQ_FINISHING 0x05UL

#define MCL_RET_UNDEFINED 0x00UL
#define MCL_RET_SUCCESS 0x01UL
#define MCL_RET_ERROR 0x02UL

#define MCL_TASK_NONE 0x00
#define MCL_TASK_CPU 0x01
#define MCL_TASK_GPU 0x02
#define MCL_TASK_FPGA 0x04
#define MCL_TASK_ANY (MCL_TASK_CPU | MCL_TASK_GPU | MCL_TASK_FPGA)
#define MCL_TASK_DFT_FLAGS MCL_TASK_ANY
#define MCL_TASK_TYPE_MASK 0xff

/**
 * @brief Task should be run without the scheduler considering resident memory
 * 
 * Flag passed to mcl_exec to indeicate that the scheduler should schedule the task without considering where
 * Resident memory is located. This is useful for testing, or perhaps for indicating that multiple replicas
 * of data should be created.
 *
 */
#define MCL_FLAG_NO_RES 0x100


#define MCL_PRG_NONE 0x01
#define MCL_PRG_SRC 0x02
#define MCL_PRG_IR 0x04
#define MCL_PRG_BIN 0x08
#define MCL_PRG_GRAPH 0x10
#define MCL_PRG_MASK 0xff
/**@}*/
 
/**
 * @defgroup Args Argument API
 * 
 * Flags that are used to label arguments to kernels
 * 
 * @{
 */
/**
 * @brief Argument hould be copied in to task
 * 
 * To be used with MCL_ARG_BUFFER. Indiactes that, if the buffer is not resident on the device, it will be copied from the host. 
 * If used with MCL_ARG_DYNAMIC | MCL_ARG_RESIDENT, this will only copy from the host on the first use of the buffer (or when used with MCL_ARG_INVALID), 
 * otherwise the data will come from the device where the data was most recently used.
 * When used with only MCL_ARG_RESIDENT, this will lead to a copy every time the data is used on a new device.
 *
 */
#define MCL_ARG_INPUT 0x001

/**
 * @brief Argument should be copied out
 * 
 * To be used with MCL_ARG_BUFFER. Indicates that, after the kernel/task is run, the data should be copied from the device back to the host buffer.
 *
 */
#define MCL_ARG_OUTPUT 0x002

/**
 * @brief Argument is a scalar, value at the address will be used as is in the kernel.
 *
 */
#define MCL_ARG_SCALAR 0x004

/**
 * @brief Argument is a buffer. Will lead to the creation (or use) of a device memory allocation. Must be used for input or output data.
 *
 */
#define MCL_ARG_BUFFER 0x008

/**
 * @brief Buffer should be left in device memory and managed by MCL.
 * 
 * Used with MCL_ARG_BUFFER. Resident data leads to persistent allocations of device memory by MCL. This can be used to reduce the overhead of data transfers and
 * increase the performance of MCL. Using only the MCL_ARG_RESIDENT flag leads to data that MCL considers read only for each kernel. The data
 * will not be moved from device to device between tasks.
 *
 */
#define MCL_ARG_RESIDENT 0x010

/**
 * @brief Data in device memory should be invalidated and copied in again.
 * 
 * Used with MCL_ARG_BUFFER | MCL_ARG_RESIDENT This flag is used when the host memory changes and needs to be updated on the device. 
 * All current allocations on devices are considered invalid, and data will be copied in again when the task is scheduled.
 *
 */
#define MCL_ARG_INVALID 0x020

/**
 * @brief Buffer can be declared as read only
 * 
 * Used with MCL_ARG_BUFFER. This refers only to how the data is allocated rather than how MCL moves and treats the data. 
 * Read only data can be allocated in read-only device memory.
 *
 */
#define MCL_ARG_RDONLY 0x040

/**
 * @brief Buffer can be declared as write only
 * 
 * Used with MCL_ARG_BUFFER. This refers only to how the data is allocated rather than how MCL moves and treats the data. 
 * Write only data can be allocated in write-only device memory.
 *
 */
#define MCL_ARG_WRONLY 0x080

/**
 * @brief Space holder for shared/local memory shared between work groups, address should be NULL
 *
 */
#define MCL_ARG_LOCAL 0x100

/**
 * @brief Done flag to indicate this is the last time the memory will be used
 * The device memory will be freed after execution
 * This should only be called on a single instance of this memory and
 * must be the exclusive referenece to that memory at the time of execution
 *
 */
#define MCL_ARG_DONE 0x200

/**
 * @brief Buffer that is written during kernels with changes that need to persist to other tasks.
 * 
 * Used with MCL_ARG_BUFFER | MCL_ARG_RESIDENT. Dynamic arguments it will create an exclusive copy of the memory that is transfered to the latest task that uses it.
 * If data needs to be copied in for the first use of the buffer, then MCL_ARG_INPUT needs to be specified as well. MCL assumes that dependencies are handled by the user.
 * For dynamic memory, this means that MCL assumes that only one concurrent task will use the memory, and there will be strict ordering with other tasks that use the buffer.
 * 
 */
#define MCL_ARG_DYNAMIC 0x400

/**
 * @brief Buffer needs to be copied in on use
 * 
 * Used with MCL_ARG_BUFFER | MCL_ARG_RESIDENT. Similar to MCL_ARG_INVALID, tells MCL that the host buffer is more recent than the device memory for this buffer. However, with
 * MCL_ARG_REWRITE, the same device allocation will be used for the new data. This means that with MCL_ARG_REWRITE, the size must stay the same as the previous use of the buffer.
 * If this cannot be guarenteed, use MCL_ARG_INVALID
 */
#define MCL_ARG_REWRITE 0x800
/**@}*/

/**
 * @defgroup DeviceStatus Device Status
 * 
 * API for Querying the status of the system
 * 
 * @{
 */
#define MCL_DEV_NONE 0x00
#define MCL_DEV_READY 0x01
#define MCL_DEV_ALLOCATED 0x02
#define MCL_DEV_ERROR 0x03
#define MCL_DEV_FULL 0x04
/**@}*/

#define MCL_DEV_DIMS 0x03

#define MCL_ERR_INVARG 0x01
#define MCL_ERR_MEMALLOC 0x02
#define MCL_ERR_INVREQ 0x03
#define MCL_ERR_INVPES 0x04
#define MCL_ERR_INVKER 0x05
#define MCL_ERR_INVDEV 0x06
#define MCL_ERR_SRVCOMM 0x07
#define MCL_ERR_INVTSK 0x08
#define MCL_ERR_MEMCOPY 0x09
#define MCL_ERR_EXEC 0x0a
#define MCL_ERR_INVPRG 0x0b
#define MCL_ERR_RESDATA 0x0c

#ifdef MCL_SHARED_MEM
#define MCL_ARG_SHARED 0x1000
#define MCL_SHARED_MEM_NEW 0x2000
#define MCL_SHARED_MEM_DEL_OLD 0x4000
#define MCL_HDL_SHARED 0x01
#else
#define MCL_ARG_SHARED 0x0
#define MCL_SHARED_MEM_NEW 0x0
#define MCL_SHARED_MEM_DEL_OLD 0x0
#define MCL_HDL_SHARED 0x0
#endif

    typedef struct mcl_device_info
    {
        uint64_t id;
        char name[CL_MAX_TEXT];
        char vendor[CL_MAX_TEXT];
        uint64_t type;
        uint64_t status;
        uint64_t mem_size;
        uint64_t pes;
        uint64_t ndims;
        uint64_t wgsize;
        size_t *wisize;
    } mcl_dev_info;

    typedef struct mcl_handle_struct
    {
        uint64_t cmd;
        uint32_t rid;
        uint64_t status;
        uint64_t flags;

        int ret;
#ifdef _STATS
        struct timespec stat_submit;
        struct timespec stat_setup;
        struct timespec stat_input;
        struct timespec stat_exec_start;
        struct timespec stat_exec_end;
        struct timespec stat_output;
        struct timespec stat_end;
        int64_t stat_true_runtime;
#endif
    } mcl_handle;

    typedef struct mcl_transfer_struct
    {
        uint64_t nargs;
        void **args;
        uint64_t *sizes;
        uint64_t *offsets;
        uint64_t *flags;
        uint64_t ncopies;
        mcl_handle **handles;
    } mcl_transfer;

    /**
     * @brief Initialize MCL
     * @ingroup General
     *
     * @param num_workers Number of concurrent workers will pull and execute tasks from the queue
     * @param flags Either 0 or MCL_SET_BIND_WORKERS to bind worker threads to CPUs
     * @return int 0 on success, non-zero otherwise
     */
    int mcl_init(uint64_t num_workers, uint64_t flags);

    /**
     * @brief Uninitialize MCL.
     * @ingroup General
     *
     * @return int 0 on success
     */
    int mcl_finit(void);

    /**
     * @brief Gets the number of available devices
     * @ingroup DeviceStatus
     *
     * @return uint32_t The number of devices
     */
    uint32_t mcl_get_ndev(void);

    /**
     * @brief Gets information about the specified device
     * @ingroup DeviceStatus
     *
     * @param devid id of device
     * @param devinfo struct to fill with device info
     * @return int 0 on success, MCL_ERR_INVDEV if devid > mcl_get_ndev()
     */
    int mcl_get_dev(uint32_t devid, mcl_dev_info *devinfo);

    /**
     * @brief Create an empty MCL task
     * @ingroup General
     *
     * @return mcl_handle* The task handle associated with the created task. Can only be used for once task
     */
    mcl_handle *mcl_task_create(void);

    /**
     * @brief Create an empty MCL task
     * @ingroup General
     *
     * @param props Bitmap of handle properties. Valid flags are MCL_HDL_SHARED
     * @return mcl_handle* The associated task
     */
    mcl_handle *mcl_task_create_with_props(uint64_t props);

    /**
     * @brief Creates a new task and initializes it with the specified kernel
     * @ingroup General
     *
     * @param prg_path Path to *.cl file containing the kernel
     * @param kname The name of the kernel
     * @param nargs Number of arguments
     * @param copts Additional compiler flags
     * @param flags 0 or MCL_FLAG_NO_RES
     * @return mcl_handle*
     */
    mcl_handle *mcl_task_init(char *prg_path, char *kname, uint64_t nargs, char *copts, unsigned long flags);

    /**
     * @brief Load a program
     * @ingroup General
     *
     * @param prg_path Path to file containing the program
     * @param copts Additional compiler flags
     * @param flags Type of program (source, IR, FPGA bitstream, DL graph, ...)
     * @return int 0 on success
     */
    int mcl_prg_load(char *prg_path, char *copts, unsigned long flags);

    /**
     * @brief Initialize a task to run the specified kernel
     * @ingroup General
     *
     * @param hdl Handle associated with task
     * @param kname The name of the kernel
     * @param nargs Number of arguments
     * @return int 0 on success
     */
    int mcl_task_set_kernel(mcl_handle *hdl, char *kname, uint64_t nargs);

    /**
     * @brief Set up an argument associated with a task
     * @ingroup Args
     *
     * @param hdl The task handle create by mcl_task_create
     * @param argid The index of the argument
     * @param addr A pointer to the data
     * @param size The size of the argument
     * @param flags Any of the MCL_ARG_* flags. Must include one of MCL_ARG_BUFFER or MCL_ARG_SCALAR
     * @return int  0 on success
     */
    int mcl_task_set_arg(mcl_handle *hdl, uint64_t argid, void *addr, size_t size, uint64_t flags);

    /**
     * @brief Same as mcl task set arg, particularly for buffers
     * @ingroup Args
     *
     * @param hdl
     * @param argid index of the argument for the task
     * @param addr Base address, if this is a subbuffer, is the address that was previously used or registered
     * @param size Size of the buffer
     * @param offset Offset of data inside buffer
     * @param flags Any of the MCL_ARG_* flags. Must include MCL_ARG_BUFFER
     * @return int
     */
    int mcl_task_set_arg_buffer(mcl_handle *hdl, uint64_t argid, void *addr, size_t size, off_t offset, uint64_t flags);

    /**
     * @brief Complete the task without executing  (i.e. trigger dependencies)
     * @ingroup General
     *
     * @param hdl The task handle created by mcl_task_create
     * @return int 0 on success
     */
    int mcl_null(mcl_handle *hdl);

    /** @addtogroup General
     *  @{
     */

    /**
     * @brief Execute a specified task
     *
     * @param hdl The task handle created by mcl_task_create
     * @param global_work_dims An array of size MCL_DEV_DIMS containing the number of threads in each dimension
     * @param local_work_dims An array of size MCL_DEV_DIMS contianing the local work dimensions
     * @param flags Additional task flags. Specify compute locations using MCL_TASK_* flags
     * @return int 0 if task is succefully able to be queued
     */
    int mcl_exec(mcl_handle *hdl, uint64_t *global_work_dims, uint64_t *local_work_dims, uint64_t flags);
    int mcl_exec2(mcl_handle *hdl, uint64_t *global_work_dims, uint64_t *local_work_dims, uint64_t *offset, uint64_t flags);
    int mcl_exec_with_dependencies(mcl_handle *hdl, uint64_t *global_work_dims, uint64_t *local_work_dims, uint64_t flags, uint64_t ndependencies, mcl_handle **dep_list);
    int mcl_exec2_with_dependencies(mcl_handle *hdl, uint64_t *global_work_dims, uint64_t *local_work_dims, uint64_t *offsets, uint64_t flags, uint64_t ndependencies, mcl_handle **dep_list);

    /**
     * @brief Create a transfer task.
     * A transfer task executes no computation, but can be used to put or remove buffers from devices
     * (i.e. if a an address needs to be invalidated because it might be reused later in the program
     * for a different buffer)
     *
     * @param nargs Number of arguments to transfer
     * @param ncopies Hint to the number of copies to make.
     * @param flags Flgas
     * @return mcl_transfer* The allocated transfer handle
     */
    mcl_transfer *mcl_transfer_create(uint64_t nargs, uint64_t ncopies, uint64_t flags);

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
    int mcl_transfer_set_arg(mcl_transfer *t_hdl, uint64_t idx, void *addr, size_t size, off_t offset, uint64_t flags);

    /**
     * @brief Executes a transfer. Asychronously moves data
     *
     * @param t_hdl transfer handle created by mcl_transfer_create
     * @param flags Flags to specify devices, same as mcl_exec
     * @return int 0 is task successfully enqued
     */
    int mcl_transfer_exec(mcl_transfer *t_hdl, uint64_t flags);

    /**
     * @brief Waits for transfers to complete
     *
     * @param t_hdl transfer handle created by mcl_transfer_create
     * @return int 0 if task successfully finished, otherwise MCL_ARG_TIMEOUT
     */
    int mcl_transfer_wait(mcl_transfer *t_hdl);

    /**
     * @brief Checks the status of a transfer
     *
     * @param t_hdl transfer handle created by mcl_transfer_create
     * @return int the status of the transfer
     */
    int mcl_transfer_test(mcl_transfer *t_hdl);

    /**
     * @brief Frees data associated with the transfer handle
     *
     * @param t_hdl
     * @return int 0 on success
     */
    int mcl_transfer_free(mcl_transfer *t_hdl);

    /**
     * @brief Free MCL handle and associated task
     * @pre Must be called after task has finished
     *
     * @param hdl The handle associated with the task
     * @return int 0 on success
     */
    int mcl_hdl_free(mcl_handle *hdl);

    /**
     * @brief Block until the task associated with handle has finished
     *
     * @param hdl THe handle associated with the task
     * @return int 0 if the task completed, -1 if the wait timed out
     */
    int mcl_wait(mcl_handle *hdl);

    /**
     * @brief Wait for all pending mcl tasks
     *
     * @return int 0 if all the tasks completed
     */
    int mcl_wait_all(void);

    /**
     * @brief Check the status of the handle
     *
     * @return the status of the handle. One of the MCL_REQ_* constants
     */
    int mcl_test(mcl_handle *);
    /**@}*/

    

    /**
     * @brief Register a buffer for future use with MCL resident memory
     * @ingroup Args
     * 
     * Use of this method allows exploitation of subbuffers using offsets. When MCL sees this buffer in a task
     * It will know that it is a reference to this section of memory, and it will use the same device allocation,
     * using only a portion of a large device buffer if necessary
     *
     * @param buffer Pointer to the data
     * @param size Size of the allocation
     * @param flags Argument flags, must include MCL_ARG_BUFFER | MCL_ARG_RESIDENT
     * @return int status of call, < 0 on failure
     */
    int mcl_register_buffer(void *buffer, size_t size, uint64_t flags);

    /**
     * @brief Unregisters a buffer from MCL Resident memory.
     * @ingroup Args
     * 
     * This method will remove any device allocation associated with the memory pointer. This could be resident data
     * created during the running of a task, or with a buffer passed to mcl_register_buffer. This method is not necessary (but still valid)
     * if MCL_ARG_DONE was passed to a previous kernel call
     *
     * @param buffer Pointer to the data
     * @return int Status of call (if memory was able to be freed). 
     */
    int mcl_unregister_buffer(void *buffer);

    /**
     * @brief Invalidates device allocations
     * @ingroup Args
     * 
     * This method will delete on device allocations associated with the buffer, but keep the reference in MCL resident data for future use.
     * @param buffer Pointer to the data (previously used)
     * @return int
     */
    int mcl_invalidate_buffer(void *buffer);

#ifdef MCL_SHARED_MEM
    /**
     * @brief Return a id for other processes to reference this task
     * @ingroup General
     * 
     * Returns a unqiue identifier for the task that can be used by another process to create dependencies 
     * to the task. This is a deterministic id based on the order the tasks were created (so it is possible to hard code dependencies when known)
     * The handle must have been created with MCL_HDL_SHARED
     *
     * @param hdl Handle refering to the shared task
     * @return uint32_t Unique identifier of the task that can be used from another process
     */
    uint32_t mcl_task_get_sharing_id(mcl_handle *hdl);

    /**
     * @brief Get the status of a task from another process
     * @ingroup General
     *
     * @param pid Process ID where the other task was created
     * @param hdl_id Id returned by mcl_task_get_sharing_id
     * @return int The status of the task, or < 0 if an error occurred 
     */
    int mcl_test_shared_hdl(pid_t pid, uint32_t hdl_id);

    /**
     * @brief Wait on a task from another process
     * @ingroup General
     *
     * @param pid Process ID where the other task was created
     * @param hdl_id Id returned by mcl_task_get_sharing_id
     * @return int The status of the task, or < 0 if an error occurred 
     */
    int mcl_wait_shared_hdl(pid_t pid, uint32_t hdl_id);

    /**
     * @brief Get a buffer that can be shared among tasks
     * @ingroup Args
     * 
     * Returns a host buffer that can be shared among tasks. Without the POCL extension, this will use the host buffer to transfer data between tasks.
     * With the POCL extensions (configured with --enable-pocl-extension) will lead to the use of on device shared memory between applications 
     *
     * @param name Identifier of the shared buffer
     * @param size Size of the shared buffer
     * @param flags Argument flags. Must include MCL_ARG_BUFFER | MCL_ARG_RESIDENT | MCL_ARG_SHARED. Can also include MCL-SHARED_* flags.
     * @return void* Host pointer to shared memory
     */
    void *mcl_get_shared_buffer(const char *name, size_t size, int flags);

    /**
     * @brief Release shared Memory
     *
     * @param address Address of a shared memory buffer
     */
    void mcl_free_shared_buffer(void *address);
#endif // MCL_SHARED_MEM

#ifdef __cplusplus
}
#endif

#endif
