#include <vector>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

class Object {
private:
    void* pointer;
    int marking;
    std::vector<Object> children;

public:
    Object(void* pointer);

    bool marked();
    void mark();
    void unmark();

    void* getPointer();
    void setPointer(void* pointer);

    uintptr_t getID();

    std::vector<Object>* getChildren();

    void destroy();
};

class GarbageCollector {
private:
    std::vector<Object> allocations;
    void** stack_top;

public:
    GarbageCollector(void** stack_top);

    void addObject(void* pointer);

    void* alloc(size_t size);
    
    void removeObject(Object object);

    void markStack();
    void sweep();
    void clean();
};