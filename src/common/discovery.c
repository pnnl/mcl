#include <utlist.h>

#include <minos.h>
#include <minos_internal.h>
#include <stats.h>
#include <tracer.h>
#include <stdint.h>

uint64_t avail_dev_types = MCL_TASK_NONE;

static inline uint64_t __get_pes(mcl_device_t *dev)
{
    Dprintf("Computing PEs for device %" PRIu64 ", type %" PRIu64 "...", dev->id, dev->type);
    uint64_t pes = dev->punits * dev->wgsize;
    return pes;
}

int class_cmp(mcl_class_t *a, mcl_class_t *b)
{
    return strcmp(a->name, b->name);
}

static inline int create_classes(mcl_platform_t *plt, uint64_t n, mcl_class_t **c)
{
    mcl_class_t *e = NULL;
    mcl_class_t edev;
    mcl_device_t *dev = NULL;
    Dprintf("Creating resource classes...");

    for (uint64_t i = 0; i < n; i++)
    {
        Dprintf("\tChecking platform %" PRIu64 "...", i);
        for (uint64_t j = 0; j < plt[i].ndev; j++)
        {
            dev = &(plt[i].devs[j]);
            Dprintf("\t\tChecking device %" PRIu64 " %s", j, dev->name);
            strcpy(edev.name, dev->name);
            LL_SEARCH(*c, e, &edev, class_cmp);
            if (!e)
            {
                Dprintf("\t\tNo class found, adding new class");
                e = (mcl_class_t *)malloc(sizeof(mcl_class_t));
                if (!e)
                {
                    eprintf("Error allocating class element. Aborting.");
                    return -1;
                }
                strcpy(e->name, dev->name);
                LL_APPEND(*c, e);
            }
        }
    }

#ifdef _DEBUG
    Dprintf("Class list:");
    LL_FOREACH(*c, e)
    Dprintf("  Class name: %s", e->name);
#endif

    return 0;
}

int resource_discover(cl_platform_id **ids, mcl_platform_t **plt,
                      mcl_class_t **class, uint64_t *platforms, uint64_t *devices)
{
    int i, j;
    mcl_platform_t *p;
    cl_uint nplt;
    cl_uint ndev, tdevs = 0;
    mcl_device_t *dev;

    Dprintf("Discovering computing resources...");

    if (clGetPlatformIDs(0, NULL, &nplt) != CL_SUCCESS)
    {
        eprintf("Error querying CL platforms. Returning.");
        goto err;
    }

    Dprintf("Detected %d CL platforms:", nplt);

    *ids = (cl_platform_id *)malloc(nplt * sizeof(cl_platform_id));
    if (*ids == NULL)
    {
        eprintf("Error allocating memory to store CL platform ID");
        perror("malloc");
        goto err;
    }

    *plt = (mcl_platform_t *)malloc(nplt * sizeof(mcl_platform_t));
    if (*plt == NULL)
    {
        eprintf("Error allocatingmemory to store MCL platform data");
        perror("malloc");
        goto err_ids;
    }

    if (clGetPlatformIDs(nplt, *ids, NULL) != CL_SUCCESS)
    {
        eprintf("Error obtainig list of CL platforms. Returning.");
        goto err_plt;
    }

    p = *plt;

    for (i = 0; i < nplt; i++)
    {

        p[i].id = i;
        p[i].cl_plt = (*ids)[i];

        if (clGetPlatformInfo(p[i].cl_plt, CL_PLATFORM_NAME, CL_MAX_TEXT,
                              p[i].name, NULL) != CL_SUCCESS)
        {
            eprintf("Error obtaining platform %d name. Returning.", i);
            goto err_plt;
        }

        if (clGetPlatformInfo(p[i].cl_plt, CL_PLATFORM_VENDOR, CL_MAX_TEXT,
                              p[i].vendor, NULL) != CL_SUCCESS)
        {
            eprintf("Error obtaining platform %d vendor. Returning.", i);
            goto err_plt;
        }

        if (clGetPlatformInfo(p[i].cl_plt, CL_PLATFORM_VERSION, CL_MAX_TEXT,
                              p[i].version, NULL) != CL_SUCCESS)
        {
            eprintf("Error obtaining platform %d version. Returning.", i);
            goto err_plt;
        }

        Dprintf("  Platform %d: Name: %s Vendor: %s Version: %s", i,
                p[i].name, p[i].vendor, p[i].version);

        if (clGetDeviceIDs(p[i].cl_plt, CL_DEVICE_TYPE_ALL, 0, NULL,
                           &ndev) != CL_SUCCESS)
        {
            eprintf("Error querying CL devices for platform %d. Returning.", i);
            goto err_plt;
        }

        p[i].ndev = ndev;
        Dprintf("  Platform %d has %d devices.", i, p[i].ndev);

        p[i].cl_dev = (cl_device_id *)malloc(p[i].ndev * sizeof(cl_device_id));
        if (!p[i].cl_dev)
        {
            eprintf("Error allocating memory to store CL device IDs for platform %d.", i);
            perror("malloc");
            goto err_plt;
        }

        p[i].devs = (mcl_device_t *)malloc(ndev * sizeof(mcl_device_t));
        if (!p[i].devs)
        {
            eprintf("Error allocating memory to store MCL device for platform %d.", i);
            perror("malloc");
            goto err_cldev;
        }

        if (clGetDeviceIDs(p[i].cl_plt, CL_DEVICE_TYPE_ALL, ndev, p[i].cl_dev,
                           NULL) != CL_SUCCESS)
        {
            eprintf("Error querying CL devices for platform %d. Returning.", i);
            goto err_dev;
        }

        dev = p[i].devs;
        for (j = 0; j < p[i].ndev; j++)
        {

            dev->id = j;
            dev->cl_plt = p[i].cl_plt;
            dev->cl_dev = p[i].cl_dev[j];

            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_GLOBAL_MEM_SIZE,
                                sizeof(cl_ulong), &dev->mem_size, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) - Global Memory. Returning.", i, j);
                goto err;
            }
            dev->mem_size *= (1 - MCL_DEV_MEM_SAFTEY_FACTOR);

            char driver_version[CL_MAX_TEXT];
            char *temp;
            if (clGetDeviceInfo(dev->cl_dev, CL_DRIVER_VERSION,
                                CL_MAX_TEXT, driver_version, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) - Driver Version. Returning.", i, j);
                goto err;
            }
            dev->driver_version = strtod(driver_version, &temp);

            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_MAX_COMPUTE_UNITS,
                                sizeof(cl_int), &dev->punits, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) - MAx Compute Units. Returning.", i, j);
                goto err;
            }

            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_MAX_WORK_GROUP_SIZE,
                                sizeof(size_t), &dev->wgsize, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) - Work Group Size. Returning.", i, j);
                goto err;
            }

            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_TYPE,
                                sizeof(cl_device_type), &dev->type, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) - Device Type. Returning.", i, j);
                goto err;
            }

            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_NAME,
                                CL_MAX_TEXT, dev->name, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) - Device Name. Returning.", i, j);
                goto err;
            }

            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_MAX_WORK_ITEM_DIMENSIONS,
                                sizeof(cl_uint), &dev->ndims, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) - Work Item Dimensions. Returning.", i, j);
                goto err;
            }

            dev->wisize = (size_t *)malloc(dev->ndims * sizeof(size_t));
            if (!dev->wisize)
            {
                printf("Error allocating device (%d,%d) max work sizes.", i, j);
                goto err;
            }
            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_MAX_WORK_ITEM_SIZES,
                                sizeof(size_t) * dev->ndims, dev->wisize, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) -Max Work Item Sizes. Returning.", i, j);
                goto err;
            }
#ifdef _DEBUG
            cl_uint mem_alignment;
            if (clGetDeviceInfo(dev->cl_dev, CL_DEVICE_MEM_BASE_ADDR_ALIGN,
                                sizeof(cl_uint), &mem_alignment, NULL) != CL_SUCCESS)
            {
                eprintf("Error querying CL device (%d,%d) -Memory Alignment. Returning.", i, j);
                goto err;
            }
            Dprintf("Required alignment size in bytes: %d", mem_alignment);
#endif
            dev->cl_ctxt = NULL;
            memset(dev->cl_queue, 0, sizeof(cl_command_queue) * MCL_MAX_QUEUES_PER_DEVICE);

            switch (dev->type)
            {
            case CL_DEVICE_TYPE_CPU:
                dev->type = MCL_TASK_CPU;
                dev->pes = __get_pes(dev);
                dev->max_kernels = MCL_DEV_MKERNELS_CPU;
                break;
            case CL_DEVICE_TYPE_GPU:
                dev->type = MCL_TASK_GPU;
                dev->pes = __get_pes(dev);
                dev->max_kernels = MCL_DEV_MKERNELS_GPU;
                break;
            case CL_DEVICE_TYPE_ACCELERATOR:
                dev->type = MCL_TASK_FPGA;
                dev->pes = __get_pes(dev);
                dev->max_kernels = MCL_DEV_MKERNELS_FPGA;
                break;
            case CL_DEVICE_TYPE_CUSTOM:
		if (strstr(dev->name, "PROTEUS") != NULL) {
			dev->type = MCL_TASK_PROTEUS;
			dev->max_kernels = MCL_DEV_MKERNELS_PROTEUS;	
		} else {
			dev->type = MCL_TASK_DF;
			dev->max_kernels = MCL_DEV_MKERNELS_DF;
		}
		dev->pes = __get_pes(dev);
		break;  
            }

            Dprintf("    Platform %" PRIu64 " Device %" PRIu64 ": Name: %s Type: 0x%" PRIx64 " PEs: %" PRIu64 " GlobalMemory: %f MB",
                    p[i].id, dev->id, dev->name, dev->type, dev->pes,
                    dev->mem_size / MEGA);
#ifdef _STATS
            dev->task_executed = 0;
            dev->task_successful = 0;
            dev->task_failed = 0;
#endif
            avail_dev_types |= dev->type;
            dev++;
            tdevs++;
        }
    }

    /* sanitize according to MCL_TASK_ANY */
    avail_dev_types &= MCL_TASK_ANY;

    Dprintf("Device Types Mask: ALL=0x%02" PRIx32 " Available Mask=0x%02" PRIx64 "",
            MCL_TASK_ANY, avail_dev_types);

    if (platforms)
        *platforms = nplt;
    if (devices)
        *devices = tdevs;

    create_classes(*plt, nplt, class);

    return 0;

err_cldev:
err_dev:
    for (; i >= 0; i--)
        free(p[i].cl_dev);
err_plt:
    free(*plt);
err_ids:
    free(*ids);
err:
    return -1;
}

static inline int class_get(mcl_class_t *class, mcl_device_t *dev)
{
    mcl_class_t *e = NULL;
    int id = 0;

    Dprintf("Mapping device %s to class...", dev->name);
    LL_FOREACH(class, e)
    {
        if (strcmp(e->name, dev->name) == 0)
        {
            Dprintf("Found device %s in class %d...", dev->name, id);
            return id;
        }
        id++;
    }

    eprintf("No class found for device %s!", dev->name);
    return -1;
}

int class_count(mcl_class_t *list)
{
    mcl_class_t *e;
    int count;

    LL_COUNT(list, e, count);

    return count;
}

int resource_map(mcl_platform_t *plt, uint64_t nplt, mcl_class_t *class, mcl_resource_t *res)
{
    uint64_t i, j;
    mcl_platform_t *p = plt;
    uint64_t nres = 0;

    Dprintf("Mapping resources....");

    for (i = 0; i < nplt; i++, p++)
    {
        for (j = 0; j < p->ndev; j++, nres++)
        {
            res[nres].plt = p;
            res[nres].dev = &(p->devs[j]);
            res[nres].pes_used = 0;
            res[nres].nkernels = 0;
            res[nres].mem_avail = p->devs[j].mem_size;
            res[nres].status = MCL_DEV_READY;
            res[nres].class = class_get(class, &p->devs[j]);

            Dprintf("    Mapped resource %" PRIu64 " to (%" PRIu64 ",%" PRIu64 ")",
                    nres, p->id, p->devs[j].id);

            TRprintf("R[%" PRIu64 "]: %" PRIu64 "/%" PRIu64 " used", nres,
                     res[nres].pes_used, res[nres].dev->pes);
        }
    }
    Dprintf("Mapped %" PRIu64 " devices.", nres);

    return 0;
}

#ifdef _DEBUG
void resource_list(mcl_resource_t *res, uint64_t n)
{
    uint64_t i, j;

    Dprintf("List of available resources:");
    for (i = 0; i < n; i++)
    {
        Dprintf("    Resource                %" PRIu64 ":", i);
        Dprintf("        Platform            %" PRIu64 " ", res[i].plt->id);
        Dprintf("            Name:           %s", res[i].plt->name);
        Dprintf("            Vendor:         %s", res[i].plt->vendor);
        Dprintf("            Version:        %s", res[i].plt->version);
        Dprintf("            Device:         %" PRIu64 ":", res[i].dev->id);
        Dprintf("                Name:       %s", res[i].dev->name);
        Dprintf("                Type:       0x%" PRIx64 "", res[i].dev->type);
        Dprintf("                Class:      0x%" PRIx64 "", res[i].class);
        Dprintf("                Dimensions: %u", res[i].dev->ndims);
        Dprintf("                PUnits:     %d", res[i].dev->punits);
        Dprintf("                WGSize:     %lu", res[i].dev->wgsize);
        Dprintf("                PEs:        %" PRIu64 "/%" PRIu64 "", res[i].pes_used,
                res[i].dev->pes);
        dprintf("[MCL_DEBUG]\t\t\t\t\t\t WISizes:    ");
        for (j = 0; j < res[i].dev->ndims; j++)
            dprintf("%lu ", res[i].dev->wisize[j]);
        dprintf("\n");
        Dprintf("                Mem:        %" PRIu64 "/%" PRIu64 "", res[i].mem_avail,
                res[i].dev->mem_size);
        Dprintf("                Status:     0x%" PRIx64 "", res[i].status);
    }
}
#endif

int resource_create_ctxt(mcl_resource_t *res, uint64_t n)
{
    uint64_t i, j, nqueues;
    cl_int error;

    Dprintf("Creating resource contexts...");
    for (i = 0; i < n; i++)
    {
        res[i].dev->cl_ctxt = clCreateContext(NULL, 1, &(res[i].dev->cl_dev), NULL, NULL, &error);
        if (error != CL_SUCCESS)
        {
            eprintf("Error creating context for MCL resource %" PRIu64 "", i);
            goto err;
        }

        nqueues = res[i].dev->type == MCL_TASK_GPU ? MCL_MAX_QUEUES_PER_DEVICE : 1;
        res[i].dev->nqueues = nqueues;

#if OPENCL2
        if (res[i].dev->driver_version > 1.0)
        {
            cl_command_queue_properties props[3] = {CL_QUEUE_PROPERTIES, 0, 0};
            for (j = 0; j < nqueues; j++)
            {
                res[i].dev->cl_queue[j] = clCreateCommandQueueWithProperties(res[i].dev->cl_ctxt, res[i].dev->cl_dev, props, &error);
                if (error != CL_SUCCESS)
                {
                    eprintf("Error creating command queue for MCL resource %" PRIu64 "", i);
                    goto err;
                }
            }
        }
        else
        {
#endif
            for (j = 0; j < nqueues; j++)
            {
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
                res[i].dev->cl_queue[j] = clCreateCommandQueue(res[i].dev->cl_ctxt, res[i].dev->cl_dev, 0, &error);
#pragma GCC diagnostic pop
                if (error != CL_SUCCESS)
                {
                    eprintf("Error creating command queue for MCL resource %" PRIu64 "", i);
                    goto err;
                }
            }
#if OPENCL2
        }
#endif
    }

    return 0;
err:
    return -1;
}
