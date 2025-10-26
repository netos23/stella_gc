//
// Created by Nikita Morozov on 25.10.2025.
//

#include <string.h>
#include "stella/gc.h"


typedef struct Gc_root {
    struct Gc_root *next, *prev;
    void **content;
} gc_root;

typedef struct Gc_state {
    void *from_space, *to_space;
    size_t from_space_size, to_space_size;
    void *scan, *next, *limit;
    gc_root *roots_list;
    gc_root *roots_last;
    size_t roots_size;
    bool gc_running;
} gc_state;


static gc_state current_state = {
        .from_space = NULL,
        .to_space = NULL,
        .from_space_size = 0,
        .to_space_size = 0,
        .scan = NULL,
        .next = NULL,
        .limit = NULL,
        .roots_list = NULL,
        .roots_last = NULL,
        .roots_size = 0,
        .gc_running = false,
};


bool is_from_space(void *p) {
    void *from_space = current_state.from_space;
    return from_space <= p && p < from_space + current_state.from_space_size;
}

bool is_to_space(void *p) {
    void *to_space = current_state.to_space;
    return to_space <= p && p < to_space + current_state.to_space_size;
}


void *get_first_field(void *p) {
    stella_object *obj = (stella_object *) p;
    size_t fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
    if (fields_count <= 0) {
        return NULL;
    }

    return obj->object_fields[0];
}


void set_first_field(void *p, void *data) {
    stella_object *obj = (stella_object *) p;
    size_t fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
    if (fields_count <= 0) {
        return;
    }

    obj->object_fields[0] = data;
}


void chase(void *p) {
    do {
        stella_object *obj = (stella_object *) p;
        size_t fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
        void *q = current_state.next;
        size_t q_size = GC_OBJ_SIZE(obj);
        current_state.next = q + q_size;
        void *r = NULL;
        memcpy(q, p, q_size);
        for (size_t i = 0; i < fields_count; i++) {
            void *qf1 = obj->object_fields[i];
            void *qff1 = get_first_field(qf1);
            if (is_from_space(qf1) && !is_to_space(qff1)) {
                r = qf1;
            }
        }
        set_first_field(p, q);
        p = r;
    } while (p != NULL);
}

void *forward(void *p) {
    if (is_from_space(p)) {
        void *f1 = get_first_field(p);

        if (!f1) {
            return p;
        }

        if (is_to_space(f1)) {
            return f1;
        } else {
            chase(p);
            return get_first_field(p);
        }
    } else {
        return p;
    }
}


void collect_garbage() {
    current_state.gc_running = true;
    if (!current_state.to_space) {
        size_t to_space_size = TO_SPACE_SIZE;
        void *to_space = malloc(to_space_size);
        current_state.to_space = to_space;
        current_state.to_space_size = to_space_size;
        current_state.next = to_space;
        current_state.scan = to_space;
        current_state.limit = to_space + to_space_size;
    }


    for (gc_root *node = current_state.roots_list; node != NULL; node = node->next) {
        *node->content = forward(*node->content);
    }
}


void *scan_and_alloc(size_t size_in_bytes) {
    size_t scanned = 0;
    while (scanned < size_in_bytes) {
        stella_object *obj = (stella_object *) current_state.scan;
        size_t field_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
        size_t obj_size = GC_OBJ_SIZE(obj);
        for (size_t i = 0; i < field_count; i++) {
            void *field = obj->object_fields[i];
            if (is_from_space(field)) {
                obj->object_fields[i] = forward(field);
            }
        }
        scanned += obj_size;
    }

    if (current_state.limit - size_in_bytes >= current_state.next) {
        printf("Out of memory");
        print_gc_alloc_stats();
        print_gc_roots();
        print_gc_state();
        exit(-1);
    }

    if (current_state.scan >= current_state.next) {
        current_state.gc_running = false;
        void *tmp = current_state.from_space;
        current_state.from_space = current_state.to_space;
        current_state.to_space = tmp;

        size_t size_tmp = current_state.from_space_size;
        current_state.from_space_size = current_state.to_space_size;
        current_state.to_space_size = size_tmp;
    }

    current_state.limit -= size_in_bytes;
    return current_state.limit;
}


void *gc_alloc(size_t size_in_bytes) {
    if (!current_state.from_space) {
        size_t from_space_size = FROM_SPACE_SIZE;
        void *from_space = malloc(from_space_size);
        current_state.from_space = from_space;
        current_state.from_space_size = from_space_size;
        current_state.next = from_space;
        current_state.limit = from_space + from_space_size;
    }

    if (current_state.gc_running) {
        return scan_and_alloc(size_in_bytes);
    }

    if (current_state.next + size_in_bytes >= current_state.limit) {
        collect_garbage();
        return scan_and_alloc(size_in_bytes);
    }

    void *ptr = current_state.next;
    current_state.next += size_in_bytes;

    return ptr;
}

void gc_read_barrier(void *object, int field_index) {
    stella_object *obj = (stella_object *) object;
    void *f = obj->object_fields[field_index];
    if (is_from_space(f)) {
        obj->object_fields[field_index] = forward(f);
    }
}


void gc_write_barrier(void *object, int field_index, void *contents) {
    // no op
}

void gc_push_root(void **object) {
    if (!object) {
        return;
    }

    gc_root *node = (gc_root *) malloc(sizeof(gc_root));

    node->next = NULL;
    node->prev = current_state.roots_last;
    node->content = object;

    if (current_state.roots_last) {
        current_state.roots_last->next = node;
    } else {
        current_state.roots_list = node;
    }
    current_state.roots_last = node;
    current_state.roots_size++;
}

void gc_pop_root(void **object) {
    gc_root *node;
    for (node = current_state.roots_last; node != NULL; node = node->prev) {
        if (node->content == object) {
            break;
        }
    }

    if (node == NULL) {
        return;
    }

    gc_root *prev = node->prev;
    gc_root *next = node->next;

    if (prev) {
        prev->next = next;
    } else {
        current_state.roots_list = next;
    }

    if (next) {
        next->prev = prev;
    } else {
        current_state.roots_last = prev;
    }

    free(node);
    current_state.roots_size--;
}

void print_gc_alloc_stats() {

}

void print_gc_state() {

}

void print_gc_roots() {
    size_t i = 0;


    for (gc_root *cur = current_state.roots_list; cur != NULL; cur = cur->next) {

    }
}
