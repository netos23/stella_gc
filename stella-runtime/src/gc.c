//
// Created by Nikita Morozov on 25.10.2025.
//


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
        current_state.scan += obj_size;
    }

    if (current_state.limit - size_in_bytes < current_state.next) {
        fprintf(stderr, "Out of memory\n");
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


static size_t size_len(size_t v) {
    size_t len = 1;
    while (v >= 10) {
        v /= 10;
        ++len;
    }
    return len;
}

static size_t ptr_to_str(char *buf, size_t cap, const void *p) {
    if (p) return (size_t) snprintf(buf, cap, "%p", p);
    return (size_t) snprintf(buf, cap, "(nil)");
}

static void print_border(FILE *out,
                         size_t widx, size_t wnode, size_t wprev,
                         size_t wnext, size_t wslot, size_t wval) {
    fputc('+', out);
    for (size_t i = 0; i < widx + 2; ++i) fputc('-', out);
    fputc('+', out);
    for (size_t i = 0; i < wnode + 2; ++i) fputc('-', out);
    fputc('+', out);
    for (size_t i = 0; i < wprev + 2; ++i) fputc('-', out);
    fputc('+', out);
    for (size_t i = 0; i < wnext + 2; ++i) fputc('-', out);
    fputc('+', out);
    for (size_t i = 0; i < wslot + 2; ++i) fputc('-', out);
    fputc('+', out);
    for (size_t i = 0; i < wval + 2; ++i) fputc('-', out);
    fputc('+', out);
    fputc('\n', out);
}


void print_gc_roots() {
    FILE *out = stdout;

    // Если список пуст — краткое сообщение и выход
    if (!current_state.roots_list) {
        fprintf(out, "GC roots: empty list (head=%p, tail=%p)\n",
                (void *) current_state.roots_list, (void *) current_state.roots_last);
        return;
    }

    // Заголовки таблицы
    const char *H_IDX = "#";
    const char *H_NODE = "node";
    const char *H_PREV = "prev";
    const char *H_NEXT = "next";
    const char *H_SLOT = "slot";
    const char *H_VAL = "*slot";


    size_t widx = strlen(H_IDX);
    size_t wnode = strlen(H_NODE);
    size_t wprev = strlen(H_PREV);
    size_t wnext = strlen(H_NEXT);
    size_t wslot = strlen(H_SLOT);
    size_t wval = strlen(H_VAL);

    size_t nrows = 0;
    for (gc_root *n = current_state.roots_list; n; n = n->next) {
        char buf[32];
        size_t len;

        len = ptr_to_str(buf, sizeof(buf), (const void *) n);
        if (len > wnode) wnode = len;

        len = ptr_to_str(buf, sizeof(buf), (const void *) n->prev);
        if (len > wprev) wprev = len;

        len = ptr_to_str(buf, sizeof(buf), (const void *) n->next);
        if (len > wnext) wnext = len;

        len = ptr_to_str(buf, sizeof(buf), (const void *) n->content);
        if (len > wslot) wslot = len;

        void *val = n->content ? *n->content : NULL;
        len = ptr_to_str(buf, sizeof(buf), (const void *) val);
        if (len > wval) wval = len;

        ++nrows;
    }

    size_t idxdigits = size_len(nrows ? (nrows - 1) : 0);
    if (idxdigits > widx) widx = idxdigits;


    print_border(out, widx, wnode, wprev, wnext, wslot, wval);
    fprintf(out, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
            (int) widx, H_IDX,
            (int) wnode, H_NODE,
            (int) wprev, H_PREV,
            (int) wnext, H_NEXT,
            (int) wslot, H_SLOT,
            (int) wval, H_VAL);
    print_border(out, widx, wnode, wprev, wnext, wslot, wval);

    size_t i = 0;
    for (gc_root *n = current_state.roots_list; n; n = n->next, ++i) {
        char snode[32], sprev[32], snext[32], sslot[32], sval[32], sidx[32];

        ptr_to_str(snode, sizeof(snode), (const void *) n);
        ptr_to_str(sprev, sizeof(sprev), (const void *) n->prev);
        ptr_to_str(snext, sizeof(snext), (const void *) n->next);
        ptr_to_str(sslot, sizeof(sslot), (const void *) n->content);
        void *val = n->content ? *n->content : NULL;
        ptr_to_str(sval, sizeof(sval), (const void *) val);
        snprintf(sidx, sizeof(sidx), "%zu", i);

        fprintf(out, "| %-*s | %-*s | %-*s | %-*s | %-*s | %-*s |\n",
                (int) widx, sidx,
                (int) wnode, snode,
                (int) wprev, sprev,
                (int) wnext, snext,
                (int) wslot, sslot,
                (int) wval, sval);
    }

    print_border(out, widx, wnode, wprev, wnext, wslot, wval);
    fprintf(out, "All roots: %zu (head=%p, tail=%p)\n",
            nrows, (void *) current_state.roots_list, (void *) current_state.roots_last);
}
