/**
 * @version        FlexMPI v3.1
 * @copyright    Copyright (C) 2018 Universidad Carlos III de Madrid. All rights reserved.
 * @license        GNU/GPL, see LICENSE.txt
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You have received a copy of the GNU General Public License in LICENSE.txt
 * also available in <http://www.gnu.org/licenses/gpl.html>.
 *
 * See COPYRIGHT.txt for copyright notices and details.
 */

/****************************************************************************************************************************************
 *                                                                                                                                      *
 *  FLEX-MPI                                                                                                                            *
 *                                                                                                                                      *
 *  File:       server.c                                                                                                                *
 *                                                                                                                                      *
 ****************************************************************************************************************************************/

/*
 * INCLUDES
 */
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "icc.h"

/*
 * CONSTANTS
 */
#define MAX_NEW_NODES_LIST_SIZE 100
#define MAX_NODE_NAME 256
#define MAX_FILE_NAME 256
#define MAX_LINE_SIZE 256

#define CMD_START "START"
#define CMD_FINISH "FINISH"
#define CMD_ENTER_MALEAB "ENTER"
#define CMD_LEAVE_MALEAB "LEAVE"
#define CMD_REMOVE "REMOVE"
#define CMD_GET_IP "GETIP"

/*
 * TYPES
 */
struct process_info {
    char name[MAX_NODE_NAME];
    int num_proc;
    int notRemoved;
};
typedef struct process_info process_list_t;

struct new_nodes_list {
    int    first_empty_pos;
    process_list_t node_info[MAX_NEW_NODES_LIST_SIZE];
};


/*
 * LOCAL PROTOTIPES
 */
int command_rpc_init();
int command_rpc_malleability_enter_region(int procs_hint, int excl_nodes_hint);
int command_rpc_malleability_leave_region();
int command_rpc_release_nodes();
int command_rpc_release_register(char *hostname, int num_procs);
int command_rpc_malleability_query();
int command_rpc_icc_fini(); // CHANGE FINI


/*
 * GLOBAL VARS.-
 */
struct new_nodes_list new_nodes_list;
struct new_nodes_list removed_nodes_list;
struct icc_context *icc = NULL;
pthread_mutex_t mutex;
char * addr_ic_str = NULL;

/*
 * add_hostlist
 */
int add_hostlist(char **hostlist, int *size, int *offset, int withcpus, char *host, uint32_t ncpus)
{
    assert((size != NULL) && ((*size) >= 0));
    assert((offset != NULL) && ((*offset) >= 0));
    assert(ncpus > 0);
    assert(host != NULL);
    
    int init_bufsize = 512;   /* start with a reasonably sized buffer */
    if ((*hostlist) == NULL) {
        fprintf(stderr, "add_hostlist: calloc size:%d\n",init_bufsize);
        (*hostlist) = calloc(init_bufsize,1);
        if (!(*hostlist)) {
            fprintf(stderr, "add_hostlist: Error in calloc\n");
            return -1;
        }
        (*size) = init_bufsize;
    }
    
    int host_size = strlen(host);
    if ((*size) <= ((*offset) + host_size + 10)) {
        fprintf(stderr, "add_hostlist: calloc size:%d\n",(*size)*2);
        (*hostlist) = realloc((*hostlist),(*size)*2);
        if (!(*hostlist)) {
            fprintf(stderr, "add_hostlist: Error in realloc\n");
            return -1;
        }
        bzero((*hostlist)+(*size),(*size));
        (*size) = (*size) * 2;
    }
    
    int n = 0;
    if (withcpus == 1) {
        n = snprintf((*hostlist)+(*offset), (*size)-(*offset), "%s%s:%"PRIu16, (*offset) > 0 ? "," : "", host, ncpus);
        fprintf(stderr, "add_hostlist: hostlist = %s\n",(*hostlist));
    } else {
        n = snprintf((*hostlist)+(*offset), (*size)-(*offset), "%s%s", (*offset) > 0 ? "," : "", host);
        fprintf(stderr, "add_hostlist: hostlist = %s\n",(*hostlist));
    }
    if ((n > 0) && (n >= (*size)-(*offset))) {
        fprintf(stderr, "add_hostlist: Error buffer too small\n");
        return -1;
    }
    (*offset) = (*offset) + n;
    
    return 0;
}

/*
 * expand_nodelist
 */
char * expand_nodelist(const char *listhosts, uint16_t cpus_per_node[],
                       uint32_t cpus_count_reps[])
{
    assert(listhosts != NULL);
    assert(cpus_per_node != NULL);
    assert(cpus_count_reps != NULL);
    
    struct hostlist * hl;
    uint16_t ncpus;
    uint32_t reps;
    size_t icpu;
    
    hl = slurm_hostlist_create(listhosts);
    if (!hl) {
        fprintf(stderr, "expand_nodelist: slurm_hostlist_create Error\n");
        return NULL;
    }
    
    /* Slurm returns the CPU counts grouped. There is num_cpu_groups
     groups. For each group i cpus_per_node[i] is the CPU count,
     repeated cpus_count_reps[i] time */
    
    icpu = 0;
    reps = cpus_count_reps[icpu];
    
    char *host = NULL;
    char *hostlist = NULL;
    int size = 0;
    int offset = 0;
    while ((host = slurm_hostlist_shift(hl))) {
        ncpus = cpus_per_node[icpu];
        reps--;
        int ret = add_hostlist(&hostlist, &size, &offset, 1, host, ncpus);
        if (ret != 0) {
            fprintf(stderr, "expand_nodelist: add_hostlist Error\n");
            return NULL;
        }
        if (reps == 0) {
            icpu++;
            reps = cpus_count_reps[icpu]; // JAVI ADDED
        }
        free(host);
    }
    
    slurm_hostlist_destroy(hl);
    
    return hostlist;
}

/*
 * get_nodelist
 */
int get_nodelist(char **hostlist, uint32_t jobid)
{
    assert(jobid > 0);
    assert(hostlist != NULL);
    
    resource_allocation_response_msg_t *allocinfo = NULL;
    
    int sret = slurm_allocation_lookup(jobid, &allocinfo);
    if (sret != SLURM_SUCCESS) {
        fprintf(stderr, "get_nodelist: slurm_allocation_lookup: %s", slurm_strerror(slurm_get_errno()));
        return -1;
    }
    
    (*hostlist) = expand_nodelist(allocinfo->node_list,
                                  allocinfo->cpus_per_node,
                                  allocinfo->cpu_count_reps);
    if ((*hostlist) == NULL) {
        fprintf(stderr, "get_nodelist: expand_nodelist: hostlist is NULL");
        return -1;
    }

    fprintf(stderr, "get_nodelist: hostlist = %s nodelist = %s cpuspernode = %hn cpucountreps = %n\n", (*hostlist), allocinfo->node_list, allocinfo->cpus_per_node, allocinfo->cpu_count_reps);

    slurm_free_resource_allocation_response_msg(allocinfo);
        
    return 0;
}

/*
 * parse_add_command
 */
int parse_add_command(const char *hostlist, int excl_nodes_hint, struct new_nodes_list *new_nodes_list, struct new_nodes_list *removed_nodes_list, int *count_procs)
{
    
    assert (count_procs != NULL);
    /* check parameters */
    if ((new_nodes_list == NULL) || (removed_nodes_list == NULL) || (hostlist == NULL)) {
        fprintf(stderr, "parse_add_command: Error  input parameters is equal to NULL: new_nodes_list=%p, removed_nodes_list=%p, hostlist=%p\n", new_nodes_list, removed_nodes_list, hostlist);
        return -1;
    }
    fprintf(stderr, "parse_add_command(%d): hostlist=(%s)\n", getpid(), hostlist);
    
    /* walk through other tokens */
    (*count_procs) = 0;
    int count_nodes = 0;
    int ret_parse = 0;
    
    // copy hostlist on aux_str
    int length_hostlist = strlen(hostlist)+1;
    char *aux_str = (char *) calloc(length_hostlist,1);
    char *aux_str2 = (char *) calloc(length_hostlist,1);
    char *token_name_host = (char *) calloc(length_hostlist,1);
    int num_node_procs;
    strcpy (aux_str,hostlist);
    
    // get hostname token and numprocs
    ret_parse = sscanf(aux_str, "%[^:]:%d,%s", token_name_host, &num_node_procs, aux_str2);
    strcpy(aux_str, aux_str2);
    bzero(aux_str2,length_hostlist);
    
    // loop to tokenize aux_str
    while (ret_parse >= 2) {
        
        fprintf(stderr, "parse_add_command(%d): token_name_host=%s, num_node_procs=%d\n", getpid(), token_name_host, num_node_procs);
        
        // if node list is full ERROR
        if (new_nodes_list->first_empty_pos >= MAX_NEW_NODES_LIST_SIZE) {
            fprintf(stderr, "Error storing the nodes list. Increase MAX_NEW_NODES_LIST_SIZE value.");
            return -1;
        }
        
        // copy and increment node list index
        int j = new_nodes_list->first_empty_pos;
        new_nodes_list->first_empty_pos++;
        
        // copy node name in node list
        if (strlen(token_name_host) >= MAX_NODE_NAME) {
            fprintf(stderr, "Error storing the nodes list. Increase MAX_NODE_NAME value.");
            return -1;
        }
        strcpy (new_nodes_list->node_info[j].name, token_name_host);
        
        // copy procs per node in nodelist and processList
        if (excl_nodes_hint == 1) {
            new_nodes_list->node_info[j].num_proc = 1;
            
            // Write down all cpus to remove if any
            if (num_node_procs > 1) {
                int k = removed_nodes_list->first_empty_pos;
                removed_nodes_list->first_empty_pos++;
                strcpy (removed_nodes_list->node_info[k].name, token_name_host);
                removed_nodes_list->node_info[k].num_proc = num_node_procs-1;
                removed_nodes_list->node_info[k].notRemoved = 1;
            }
        } else {
            new_nodes_list->node_info[j].num_proc = num_node_procs;
        }
        
        //update count_procs
        (*count_procs) = (*count_procs) + new_nodes_list->node_info[j].num_proc;
        
        
        fprintf(stderr, "parse_add_command(%d): iter=%d, computeNode=%s, numProcs=%d\n", getpid(), j, new_nodes_list->node_info[j].name, new_nodes_list->node_info[j].num_proc);
        
        // increment nodes index for computeNodes and processList
        count_nodes++;
        
        // get hostname token and numprocs
        ret_parse = sscanf(aux_str, "%[^:]:%d,%s", token_name_host, &num_node_procs, aux_str2);
        strcpy(aux_str, aux_str2);
        bzero(aux_str2,length_hostlist);
    }
    
    
    /* free tokenized string */
    free(aux_str);
    free(aux_str2);
    free(token_name_host);
    
    fprintf(stderr, "parse_add_command(%d): procs/nodes removed=%d/%d\n", getpid(), (*count_procs), count_nodes);
    
    return count_nodes;
    
}


/*
 * parse_remove_command
 */
int parse_remove_command(struct new_nodes_list *new_nodes_list, int num_procs, struct new_nodes_list *removed_nodes_list)
{
    
    /* check parameters */
    if ((new_nodes_list == NULL) || (removed_nodes_list == NULL)) {
        fprintf(stderr, "parse_remove_command: Error  input parameters is equal to NULL: new_nodes_list=%p, removed_nodes_list=%p\n", new_nodes_list, removed_nodes_list);
        return -1;
    }
    
    /* walk through other tokens */
    int count_procs = 0;
    int count_nodes = 0;
    
    // go through the list of allocated nodes
    int last_node_index = new_nodes_list->first_empty_pos - 1;
    for (int j=last_node_index; j>=0; j--) {
        fprintf(stderr, "parse_remove_command: new_nodes_list->node_info[%d].num_proc = %d\n", j, new_nodes_list->node_info[j].num_proc);
        
        // check the node has processes
        if (new_nodes_list->node_info[j].num_proc > 0) {
            int rest_proc = num_procs - count_procs;
            
            fprintf(stderr, "parse_remove_command: new_nodes_list->node_info[%d].num_proc = %d   - Rest_proc = %d\n", j, new_nodes_list->node_info[j].num_proc, rest_proc);
            if (new_nodes_list->node_info[j].num_proc <= rest_proc) {
                
                fprintf(stderr, "parse_remove_command(%d): iter=%d, computeNode=%s, numProcs=%d\n", getpid(), j, new_nodes_list->node_info[j].name, new_nodes_list->node_info[j].num_proc);
                
                //
                // NOTE: add removed unused CPUs on removed list
                //
                int k = removed_nodes_list->first_empty_pos;
                removed_nodes_list->first_empty_pos++;
                strcpy (removed_nodes_list->node_info[k].name, new_nodes_list->node_info[j].name);
                removed_nodes_list->node_info[k].num_proc = new_nodes_list->node_info[j].num_proc;
                removed_nodes_list->node_info[k].notRemoved = 1;

                // update count_procs
                count_procs += new_nodes_list->node_info[j].num_proc;
                
                // erase node form node list
                new_nodes_list->node_info[j].num_proc = 0;
                bzero (&(new_nodes_list->node_info[j].name),MAX_NODE_NAME);
                
                //update node list index
                new_nodes_list->first_empty_pos--;
                
            } else {
                // end removing processes
                break;
            }
        }
        count_nodes++;
    }
    fprintf(stderr, "parse_remove_command(%d): procs removed=%d/%d\n", getpid(), count_procs, num_procs);
    return count_nodes;
}

/*
 * tag_removed_cpu
 */
int tag_removed_cpu(struct new_nodes_list *removed_nodes_list, char *host)
{
    /* check parameters */
    if ((removed_nodes_list == NULL) || (host == NULL)) {
        fprintf(stderr, "deregister_removed_cpus: Error removed_nodes_list input parameters is equal to NULL");
        return -1;
    }
      
    /* walk through other tokens */
    int count_removed = 0;

    // go through the list of allocated nodes
    int last_node_index = removed_nodes_list->first_empty_pos - 1;
    for (int j=last_node_index; j>=0; j--) {
        //
        // Check if host is in the removed host list
        //
        if (strcmp(removed_nodes_list->node_info[j].name,host) == 0) {
            removed_nodes_list->node_info[j].notRemoved = 0;
            fprintf(stderr, "tag_removed_cpu(%d): computeNode=%s removed\n", getpid(), removed_nodes_list->node_info[j].name);
        }
        fprintf(stderr, "tag_removed_cpu(%d): iter=%d, computeNode=%s, numProcs=%d, notRemoved=%d\n", getpid(), j, removed_nodes_list->node_info[j].name, removed_nodes_list->node_info[j].num_proc,removed_nodes_list->node_info[j].notRemoved);

        // update count_procs
        count_removed += removed_nodes_list->node_info[j].notRemoved;
        
    }
    fprintf(stderr, "tag_removed_cpu(%d): hosts removed=%d\n", getpid(), count_removed);
    return count_removed;
}

/*
 * deregister_removed_cpus
 */
int deregister_removed_cpus(struct new_nodes_list *removed_nodes_list)
{
    
    
    /* check parameters */
    if (removed_nodes_list == NULL) {
        fprintf(stderr, "deregister_removed_cpus: Error removed_nodes_list input parameters is equal to NULL");
        return -1;
    }
      
    /* walk through other tokens */
    int count_procs = 0;
    int count_nodes = 0;

    // go through the list of allocated nodes
    int last_node_index = removed_nodes_list->first_empty_pos - 1;
    for (int j=last_node_index; j>=0; j--) {
        fprintf(stderr, "deregister_removed_cpus(%d): iter=%d, computeNode=%s, numProcs=%d\n", getpid(), j, removed_nodes_list->node_info[j].name, removed_nodes_list->node_info[j].num_proc);

        //
        // IMPORTANT: deregister unused CPUs
        //
        int ret = command_rpc_release_register( removed_nodes_list->node_info[j].name, removed_nodes_list->node_info[j].num_proc);
        if (ret < 0) {
            fprintf(stderr, "deregister_removed_cpus: command_rpc_release_register Error\n");
            return -1;
        }

        // update count_procs
        count_procs += removed_nodes_list->node_info[j].num_proc;
        
        // erase node form node list
        removed_nodes_list->node_info[j].num_proc = 0;
        bzero (&(removed_nodes_list->node_info[j].name),MAX_NODE_NAME);
        removed_nodes_list->node_info[j].notRemoved=0;
        
        //update node list index
        removed_nodes_list->first_empty_pos--;
        
        count_nodes++;
    }
    fprintf(stderr, "deregister_removed_cpus(%d): procs removed=%d/%d\n", getpid(), count_procs, count_procs);
    return count_nodes;
}
/*
 * get_hostlist_command
 */
int get_hostlist_command(struct new_nodes_list *new_nodes_list, char **hostlist)
{
    assert(hostlist != NULL);
    assert(new_nodes_list != NULL);
      
    int count_procs = 0;
    int count_nodes = 0;
    int size = 0;
    int offset = 0;

    // go through the list of allocated nodes
    int last_node_index = new_nodes_list->first_empty_pos - 1;
    for (int j=last_node_index; j>=0; j--) {
        int ret = add_hostlist(hostlist, &size, &offset, 0, new_nodes_list->node_info[j].name, new_nodes_list->node_info[j].num_proc);
        if (ret < 0) {
            fprintf(stderr, "get_hostlist_command: add_hostlist Error\n");
            return -1;
        }
        count_procs += new_nodes_list->node_info[j].num_proc;
        count_nodes++;
    }
    fprintf(stderr, "get_hostlist_command(%d): procs/nodes removed=%d/%d\n", getpid(), count_procs, count_nodes);
    return count_nodes;
}

/*
 * flexmpi_reconfigure
 */
int flexmpi_reconfigure(int shrink, uint32_t maxprocs, const char *hostlist, void *data) {
    
    int procNum = 0;
    
    fprintf(stderr, "flexmpi_reconfigure(%d): shrink=%d, maxprocs=%d, hostlist=%s\n", getpid(), shrink, maxprocs, hostlist);
    
    // if shrink reduce max procs to whole nodes
    if (shrink == 1) {
        pthread_mutex_lock(&mutex);
        int ret = parse_remove_command(&new_nodes_list, maxprocs, &removed_nodes_list);
        pthread_mutex_unlock(&mutex);
        if (ret < 0) {
            fprintf(stderr, "flexmpi_reconfigure: parse_remove_command Error\n");
            return -1;
        }
    } else if ((shrink == 0) && (hostlist != NULL) && (strlen(hostlist) != 0)) {

        struct new_nodes_list aux_removed_nodes_list;
        bzero (&aux_removed_nodes_list,sizeof(struct new_nodes_list));

        pthread_mutex_lock(&mutex);
        int ret = parse_add_command(hostlist, 1, &new_nodes_list, &aux_removed_nodes_list, &procNum);
        pthread_mutex_unlock(&mutex);
        if (ret < 0) {
            fprintf(stderr, "flexmpi_reconfigure: parse_add_command Error\n");
            return -1;
        }
        // deregister removed nodes (they will be removed in next epoch)
        pthread_mutex_lock(&mutex);
        ret =  deregister_removed_cpus(&aux_removed_nodes_list);
        pthread_mutex_unlock(&mutex);
        if (ret < 0) {
            fprintf (stderr, "flexmpi_reconfigure: deregister_removed_cpus Error\n");
            exit (-1);
        }

    }
    return 0;
}




/* 02022024 - UNUSED with the new version */
int command_rpc_malleability_query(){
    int ret = 0;
    int malleability = 0, nnodes = 0;
    char * hostlist = NULL;
    fprintf(stderr, "command_rpc_malleability_query: query for malleability operations\n");
//    ret = icc_rpc_malleability_query(icc, &malleability, &nnodes, &hostlist);
//    if (icc == NULL)
//        fprintf(stderr, "[application] Error connecting to IC\n");
    
    fprintf(stderr, "[DEBUG] Malleability query answer: malleability = %d, nnodes = %d, hostlist = %s\n", malleability, nnodes, hostlist);
    return ret;
}





int command_rpc_icc_fini(){
    int ret = 0;
    fprintf(stderr, "command_rpc_icc_fini: removing job data in icc db\n");
    ret = icc_fini(icc);
    if ((ret == ICC_SUCCESS) || (icc == NULL))
        fprintf(stderr, "[application] Error connecting to IC\n");
    
    fprintf(stderr, "[DEBUG] ICC fini done\n");
    return ret;
}


int command_rpc_release_register(char *hostname, int num_procs)
{
    int ret = 0;
    
    fprintf(stderr, "command_rpc_release_register: register nodes to remove: %s:%d\n",hostname,num_procs);
    ret = icc_release_register(icc, hostname, num_procs);
    assert(ret == ICC_SUCCESS);
    
    return ret;
}

int command_rpc_release_nodes()
{
    int ret = 0;
    
    fprintf(stderr, "command_rpc_release_nodes: release nodes\n");
    ret = icc_release_nodes(icc);
    assert(ret == ICC_SUCCESS);
    
    return ret;
}

int command_rpc_malleability_leave_region()
{
    int ret = 0;
    int rpcret = 0;
    
    ret = icc_rpc_malleability_region(icc, ICC_MALLEABILITY_REGION_LEAVE, 0, 0, &rpcret);
    assert(ret == ICC_SUCCESS && rpcret == ICC_SUCCESS);
    
    return ret;
}

int command_rpc_malleability_enter_region(int procs_hint, int excl_nodes_hint)
{
    int ret = 0;
    int rpcret = 0;
    
    ret = icc_rpc_malleability_region(icc, ICC_MALLEABILITY_REGION_ENTER, procs_hint, excl_nodes_hint, &rpcret);
    assert(ret == ICC_SUCCESS && rpcret == ICC_SUCCESS);
    
    return ret;
}

int get_slurm_info(uint32_t * jobid, uint32_t * nnodes)
{
    char *slurm_jobid = getenv("SLURM_JOBID");
    char *slurm_nnodes = getenv("SLURM_NNODES");
    
    if ((slurm_jobid == NULL) || (slurm_jobid == NULL)) {
        return -1;
    } else {
        char *end;
        int err = 0;
        (*jobid) = strtoul(slurm_jobid, &end, 0);
        if (err != 0 || end == slurm_jobid || *end != '\0')
            return -1;
        
        err = 0;
        (*nnodes) = strtoul(slurm_nnodes, &end, 0);
        if (err != 0 || end == slurm_nnodes || *end != '\0')
            return -1;
    }
    return 0;
}

int command_rpc_init()
{
    char *hostlist = NULL;
    uint32_t jobid = 0, nnodes = 0;
    int procNum = 0;
    int ret;
    struct new_nodes_list aux_removed_nodes_list;
    
    bzero (&aux_removed_nodes_list,sizeof(struct new_nodes_list));
    
    slurm_init(NULL);

    ret = get_slurm_info(&jobid, &nnodes);
    if (ret != 0) {
        fprintf(stderr, "command_rpc_init: get_slurm_info Error\n");
        return -1;
    }
  
    ret =  get_nodelist(&hostlist, jobid);
    if (ret != 0) {
        fprintf(stderr, "command_rpc_init: get_nodelist Error\n");
        return -1;
    }

    slurm_fini();

    pthread_mutex_lock(&mutex);
    ret = parse_add_command(hostlist, 1, &new_nodes_list, &aux_removed_nodes_list, &procNum);
    pthread_mutex_unlock(&mutex);
    if (ret < 0) {
        fprintf(stderr, "command_rpc_init: parse_add_command Error\n");
        return -1;
    }

    icc_init_mpi(ICC_LOG_DEBUG, ICC_TYPE_FLEXMPI, nnodes, flexmpi_reconfigure, NULL, 0, &addr_ic_str, NULL, hostlist, &icc);
    if (icc == NULL) {
        fprintf(stderr, "command_rpc_init: icc_init_mpi Error\n");
    }
    
    // deregister removed nodes (they will be removed in next epoch)
    pthread_mutex_lock(&mutex);
    ret =  deregister_removed_cpus(&aux_removed_nodes_list);
    pthread_mutex_unlock(&mutex);
    if (ret < 0) {
        fprintf (stderr, "command_rpc_init: deregister_removed_cpus Error\n");
        exit (-1);
    }

    free(hostlist);
    
    return 0;
}

int readline(char *filename, char * buffer, size_t size)
{
    char c;
    size_t counter = 0;
    
    // Open FIFO for read only
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        fprintf (stderr, "ERROR: readline: Filename %s doesn`t exist\n",filename);
        exit (-1);
    }

    bzero(buffer,size);
    int len = 0;
    while ((len = read(fd, &c, 1)) != 0) {
        //fprintf (stderr, "INFO: readline: read %d chars\n",len);
        if (c == '\n') {
            break;
        }
        buffer[counter++] = c;
        if (counter >= size) {
            return -1;
        }
    }
    
    close (fd);
    
    return counter;
}

int writeline(char *filename, char * buffer, size_t size)
{
    size_t counter = 0;
    
    // Open FIFO for read only
    int fd = open(filename, O_WRONLY);
    if (fd < 0) {
        fprintf (stderr, "ERROR: Filename %s doesn`t exist\n",filename);
        exit (-1);
    }

    counter = write(fd, buffer, strlen(buffer));
    if ((counter <= 0) || (counter > size)) {
        fprintf (stderr, "ERROR: writeline: wrote %lu chars\n",counter);
        exit(-1);
    }
    int len = write(fd, "\n", 1);
    if ((len <= 0)) {
        fprintf (stderr, "ERROR: writeline: wrote %d chars\n",len);
        exit(-1);
    }

    close (fd);
    
    return counter;
}

int main(int argc, char *argv[])
{
    char filename[MAX_FILE_NAME];
    char strline[MAX_LINE_SIZE];
    int end_flag = 0;
    
    // init mutex
    pthread_mutex_init(&mutex, NULL);
    
    
    // get filename from params
    if (argc != 2) {
        fprintf (stderr, "help: %s fifo_file\n",argv[1]);
        exit (-1);
    }
    strcpy (filename, argv[1]);
    
    while (!end_flag) {
        // read a line to get the command
        int numread = readline(filename, strline, MAX_LINE_SIZE);
        if (numread < 0) {
            fprintf (stderr, "ERROR: read line bigger than %d\n",MAX_LINE_SIZE);
            exit (-1);
        }
        
        // switch upon  the command
        if (strcmp(strline,CMD_START) == 0) {
            fprintf (stderr, "INFO: Command received is START\n");
                        
            // send init command
            int ret = command_rpc_init();
            if (ret < 0) {
                fprintf (stderr, "ERROR: command_rpc_init\n");
                exit (-1);
            }
        } else if (strcmp(strline,CMD_FINISH) == 0) {
            fprintf (stderr, "INFO: Command received is FINISH\n");

            // send fini command
            int ret = command_rpc_icc_fini();
            if (ret < 0) {
                fprintf (stderr, "ERROR: command_rpc_fini\n");
                exit (-1);
            }

            // end loop
            end_flag = 1;
            
        } else if (strcmp(strline,CMD_ENTER_MALEAB) == 0) {
            fprintf (stderr, "INFO: Command received is ENTER MALLEABLE REGION\n");
            int nodes_hint = 0;

            // read a line to get the params
            int numread = readline(filename, strline, MAX_LINE_SIZE);
            if (numread < 0) {
                fprintf (stderr, "ERROR: read line bigger than %d\n",MAX_LINE_SIZE);
                exit (-1);
            }
            int num_matchs = sscanf(strline, "%d", &nodes_hint);
            if (num_matchs < 0) {
                fprintf (stderr, "ERROR: Param is not an integer (%s)\n",strline);
                exit (-1);
            }
            fprintf (stderr, "INFO: Command Parameter nodes_hint = %d\n",nodes_hint);

            // send enter region command
            int ret = command_rpc_malleability_enter_region(nodes_hint, 1);
            if (ret < 0) {
                fprintf (stderr, "ERROR: command_rpc_malleability_enter_region\n");
                exit (-1);
            }
        } else if (strcmp(strline,CMD_LEAVE_MALEAB) == 0) {
            fprintf (stderr, "INFO: Command received is LEAVE MALLEABLE REGION\n");
            
            // send leave region command
            int ret = command_rpc_malleability_leave_region();
            if (ret < 0) {
                fprintf (stderr, "ERROR: command_rpc_malleability_leave_region\n");
                exit (-1);
            }
            char *hostlist = NULL;
            pthread_mutex_lock(&mutex);
            ret = get_hostlist_command(&new_nodes_list, &hostlist);
            pthread_mutex_unlock(&mutex);
            if (ret < 0) {
                fprintf (stderr, "ERROR: get_hostlist_command\n");
                exit (-1);
            }
            fprintf (stderr, "INFO: Hotlist received: %s\n", hostlist);
            ret = writeline(filename, hostlist, strlen(hostlist)+1);
            if (ret < 0) {
                fprintf (stderr, "ERROR: writeline\n");
                exit (-1);
            }

        } else if (strcmp(strline,CMD_REMOVE) == 0) {
            fprintf (stderr, "INFO: Command received is REMOVE\n");
            
            // read a line to get the params
            int numread = readline(filename, strline, MAX_LINE_SIZE);
            if (numread < 0) {
                fprintf (stderr, "ERROR: read line bigger than %d\n",MAX_LINE_SIZE);
                exit (-1);
            }
            fprintf (stderr, "INFO: Command Parameter host = %s\n",strline);
           
            // tag removed host
            pthread_mutex_lock(&mutex);
            int removed = tag_removed_cpu(&removed_nodes_list, strline);
            pthread_mutex_unlock(&mutex);
            if (removed < 0) {
                fprintf (stderr, "ERROR: tag_removed_cpu\n");
                exit (-1);
            } else if (removed == 0) {
                
                // deregister removed nodes (they will be removed in next epoch)
                pthread_mutex_lock(&mutex);
                int ret =  deregister_removed_cpus(&removed_nodes_list);
                pthread_mutex_unlock(&mutex);
                if (ret < 0) {
                    fprintf (stderr, "ERROR: deregister_removed_cpus\n");
                    exit (-1);
                }
                
                // removed nodes deregistered in previous epoch
                ret = command_rpc_release_nodes();
                if (ret < 0) {
                    fprintf(stderr, "ERROR: command_rpc_release_nodes\n");
                    exit (-1);
                }
            }
        } else if (strcmp(strline,CMD_GET_IP) == 0) {
            fprintf (stderr, "INFO: Command received is GET IP\n");
            if (addr_ic_str != NULL) {
                int ret = writeline(filename, addr_ic_str, strlen(addr_ic_str)+1);
                if (ret < 0) {
                    fprintf (stderr, "ERROR: writeline\n");
                    exit (-1);
                }
            } else {
                int ret = writeline(filename, "0.0.0.0", 8);
                if (ret < 0) {
                    fprintf (stderr, "ERROR: writeline\n");
                    exit (-1);
                }

            }
        } else {
            fprintf (stderr, "ERROR: command %s not found\n", strline);
            
        }
    }
    
    // destroy mutex
    pthread_mutex_destroy(&mutex);

    return 0;
}
