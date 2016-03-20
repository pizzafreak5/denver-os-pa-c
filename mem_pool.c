/*
 * Created by Ivo Georgiev on 2/9/16.
 */

#include <stdlib.h>
#include <assert.h>
#include <stdio.h> // for perror()

#include "mem_pool.h"

/*************/
/*           */
/* Constants */
/*           */
/*************/
static const float      MEM_FILL_FACTOR                 = 0.75;
static const unsigned   MEM_EXPAND_FACTOR               = 2;

static const unsigned   MEM_POOL_STORE_INIT_CAPACITY    = 20;
static const float      MEM_POOL_STORE_FILL_FACTOR      = 0.75;	//same as MEM_FILL_FACTOR	*I hard coded these constants because gcc was refusing to compile them before.
static const unsigned   MEM_POOL_STORE_EXPAND_FACTOR    = 2;	//same as MEM_EXPAND_FACTOR

static const unsigned   MEM_NODE_HEAP_INIT_CAPACITY     = 40;
static const float      MEM_NODE_HEAP_FILL_FACTOR       = 0.75;	//same as MEM_FILL_FACTOR
static const unsigned   MEM_NODE_HEAP_EXPAND_FACTOR     = 2;	//same as MEM_EXPAND_FACTOR

static const unsigned   MEM_GAP_IX_INIT_CAPACITY        = 40;
static const float      MEM_GAP_IX_FILL_FACTOR          = 0.75;	//same as MEM_FILL_FACTOR
static const unsigned   MEM_GAP_IX_EXPAND_FACTOR        = 2;	//same as MEM_EXPAND_FACTOR



/*********************/
/*                   */
/* Type declarations */
/*                   */
/*********************/
typedef struct _node {
    alloc_t alloc_record;
    unsigned used;
    unsigned allocated;
    struct _node *next, *prev; // doubly-linked list for gap deletion
} node_t, *node_pt;

typedef struct _gap {
    size_t size;
    node_pt node;
} gap_t, *gap_pt;

typedef struct _pool_mgr {
    pool_t pool;
    node_pt node_heap;
    unsigned total_nodes;
    unsigned used_nodes;
    gap_pt gap_ix;
    unsigned gap_ix_capacity;
} pool_mgr_t, *pool_mgr_pt;



/***************************/
/*                         */
/* Static global variables */
/*                         */
/***************************/
static pool_mgr_pt *pool_store = NULL; // an array of pointers, only expand
static unsigned pool_store_size = 0;
static unsigned pool_store_capacity = 0;



/********************************************/
/*                                          */
/* Forward declarations of static functions */
/*                                          */
/********************************************/
static alloc_status _mem_resize_pool_store();
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr);
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr);
static alloc_status
        _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
                           size_t size,
                           node_pt node);
static alloc_status
        _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
                                size_t size,
                                node_pt node);
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr);



/****************************************/
/*                                      */
/* Definitions of user-facing functions */
/*                                      */
/****************************************/
//Chris
alloc_status mem_init() {
    if (pool_store != NULL) {return ALLOC_CALLED_AGAIN;}	// ensure that it's called only once until mem_free
    pool_store = (pool_mgr_pt*)malloc(MEM_POOL_STORE_INIT_CAPACITY * sizeof(pool_mgr_pt));	// allocate the pool store with initial capacity
	if (pool_store == NULL) {return ALLOC_FAIL;}	//ensure pool store allocation succeded
    // note: holds pointers only, other functions to allocate/deallocate

    return ALLOC_OK;
}
//Garrett
alloc_status mem_free() {
	if (pool_store == NULL) {return ALLOC_CALLED_AGAIN;} // ensure that it's called only once for each mem_init
	int i = 0;
	for (i = 0; i < pool_store_size; i++) { // make sure all pool managers have been deallocated
		if (pool_store[i] != NULL) {return ALLOC_NOT_FREED;}
	}
	// can free the pool store array
	// update static variables

	if(pool_store != NULL){ //Check if pool store has been allocated If so, free pool store
		free(pool_store); //deallocate the pool store
		pool_store = NULL;
		pool_store_capacity = 0; //set pool_store_capacity and pool_store_size to 0
		pool_store_size = 0;
		return ALLOC_OK;
	}
	return ALLOC_FAIL;
}
//Chris
pool_pt mem_pool_open(size_t size, alloc_policy policy) {
    assert(pool_store != NULL); // make sure there the pool store is allocated
    alloc_status status = _mem_resize_pool_store();	// expand the pool store, if necessary
	if (status == ALLOC_FAIL) {return NULL;}

    pool_mgr_pt newMgr = (pool_mgr_pt)malloc(sizeof(pool_mgr_t));	// allocate a new mem pool mgr
	newMgr->pool.mem = (char*)malloc(size);	// allocate a new memory pool
    newMgr->node_heap = (node_pt)malloc(MEM_NODE_HEAP_INIT_CAPACITY * sizeof(node_t));	// allocate a new node heap
    newMgr->gap_ix = (gap_pt)malloc(MEM_GAP_IX_INIT_CAPACITY * sizeof(gap_t));	// allocate a new gap index

    if (newMgr == NULL || newMgr->pool.mem == NULL || newMgr->node_heap == NULL || newMgr->gap_ix == NULL) {	// check success, on error deallocate mgr/pool/heap/gap  and return null
		free(newMgr->pool.mem);
		free(newMgr->node_heap);
		free(newMgr->gap_ix);
		free(newMgr);
		return NULL;
	}

    // assign all the pointers and update meta data:
	newMgr->pool.policy = policy;
	newMgr->pool.total_size = size;
	newMgr->pool.num_gaps = 1;
	newMgr->node_heap[0].alloc_record.size = size;	//   initialize top node of node heap
	newMgr->node_heap[0].alloc_record.mem = newMgr->pool.mem;
	newMgr->node_heap[0].used = 1;
	newMgr->node_heap[0].allocated = 0;
	newMgr->node_heap[0].next = NULL;
	newMgr->node_heap[0].prev = NULL;
    newMgr->gap_ix[0].node = newMgr->node_heap;	//   initialize top node of gap index
	newMgr->gap_ix[0].size = size;
    newMgr->total_nodes = MEM_NODE_HEAP_INIT_CAPACITY;	//   initialize pool mgr
    newMgr->used_nodes = 1;
    newMgr->gap_ix_capacity = MEM_GAP_IX_INIT_CAPACITY;

    pool_store[pool_store_size] = newMgr;//   link pool mgr to pool store
	pool_store_size++;

    return (pool_pt)newMgr;	// return the address of the mgr, cast to (pool_pt)
}
//Garrett
alloc_status mem_pool_close(pool_pt pool) {
	// note: don't decrement pool_store_size, because it only grows
	// free mgr

	pool_mgr_pt manager = (pool_mgr_pt) pool; // get mgr from pool by casting the pointer to (pool_mgr_pt)

	if (pool->mem == NULL){ // check if this pool is allocated
		return ALLOC_NOT_FREED;
	}

	if (!(manager->used_nodes == 1 && manager->node_heap[0].allocated == 0)) { // check if one gap; zero allocations
		return ALLOC_NOT_FREED;
	}

	free(pool->mem); // free memory pool
	free(manager->node_heap); // free node heap
	free(manager->gap_ix); // free gap index

	int i = 0;
	for (i = 0; i < pool_store_size; i++){ // find mgr in pool store and set to null

		if (manager == pool_store[i]){
			pool_store[i] = NULL;
		}

	}

	//dealloc manager

	free(manager);

	return ALLOC_OK;
}
//G
alloc_pt mem_new_alloc(pool_pt pool, size_t size) {
	pool_mgr_pt mgr = (pool_mgr_pt)pool;				// get mgr from pool by casting the pointer to (pool_mgr_pt)
	if (pool->num_gaps == 0) {return NULL;}				// check if any gaps, return null if none
	assert(_mem_resize_node_heap(mgr) != ALLOC_FAIL);	// expand heap node, if necessary, quit on error
	assert(mgr->used_nodes < mgr->total_nodes);			// check used nodes fewer than total nodes, quit on error

	// get a node for allocation:
	node_pt newAllocation = NULL;
	int length = 1;

	// if FIRST_FIT, then find the first sufficient node in the node heap
	if (pool->policy == FIRST_FIT)
	{
		newAllocation = mgr->node_heap;											//Start at the top of the heap
		int found = 0;															//Variable used to exit in the case of finding an unused node
		while (newAllocation != NULL && (found == 0))						//Try to find an empty node in the heap that isn't being used
		{
			length = length + 1;
			if (newAllocation->allocated == 0 && newAllocation->used == 1 && newAllocation->alloc_record.size >= size)	//If the node is not being used, select it
			{
				found = 1;
			} else {
				newAllocation = newAllocation->next;							//This will eventually lead to the while loop breaking because the next element will be null
			}
		}
		if (found == 0)															//The 1st node is allocated, if
		{
			return NULL;
		}
	}
	// if BEST_FIT, then find the first sufficient node in the gap index
	if (pool->policy == BEST_FIT)
	{
		for (int i = 0; i < mgr->pool.num_gaps; i++)
		{
			if (mgr->gap_ix[i].size >= size)
			{
				newAllocation = (&mgr->gap_ix[i])->node;
			}
		}
	}

	// check if node found
	if (newAllocation == NULL)			//
	{
		return NULL;
	}

	// update metadata (num_allocs, alloc_size)
	if (length == 0)	//If it is the first memory allocation, elsewise length > 1
	{
		pool->num_allocs = 1;
		pool->alloc_size = size;
	} else {
		pool->num_allocs = pool->num_allocs + 1;
		pool->alloc_size = pool->alloc_size + size;
	}

	//Check to see if it is a perfect fit
	if (newAllocation->alloc_record.size == size)
	{
		//Change the size of the gap
		newAllocation->allocated = 1;
		_mem_remove_from_gap_ix(mgr, size, newAllocation);
	}
	else	//In the case it isn't an exact allocation
	{
		int i = 0;
		while (mgr->node_heap[i].used == 1) {i++;}
		node_pt newInsertNode = &mgr->node_heap[i];	//The new gap

		newInsertNode->next = newAllocation->next;
		newInsertNode->prev = newAllocation;
		if (newAllocation->next != NULL) {newAllocation->next->prev = newInsertNode;}
		newAllocation->next = newInsertNode;

		_mem_remove_from_gap_ix(mgr, newAllocation->alloc_record.size, newAllocation);

		newInsertNode->allocated = 0;													//It is going to be allocated to a memory
		newInsertNode->used = 1;														//It is not going to be used yet
		newInsertNode->alloc_record.size = newAllocation->alloc_record.size - size;		//The size should be the remaining gap while still satisfying the function call
		newInsertNode->alloc_record.mem = newAllocation->alloc_record.mem + size;

		newAllocation->alloc_record.size = size;
		newAllocation->allocated = 1;

		_mem_add_to_gap_ix(mgr, newInsertNode->alloc_record.size, newInsertNode);

		mgr->used_nodes = mgr->used_nodes + 1; //update metadata (used_nodes)
	}

	//Remove the gap used

	//_mem_sort_gap_ix(mgr);

	//AT THIS POINT:
	//newAllocation, the new allocation given to the user is removed from the gap index
	//newInsertNode, the new gap created by allocating for newAllocation is in the gap index and is pointing to its domain of memory with the correct size of it as well

	// return allocation record by casting the node to (alloc_pt)
	return &(newAllocation->alloc_record);
}

//Garrett
alloc_status mem_del_alloc(pool_pt pool, alloc_pt alloc) {
	// get mgr from pool by casting the pointer to (pool_mgr_pt)
	pool_mgr_pt manager = (pool_mgr_pt) pool;
	// get node from alloc by casting the pointer to (node_pt)
	// find the node in the node heap

	node_pt cursor = (node_pt) alloc;
	if (cursor->allocated == 0) {return ALLOC_NOT_FREED;}
	// this is node-to-delete
	cursor->allocated = 0;

	// update metadata (num_allocs, alloc_size)
	pool->num_allocs--;
	pool->alloc_size = pool->alloc_size - alloc->size;

	// if the next node in the list is also a gap, merge into node-to-delete
	//   remove the next node from gap index
	//   check success
	//   add the size to the node-to-delete
	//   update node as unused
	//   update metadata (used nodes)
	//   update linked list:
	/*
                    if (next->next) {
                        next->next->prev = node_to_del;
                        node_to_del->next = next->next;
                    } else {
                        node_to_del->next = NULL;
                    }
                    next->next = NULL;
                    next->prev = NULL;
     */

	node_pt next = cursor->next;
	if ((next != NULL) && (next->allocated == 0)){
		//check success
		if (_mem_remove_from_gap_ix(manager, next->alloc_record.size, next) == ALLOC_FAIL){
			return ALLOC_FAIL;
		}

		cursor->alloc_record.size = next->alloc_record.size + cursor->alloc_record.size;

		if (cursor->next != NULL) {cursor->next->used = 0;}

		manager->used_nodes--;

		if (next->next != NULL){
			next->next->prev = cursor;
			cursor->next = next->next;
		}
		else {
			cursor->next = NULL;
		}
		next->next = NULL;
		next->prev = NULL;
	}

	// this merged node-to-delete might need to be added to the gap index
	// but one more thing to check...
	// if the previous node in the list is also a gap, merge into previous!
	//   remove the previous node from gap index <- I think this should be rmving the node to delete
	//   check success
	//   add the size of node-to-delete to the previous
	//   update node-to-delete as unused
	//   update metadata (used_nodes)
	//   update linked list
	/*
                    if (node_to_del->next) {
                        prev->next = node_to_del->next;
                        node_to_del->next->prev = prev;
                    } else {
                        prev->next = NULL;
                    }
                    node_to_del->next = NULL;
                    node_to_del->prev = NULL;
     */
	//   change the node to add to the previous node!
	// add the resulting node to the gap index
	// check success
	node_pt prev = cursor->prev;
	if ((prev != NULL) && (prev->allocated == 0)){
		if (_mem_remove_from_gap_ix(manager, prev->alloc_record.size, prev) == ALLOC_FAIL){
			return ALLOC_FAIL;
		}

		//check success

		prev->alloc_record.size = prev->alloc_record.size + cursor->alloc_record.size;
		cursor->used = 0;

		manager->used_nodes--;

		if (cursor->next != NULL){
			prev->next = cursor->next;
			cursor->next->prev = prev;
		}
		else {
			prev->next = NULL;
		}
		cursor->next = NULL;
		cursor->prev = NULL;

		return _mem_add_to_gap_ix(manager, prev->alloc_record.size, prev);
	}

	return _mem_add_to_gap_ix(manager, cursor->alloc_record.size, cursor);
}
//Chris
void mem_inspect_pool(pool_pt pool,
                      pool_segment_pt *segments,
                      unsigned *num_segments) {
    pool_mgr_pt pool_mgr = (pool_mgr_pt)pool;	// get the mgr from the pool
    pool_segment_pt segs = (pool_segment_pt)malloc(pool_mgr->used_nodes * sizeof(pool_segment_t));	// allocate the segments array with size == used_nodes
    assert(segs != NULL);	// check successful

	node_t node = pool_mgr->node_heap[0];
    for (size_t i = 0; i < pool_mgr->used_nodes; i++)	{	// loop through the node heap and the segments array
		segs[i].size = node.alloc_record.size;	// for each node, write the size and allocated in the segment
		segs[i].allocated = node.allocated;
		if (node.next != NULL) {
			node = *node.next;
		}
	}
   	// "return" the values:

    *segments = segs;
    *num_segments = pool_mgr->used_nodes;
}



/***********************************/
/*                                 */
/* Definitions of static functions */
/*                                 */
/***********************************/
//Chris
static alloc_status _mem_resize_pool_store() {
    // check if necessary
	if (((float) pool_store_size / pool_store_capacity) > MEM_POOL_STORE_FILL_FACTOR) {
		size_t newSize = MEM_POOL_STORE_EXPAND_FACTOR * pool_store_capacity;
		pool_mgr_pt *newStore = realloc(pool_store, newSize * sizeof(pool_mgr_pt));	// update capacity variables
		if (newStore == NULL) {	//check if realloc failed
			return ALLOC_FAIL;
		}
		pool_store = newStore;
		pool_store_capacity = newSize;
		return ALLOC_OK;
	}

    return ALLOC_OK;
}
//Chris
static alloc_status _mem_resize_node_heap(pool_mgr_pt pool_mgr) {
    if (((float) pool_mgr->used_nodes / pool_mgr->total_nodes) > MEM_NODE_HEAP_FILL_FACTOR) {
		size_t newSize = MEM_NODE_HEAP_EXPAND_FACTOR * pool_mgr->total_nodes;
		node_pt newHeap = realloc(pool_mgr->node_heap, newSize * sizeof(node_t));
		if (newHeap == NULL) {	//check if realloc failed
			return ALLOC_FAIL;
		}
		pool_mgr->node_heap = newHeap;
		pool_mgr->total_nodes = newSize;
		return ALLOC_OK;
	}

    return ALLOC_OK;
}
//Chris
static alloc_status _mem_resize_gap_ix(pool_mgr_pt pool_mgr) {
	if (((float) pool_mgr->pool.num_gaps / pool_mgr->gap_ix_capacity) > MEM_GAP_IX_FILL_FACTOR) {
		size_t newSize = MEM_GAP_IX_EXPAND_FACTOR * pool_mgr->gap_ix_capacity;
		gap_pt newGapIx = realloc(pool_mgr->gap_ix, newSize * sizeof(gap_t));
		if (newGapIx == NULL) {	//check if realloc failed
			return ALLOC_FAIL;
		}
		pool_mgr->gap_ix = newGapIx;
		pool_mgr->gap_ix_capacity = newSize;
		return ALLOC_OK;
	}

    return ALLOC_OK;
}
//G
static alloc_status _mem_add_to_gap_ix(pool_mgr_pt pool_mgr,
									   size_t size,
									   node_pt node) {
	// expand the gap index, if necessary (call the function)
	_mem_resize_gap_ix(pool_mgr);

	// add the entry at the end
	pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = node;
	pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = size;
	// update metadata (num_gaps)
	pool_mgr->pool.num_gaps++;
	// sort the gap index (call the function)
	// check success
	if (_mem_sort_gap_ix(pool_mgr) == ALLOC_FAIL){
		//the world hs ended
		return ALLOC_FAIL;
	}

	return ALLOC_OK;
}
//G
static alloc_status _mem_remove_from_gap_ix(pool_mgr_pt pool_mgr,
											size_t size,
											node_pt node) {
	size_t pos;
	for (pos = 0; pos < pool_mgr->pool.num_gaps; pos++)	{ // find the position of the node in the gap index
		if (pool_mgr->gap_ix[pos].node == node) {break;}
	}

	for (pos; pos < pool_mgr->pool.num_gaps; pos++) {// loop from there to the end of the array:
		pool_mgr->gap_ix[pos] = pool_mgr->gap_ix[pos+1];	//    pull the entries (i.e. copy over) one position up
	}
	pool_mgr->pool.num_gaps = pool_mgr->pool.num_gaps - 1;	// update metadata (num_gaps)
	pool_mgr->gap_ix[pool_mgr->pool.num_gaps].size = 0;// zero out the element at position num_gaps!
	pool_mgr->gap_ix[pool_mgr->pool.num_gaps].node = NULL;

	return ALLOC_OK;
}
//G
// note: only called by _mem_add_to_gap_ix, which appends a single entry
static alloc_status _mem_sort_gap_ix(pool_mgr_pt pool_mgr) {
	// the new entry is at the end, so "bubble it up"
	// loop from num_gaps - 1 until but not including 0:
	//    if the size of the current entry is less than the previous (u - 1)
	//       swap them (by copying) (remember to use a temporary variable)
	int length = pool_mgr->pool.num_gaps;

	//In the case that there is only 1 node
	if (length < 2)
	{
		return ALLOC_OK;
	}
	for (int currentIndex = length; currentIndex >= 1;currentIndex--) {
		//Swap condition
		if (pool_mgr->gap_ix[currentIndex-1].size < pool_mgr->gap_ix[currentIndex].size
				|| (pool_mgr->gap_ix[currentIndex-1].size == pool_mgr->gap_ix[currentIndex].size
				&& pool_mgr->gap_ix[currentIndex-1].node->alloc_record.mem < pool_mgr->gap_ix[currentIndex].node->alloc_record.mem))
		{
			gap_t temp = pool_mgr->gap_ix[currentIndex];
			pool_mgr->gap_ix[currentIndex] = pool_mgr->gap_ix[currentIndex-1];
			pool_mgr->gap_ix[currentIndex-1] = temp;
		}
	}
	return ALLOC_OK;
}


