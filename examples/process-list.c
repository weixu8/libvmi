/*
 * The LibVMI Library is an introspection library that simplifies access to 
 * memory in a target virtual machine or in a file containing a dump of 
 * a system's physical memory.  LibVMI is based on the XenAccess Library.
 *
 * Copyright (C) 2010 Sandia National Laboratories
 * Author: Bryan D. Payne (bpayne@sandia.gov)
 */

#include <libvmi/libvmi.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdio.h>

#define PAGE_SIZE 1 << 12

int main (int argc, char **argv)
{
    vmi_instance_t vmi;
    unsigned char *memory = NULL;
    uint32_t offset, next_process, list_head;
    char *procname = NULL;
    int pid = 0;
    int tasks_offset, pid_offset, name_offset;

    /* this is the VM or file that we are looking at */
    char *name = argv[1];

    /* initialize the libvmi library */
    if (vmi_init_name(&vmi, VMI_MODE_AUTO, name) == VMI_FAILURE){
        perror("failed to init LibVMI library");
        goto error_exit;
    }

    /* init the offset values */
    if (VMI_OS_LINUX == vmi_get_ostype(vmi)){
        tasks_offset = vmi_get_offset(vmi, "linux_tasks");
        name_offset = 0x194; /* pv, xen 3.3.1, centos 2.6.18-92.1.10.el5xen */
        pid_offset = vmi_get_offset(vmi, "linux_pid");
    }
    else if (VMI_OS_WINDOWS == vmi_get_ostype(vmi)){
        tasks_offset = vmi_get_offset(vmi, "win_tasks");
        name_offset = 0x174; /* Windows XP SP2 */
//        name_offset = 0x14c; /* Windows Vista */
        pid_offset = vmi_get_offset(vmi, "win_pid");
    }

    /* get the head of the list */
    if (VMI_OS_LINUX == vmi_get_ostype(vmi)){
        memory = vmi_access_kernel_sym(vmi, "init_task", &offset, PROT_READ);
        if (NULL == memory){
            perror("failed to get process list head");
            goto error_exit;
        }    
        memcpy(&next_process, memory + offset + tasks_offset, 4);
    }
    else if (VMI_OS_WINDOWS == vmi_get_ostype(vmi)){
        vmi_read_long_sym(vmi, "PsInitialSystemProcess", &list_head);
        memory = vmi_access_kernel_va(vmi, list_head, &offset, PROT_READ);
        if (NULL == memory){
            perror("failed to get EPROCESS for PsInitialSystemProcess");
            goto error_exit;
        }
        memcpy(&next_process, memory + offset + tasks_offset, 4);
        procname = (char *) (memory + offset + name_offset);
        memcpy(&pid, memory + offset + pid_offset, 4);
        printf("[%5d] %s\n", pid, procname);
    }
    list_head = next_process;
    munmap(memory, PAGE_SIZE);

    /* walk the task list */
    while (1){

        /* follow the next pointer */
        memory = vmi_access_kernel_va(vmi, next_process, &offset, PROT_READ);
        if (NULL == memory){
            perror("failed to map memory for process list pointer");
            goto error_exit;
        }
        memcpy(&next_process, memory + offset, 4);

        /* if we are back at the list head, we are done */
        if (list_head == next_process){
            break;
        }

        /* print out the process name */

        /* Note: the task_struct that we are looking at has a lot of
           information.  However, the process name and id are burried
           nice and deep.  Instead of doing something sane like mapping
           this data to a task_struct, I'm just jumping to the location
           with the info that I want.  This helps to make the example
           code cleaner, if not more fragile.  In a real app, you'd
           want to do this a little more robust :-)  See
           include/linux/sched.h for mode details */
        procname = (char *) (memory + offset + name_offset - tasks_offset);
        memcpy(&pid, memory + offset + pid_offset - tasks_offset, 4);

        /* trivial sanity check on data */
        if (pid < 0){
            continue;
        }
        printf("[%5d] %s\n", pid, procname);
        munmap(memory, PAGE_SIZE);
    }

error_exit:

    /* sanity check to unmap shared pages */
    if (memory) munmap(memory, PAGE_SIZE);

    /* cleanup any memory associated with the LibVMI instance */
    vmi_destroy(vmi);

    return 0;
}
