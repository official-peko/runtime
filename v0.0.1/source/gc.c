#if defined(__unix__) || defined(__linux__)
#define _GNU_SOURCE
#define __USE_GNU
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>

#if defined(__APPLE__)
#include <unistd.h>
#endif

#if defined(__APPLE__) || defined(__unix__) || defined(__linux__)
#include <pthread.h>
#else
#include <windows.h>
#endif


void current_stack_info(void** start, void** end) {
#if defined(__unix__) || defined(__linux__)
    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    size_t stack_size;
    pthread_attr_getstack(&attr, start, &stack_size);
    *end = (*start)+stack_size;
#elif defined(__APPLE__)
    
    *start = pthread_get_stackaddr_np(pthread_self());
    *end = (*start)+pthread_get_stacksize_np(pthread_self());

#endif
#ifdef _WIN32
    ULONG_PTR lowLimit;
    ULONG_PTR highLimit;

    GetCurrentThreadStackLimits(&lowLimit, &highLimit);
    *start = (void*)lowLimit;
    *end = (void*)highLimit;
#endif
}


// Helps create object trees for easier search
typedef struct ObjectHeader {
    int child_count;
    int* child_offsets;
} ObjectHeader;

// Represents an allocated object, stores important tracking info on pointer
typedef struct DataAllocation {
    void* address;
    bool isobject;
    bool marked;
} DataAllocation;

typedef struct ThreadStack {
    void** stack_start;
    void** stack_end;
} ThreadStack;

// Main garbage collector entity
typedef struct PekoGarbageCollector {
    // allows garbage collector to run on multiple threads
    atomic_flag lock_flag;

    // every allocated pointer
    int allocation_count; 
    DataAllocation* allocations;
    
    // used for marking the stack
    int stack_count;
    ThreadStack* thread_stacks;

    // used for marking from global roots that may not be on stack
    int root_count;
    void*** global_roots;
} PekoGarbageCollector;

// locks the gc as a mutex
void gc_lock(PekoGarbageCollector* gc) {
    while (atomic_flag_test_and_set(&(gc->lock_flag))) {}
}

// unlocks the gc as a mutex
void gc_unlock(PekoGarbageCollector* gc) {
    atomic_flag_clear(&(gc->lock_flag));
}

// Initiates a new garbage collector
PekoGarbageCollector* create_gc(void* stack_start) {
    PekoGarbageCollector* gc = (PekoGarbageCollector*)malloc(sizeof(gc));
    
    atomic_flag default_flag = ATOMIC_FLAG_INIT;
    gc->lock_flag = default_flag;
    
    atomic_flag_clear(&(gc->lock_flag));
    
    gc->allocation_count = 0;
    gc->allocations = NULL;
    
    gc->stack_count = 1;
    gc->thread_stacks = (ThreadStack*)malloc(sizeof(ThreadStack));
    gc->thread_stacks[0].stack_start = (void**)stack_start;
    gc->thread_stacks[0].stack_end = (void**)stack_start;
    
    gc->root_count = 0;
    gc->global_roots = NULL;

    return gc;
}

// tracks the given stack info
int gc_track_stack(PekoGarbageCollector* gc, void* stack_start, void* stack_end) {
    gc_lock(gc);
    gc->stack_count += 1;
    ThreadStack* new_stacks = (ThreadStack*)malloc(sizeof(ThreadStack)*gc->stack_count);
    
    if(gc->root_count > 1) {
        memmove(new_stacks, gc->thread_stacks, sizeof(ThreadStack)*(gc->stack_count-1));
        free(gc->thread_stacks);
    }
    gc->thread_stacks = new_stacks;

    gc->thread_stacks[gc->stack_count-1].stack_start = stack_start;
    gc->thread_stacks[gc->stack_count-1].stack_end = stack_end;
    gc_unlock(gc);
    
    return gc->stack_count-1;
}

// untracks the stack at the given index
void gc_untrack_stack(PekoGarbageCollector* gc, int stack_idx) {
    gc_lock(gc);
    
    gc->stack_count -= 1;

    ThreadStack* new_stacks = (ThreadStack*)malloc(sizeof(ThreadStack)*gc->stack_count);
    memmove(new_stacks, gc->thread_stacks, sizeof(ThreadStack)*stack_idx);
    memmove((new_stacks + stack_idx), (gc->thread_stacks + (stack_idx+1)), sizeof(ThreadStack)*(gc->allocation_count-stack_idx));
    
    free(gc->thread_stacks);
    gc->thread_stacks = new_stacks;

    gc_unlock(gc);
}

// destroys all info related to an allocation
void destroy_allocation(DataAllocation allocation) {
    // if it is an object, free its header data
    if(allocation.isobject) {
        free(*(ObjectHeader**)(allocation.address+8));
    }

    // free the allocated pointer
    free(allocation.address);
}

// Destroys a garbage collector, freeing all of its allocations in the process
void destroy_gc(PekoGarbageCollector* gc) {
    // free all allocations
    if(gc->allocations) {
        for(int i = 0; i < gc->allocation_count; i++) {
            destroy_allocation(gc->allocations[i]);
        }

       free(gc->allocations);
       gc->allocations = NULL;
    }

    free(gc);
}

void gc_mark(PekoGarbageCollector* gc);
void gc_sweep(PekoGarbageCollector* gc);

// Allocates the specified amount of bytes, tracks it on the GC
void* gc_allocate(PekoGarbageCollector* gc, size_t bytes, bool for_object) {
    gc_lock(gc);
    
    // Add one DataAllocation point to the tracked list
    gc->allocation_count += 1;
    DataAllocation* new_allocations = (DataAllocation*)malloc(sizeof(DataAllocation)*gc->allocation_count);

    if(gc->allocation_count > 1) {
        memmove(new_allocations, gc->allocations, sizeof(DataAllocation)*(gc->allocation_count-1));
        free(gc->allocations);
    }
    
    gc->allocations = new_allocations;
    
    // Allocate the data and track its info
    void* allocation_pointer = malloc(bytes);
    if(allocation_pointer == NULL) {
        gc_mark(gc);
        gc_sweep(gc);
    }
    
    gc->allocations[gc->allocation_count-1].address = allocation_pointer;
    gc->allocations[gc->allocation_count-1].isobject = for_object;
    gc->allocations[gc->allocation_count-1].marked = false;

    gc_unlock(gc);

    return allocation_pointer;
}

// Marks an object if found in tracking list
int gc_mark_object(PekoGarbageCollector* gc, void* object) {
    gc_lock(gc);

    for(int i = 0; i < gc->allocation_count; i++) {
        if(gc->allocations[i].address == object) {
            if(gc->allocations[i].marked) return 1;

            // mark this object
            gc->allocations[i].marked = true;

            // search and mark any children of this object
            if(gc->allocations[i].isobject) {
                ObjectHeader* obj_header = *(ObjectHeader**)(object+8);

                for(int i = 0; i < obj_header->child_count; i++) {
                    gc_mark_object(gc, *(void**)(object+obj_header->child_offsets[i]));
                }
            }

            return 1;
        }
    }

    gc_unlock(gc);

    return 0;
}

// marking function of the garbage collector
void gc_mark(PekoGarbageCollector* gc) {
    gc_lock(gc);

    // searches through all stacks, marking applicable objects
    for(int i = 0; i < gc->stack_count; i++) {
        void** stack_start = gc->thread_stacks[i].stack_start;
        void** stack_end = gc->thread_stacks[i].stack_end;
        
        // searches through stack in proper order
        // different ABIs index stack differently
        if(stack_end < stack_start) {
            for(void* stack_current = (void*)stack_end; stack_current < (void*)stack_start; stack_current++) {
                // "mark" the current point, if not in system then nothing will happen
                gc_mark_object(gc, *(void**)stack_current);
            }
        } else {
            for(void* stack_current = (void*)stack_start; stack_current < (void*)stack_end; stack_current++) {
                // "mark" the current point, if not in system then nothing will happen
                gc_mark_object(gc, *(void**)stack_current);
            } 
        }
    }
    

    // now, search all roots and mark them
    for(int i = 0; i < gc->root_count; i++) {
        gc_mark_object(gc, *(gc->global_roots[i]));
    }

    gc_unlock(gc);
}

// Removes the item at that index
void gc_remove_allocation_item(PekoGarbageCollector* gc, int idx) {
    gc_lock(gc);
    
    gc->allocation_count -= 1;
    DataAllocation* new_allocations = (DataAllocation*)malloc(sizeof(DataAllocation)*gc->allocation_count );
    memmove(new_allocations, gc->allocations, sizeof(DataAllocation)*idx);
    memmove((new_allocations + idx), (gc->allocations + (idx+1)), sizeof(DataAllocation)*(gc->allocation_count-idx));
    
    free(gc->allocations);
    gc->allocations = new_allocations;

    gc_unlock(gc);
}

// Sweeps through the entire allocation list and removes any objects that are not marked
void gc_sweep(PekoGarbageCollector* gc) {
    gc_lock(gc);

    for(int i = 0; i < gc->allocation_count; i++) {
        if(!gc->allocations[i].marked) {
            destroy_allocation(gc->allocations[i]);
            gc_remove_allocation_item(gc, i);
            i--;
        }
    }

    gc_unlock(gc);
}

  // ---------------------- //
 // --- glue functions --- //
// ---------------------- //


static PekoGarbageCollector* global_gc = NULL;

void start_gc(void* stack_start) {
    if(global_gc) return;
    global_gc = create_gc(stack_start);
}

void stop_gc() {
    if(!global_gc) return;
    destroy_gc(global_gc);
    global_gc = NULL;
}

void* gc_alloc(int bytes) {
    if(!global_gc) return NULL;
    return gc_allocate(global_gc, (size_t)bytes, false);
}

void* gc_alloc_object(int bytes) {
    if(!global_gc) return NULL;
    return gc_allocate(global_gc, (size_t)bytes, true);
}

void* create_object_header(int offset_count, ...) {
    ObjectHeader* header = (ObjectHeader*)malloc(sizeof(ObjectHeader));
    header->child_count = offset_count;
    header->child_offsets = (int*)malloc(sizeof(int)*offset_count);

    va_list offsets;
    va_start(offsets, offset_count);

    for(int i = 0; i < offset_count; i++) {
        header->child_offsets[i] = va_arg(offsets, int);
    }
    va_end(offsets);

    return (void*)header;
}

void mark_global_root(void* root) {
    if(!global_gc) return;
    
    global_gc->root_count += 1;
    void*** new_global_roots = (void***)malloc(sizeof(void**)*global_gc->root_count);
    
    if(global_gc->root_count > 1) {
        memmove(new_global_roots, global_gc->global_roots, sizeof(void**)*(global_gc->root_count-1));
        free(global_gc->global_roots);
    }
    global_gc->global_roots = new_global_roots;

    global_gc->global_roots[global_gc->root_count-1] = (void**)root;
}

int gc_track_current_stack() {
    void *stack_start, *stack_end;
    current_stack_info(&stack_start, &stack_end);
    return gc_track_stack(global_gc, stack_start, stack_end);
}

void gc_remove_stack(int stack_idx) {
    gc_untrack_stack(global_gc, stack_idx);
}