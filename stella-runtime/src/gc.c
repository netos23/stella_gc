//
// Created by Nikita Morozov on 25.10.2025.
//
#include "stella/gc.h"


void* gc_alloc(size_t size_in_bytes) {
    return NULL;
}

void gc_read_barrier(void *object, int field_index){

}


void gc_write_barrier(void *object, int field_index, void *contents){

}

void gc_push_root(void **object) {

}

void gc_pop_root(void **object) {

}

void print_gc_alloc_stats(){

}

void print_gc_state() {

}

void print_gc_roots() {

}
