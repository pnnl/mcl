#include "minos_sched.h"

#include <pthread.h>
#include <inttypes.h>
#include <utlist.h>

#include <atomics.h>

typedef struct lru_list_node
{
    enode_t* parent;
    struct lru_list_node* next;
    struct lru_list_node* prev;

    struct lru_list_node* dev_next;
    struct lru_list_node* dev_prev;
} lru_data_t;

pthread_mutex_t    lru_lock;
lru_data_t*         lru_list;
lru_data_t**        dev_lru_list;

void lru_init(mcl_resource_t* res, int ndev) {
    dev_lru_list = malloc(sizeof(lru_data_t*) * ndev);
    memset(dev_lru_list, 0, sizeof(lru_data_t*) * ndev);
    lru_list = NULL;
    pthread_mutex_init(&lru_lock, NULL);
    return;
}

void lru_init_node(enode_t* e) {
    e->pol_data = (void*)malloc(sizeof(lru_data_t));
    memset(e->pol_data, 0, sizeof(lru_data_t));
    ((lru_data_t*)e->pol_data)->parent = e;
}

enode_t* lru_evict(void) {
    pthread_mutex_lock(&lru_lock);
    lru_data_t* el;
    int success = 0;
    DL_FOREACH(lru_list, el) {
        if(el->parent->refs)
            continue;
        success = 1;
        break;
    }

    if(!success) {
        pthread_mutex_unlock(&lru_lock);
        eprintf("Could not find memory not in use");
        return NULL;
    }
    enode_t* ret = el->parent;

    Dprintf("Trying to delete memid %"PRIu64" from dev %d lru list", ret->mem_data->mem_id, ret->dev);
    DL_DELETE2((dev_lru_list[ret->dev]), el, dev_prev, dev_next);
    DL_DELETE(lru_list, el);

    el->next = NULL;
    el->prev = NULL;
    el->dev_next = NULL;
    el->dev_prev = NULL;
    
    pthread_mutex_unlock(&lru_lock);
    return ret;
}

enode_t* lru_evict_from_dev(int dev) {
    pthread_mutex_lock(&lru_lock);
    lru_data_t* el;
    int success = 0;
    DL_FOREACH((dev_lru_list[dev]), el) {
        if(el->parent->refs > 0)
            continue;
        success = 1;
        break;
    }
    if(!success) {
        pthread_mutex_unlock(&lru_lock);
        eprintf("Could not find memory not in use");
        return NULL;
    }

    enode_t* ret = el->parent;

    DL_DELETE(lru_list, el);
    DL_DELETE2((dev_lru_list[dev]), el, dev_prev, dev_next);
    
    el->next = NULL;
    el->prev = NULL;
    el->dev_next = NULL;
    el->dev_prev = NULL;

    pthread_mutex_unlock(&lru_lock);
    return ret;
}

int lru_used(enode_t* e) {
    pthread_mutex_lock(&lru_lock);
    lru_data_t* el = (lru_data_t*)e->pol_data;
    

    if(el->next || el->prev){
        DL_DELETE(lru_list, el);
        DL_DELETE2((dev_lru_list[e->dev]), el, dev_prev, dev_next);
    }

    DL_APPEND(lru_list, el);
    DL_APPEND2((dev_lru_list[e->dev]), el, dev_prev, dev_next);
    Dprintf("Added memid %"PRIu64" to dev %d lru list at %p", e->mem_data->mem_id, e->dev, dev_lru_list[e->dev]);

    ainc(&(e->refs));
    pthread_mutex_unlock(&lru_lock);
    return 0;
}

int lru_released(enode_t* e) {
    adec(&(e->refs));
    return 0;
}

int lru_removed(enode_t* e) {
    pthread_mutex_lock(&lru_lock);
    lru_data_t* el = (lru_data_t*)e->pol_data;
    if(el->next || el->prev){
        DL_DELETE(lru_list, el);
        DL_DELETE2((dev_lru_list[e->dev]), el, dev_prev, dev_next);
    }
    pthread_mutex_unlock(&lru_lock);
    return 0;
}

void lru_destroy(void) {
}

const struct sched_eviction_policy lru_eviction_policy = {
    .init = lru_init,
    .init_node = lru_init_node,
    .used = lru_used,
    .released = lru_released,
    .removed = lru_removed,
    .destroy = lru_destroy,
    .evict = lru_evict,
    .evict_from_dev = lru_evict_from_dev
};