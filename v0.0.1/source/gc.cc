#include "gc.h"

#ifdef _WIN32
#include <Windows.h>
#endif

Object::Object(void* pointer) {
    this->pointer = pointer;
    this->marking = 0;
    this->children = std::vector<Object>();
}

bool Object::marked() {
    return (bool)marking;
}

void Object::mark() {
    marking = 1;

    for(Object child : this->children) {
        child.mark();
    }
}

void Object::unmark() {
    marking = 0;
    
    for(Object child : this->children) {
        child.unmark();
    }
}

void* Object::getPointer() {
    return pointer;
}

void Object::setPointer(void* pointer) {
    this->pointer = pointer;
}

uintptr_t Object::getID() {
    return (uintptr_t)pointer;
}

std::vector<Object>* Object::getChildren() {
    return &children;
}

void Object::destroy() {
    free(this->pointer);

    for(Object child : this->children) {
        child.destroy();
    }
}

GarbageCollector::GarbageCollector(void** stack_top) {
    this->allocations = std::vector<Object>();
    this->stack_top = stack_top;
}

#include <stdio.h>

void* GarbageCollector::alloc(size_t size) {
    void* objptr = calloc(1, size);

    while(!objptr) {
        printf("Allocation error, could not allocate %zu bytes\n", size);
        markStack();
        sweep();
        objptr = malloc(size);
    }
    
    Object newObject = Object(objptr);
    allocations.push_back(newObject);

    return objptr;
}

void *stack_bottom() {
  int stack_bptr;
  return &stack_bptr;
}

void *get_stack_bottom() {
  void *(*volatile sb)(void) = stack_bottom;
  return sb();
}

void GarbageCollector::addObject(void* pointer) {
    allocations.push_back(Object(pointer));
}

void GarbageCollector::removeObject(Object object) {
    free(object.getPointer());

    for(int i = 0; i < allocations.size(); i++) {
        if(allocations[i].getID() == object.getID()) {
            allocations.erase(allocations.begin()+i);
            break;
        }
    }

    for(Object child : *object.getChildren()) {
        removeObject(child);
    }
}

void GarbageCollector::markStack() {
    char* stack_bottom = (char*)get_stack_bottom();

    char* stack_it = (char*)stack_bottom;

    for(;stack_it < (char*)stack_top; stack_it++) {
        char** stack_it_cur = (char**)stack_it;
        
        for(Object obj : allocations) {
            if(obj.getPointer() == *stack_it_cur) {
                obj.mark();
            }
        }
    }
}

void GarbageCollector::sweep() {
    char* stack_bottom = (char*)get_stack_bottom();

    char* stack_it = (char*)stack_bottom;

    for(;stack_it < (char*)stack_top; stack_it++) {
        char** stack_it_cur = (char**)stack_it;
        
        for(Object obj : allocations) {
            if(obj.getPointer() == *stack_it_cur) {
                if(!obj.marked()) {
                    removeObject(obj);
                } else {
                    obj.unmark();
                }
            }
        }
    }
}

void GarbageCollector::clean() {
    for(Object obj : allocations) {
        free(obj.getPointer());
    }
}

static GarbageCollector gc = GarbageCollector((void**)0);

extern "C" {
void start_gc(void* stack_start) {
    gc = GarbageCollector((void**)stack_start);
}

void end_gc() {
    gc.clean();
}

void* gc_alloc(int size) {
    return gc.alloc(size);
}

// void _gc_mark() {
//     GarbageCollector* gcp = (GarbageCollector*)gc;
//     gcp->markStack();
// }

// void _gc_add_object(void* pointer) {
//     GarbageCollector* gcp = (GarbageCollector*)gc;
//     gcp->addObject(pointer);
// }
}