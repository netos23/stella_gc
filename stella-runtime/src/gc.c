//
// Created by Nikita Morozov on 25.10.2025.
//


#include "stella/gc.h"


gc_state current_state = {
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

gc_stats stats = {
        .total_allocated_bytes= 0,
        .total_allocated= 0,
        .maximum_residency_bytes= 0,
        .maximum_residency= 0,
        .residency_bytes= 0,
        .residency= 0,
        .reads= 0,
        .writes= 0,
        .read_barriers= 0,
        .write_barriers= 0,
        .gc_cycles= 0,
};


bool is_from_space(void *p) {
    char *obj = (char *) p;
    char *from_space = (char *) current_state.from_space;
    return from_space <= obj && obj < from_space + current_state.from_space_size;
}

bool is_to_space(void *p) {
    char *obj = (char *) p;
    char *to_space = (char *) current_state.to_space;
    return to_space <= obj && obj < to_space + current_state.to_space_size;
}

bool is_record(void *p){
    if (!p) return false;
    stella_object *obj = (stella_object *) p;
    enum TAG tag = STELLA_OBJECT_HEADER_TAG(obj->object_header);
    size_t size = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
    switch (tag) {
        case TAG_ZERO:
        case TAG_SUCC:
        case TAG_FALSE:
        case TAG_TRUE:
            return false;
        case TAG_FN:
        case TAG_REF:
        case TAG_UNIT:
            return true;
        case TAG_TUPLE:
            return size > 0;
        case TAG_INL:
        case TAG_INR:
        case TAG_EMPTY:
        case TAG_CONS:
            return true;
        default:
            return false;
    }
}


void *get_first_field(void *p) {
    stats.reads++;
    stella_object *obj = (stella_object *) p;
    size_t fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
    if (fields_count <= 0) {
        return NULL;
    }

    return obj->object_fields[0];
}


void set_first_field(void *p, void *data) {
    stats.writes++;
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
        current_state.next = (char *) q + q_size;
        void *r = NULL;
        stats.reads++;
        stats.writes++;
        memcpy(q, p, q_size);
        for (size_t i = 0; i < fields_count; i++) {
            stella_object *qf1 = (stella_object *) obj->object_fields[i];
            stats.reads++;

            if (is_record(qf1) && is_from_space(qf1)) {
                void *qff1 = get_first_field(qf1);
                if (!is_record(qff1) || !is_to_space(qff1)) {
                    r = qf1;
                }

            }
        }
        set_first_field(p, q);
        p = r;
    } while (p != NULL);
}

void *forward(void *p) {
    if (is_record(p) && is_from_space(p)) {
        void *f1 = get_first_field(p);

        if (!f1) {
            return p;
        }

        if (is_record(f1) && is_to_space(f1)) {
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
        stats.reads++;
        stella_object *obj = (stella_object *) current_state.scan;
        size_t field_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
        size_t obj_size = GC_OBJ_SIZE(obj);
        for (size_t i = 0; i < field_count; i++) {
            void *field = obj->object_fields[i];
            stats.reads++;
            if (is_from_space(field)) {
                stats.writes++;
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

    stats.total_allocated++;
    stats.total_allocated_bytes += size_in_bytes;
    stats.residency++;
    stats.residency_bytes += size_in_bytes;
    stats.writes++;

    if (current_state.scan >= current_state.next) {
        current_state.gc_running = false;
        void *tmp = current_state.from_space;
        current_state.from_space = current_state.to_space;
        current_state.to_space = tmp;

        size_t size_tmp = current_state.from_space_size;
        current_state.from_space_size = current_state.to_space_size;
        current_state.to_space_size = size_tmp;
        stats.gc_cycles++;
        stats.maximum_residency = MAX(stats.maximum_residency, stats.residency);
        stats.maximum_residency_bytes = MAX(stats.maximum_residency_bytes, stats.residency_bytes);
        stats.residency = 0;
        stats.residency_bytes = 0;
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

    stats.total_allocated++;
    stats.total_allocated_bytes += size_in_bytes;
    stats.residency++;
    stats.residency_bytes += size_in_bytes;
    stats.writes++;

    return ptr;
}

void gc_read_barrier(void *object, int field_index) {
    stats.read_barriers++;
    if(!current_state.gc_running){
        return;
    }

    stella_object *obj = (stella_object *) object;
    void *f = obj->object_fields[field_index];
    if (is_from_space(f)) {
        obj->object_fields[field_index] = forward(f);
    }
}


void gc_write_barrier(void *object, int field_index, void *contents) {
    stats.write_barriers++;
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

static void fmt_commas(size_t v, char *out, size_t cap) {
    char buf[64];
    size_t i = 0, group = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = '\0';
        return;
    }
    while (v && i + 1 < sizeof(buf)) {
        if (group == 3) {
            buf[i++] = ',';
            group = 0;
        }
        buf[i++] = (char) ('0' + (v % 10));
        v /= 10;
        group++;
    }

    size_t n = (i < cap - 1) ? i : cap - 1;
    for (size_t k = 0; k < n; k++) out[k] = buf[i - 1 - k];
    out[n] = '\0';
}

static void fmt_pair_bytes_objs(size_t bytes, size_t objs, char *out, size_t cap) {
    char b[64], o[64];
    fmt_commas(bytes, b, sizeof(b));
    fmt_commas(objs, o, sizeof(o));
    snprintf(out, cap, "%s bytes (%s objects)", b, o);
}


void print_gc_alloc_stats() {
    char totalalloc[96], maxres[96], curres[96];
    fmt_pair_bytes_objs(stats.total_allocated_bytes, stats.total_allocated, totalalloc, sizeof totalalloc);
    fmt_pair_bytes_objs(stats.maximum_residency_bytes, stats.maximum_residency, maxres, sizeof maxres);
    fmt_pair_bytes_objs(stats.residency_bytes, stats.residency, curres, sizeof curres);

    char readss[64], writess[64], rbs[64], wbs[64], cycless[64];
    fmt_commas(stats.reads, readss, sizeof readss);
    fmt_commas(stats.writes, writess, sizeof writess);
    fmt_commas(stats.read_barriers, rbs, sizeof rbs);
    fmt_commas(stats.write_barriers, wbs, sizeof wbs);
    fmt_commas(stats.gc_cycles, cycless, sizeof cycless);

    printf("Garbage collector (GC) statistics:\n");
    printf("- Total memory allocation: %s\n", totalalloc);
    printf("- GC cycles: %s\n", cycless);
    printf("- Maximum residency: %s\n", maxres);
    printf("- Current residency: %s\n", curres);
    printf("- Total memory use: %s reads and %s writes\n", readss, writess);
    printf("- Barrier hits: %s read, %s write\n", rbs, wbs);

}

void print_gc_state() {
    char curres[96];
    fmt_pair_bytes_objs(stats.residency_bytes, stats.residency, curres, sizeof curres);
    size_t free_memory = current_state.limit - current_state.scan;
    printf("Garbage collector (GC) state:\n");

    printf("- Total scan: %p, next: %p, limit: %p\n", current_state.scan, current_state.next, current_state.limit);
    printf("- Current allocated: %s\n", curres);
    printf("- Total free memory : %zu\n", free_memory);
    print_gc_roots();
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
    return (size_t) snprintf(buf, cap, "(null)");
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


    if (!current_state.roots_list) {
        fprintf(out, "GC roots: empty list (head=%p, tail=%p)\n",
                (void *) current_state.roots_list, (void *) current_state.roots_last);
        return;
    }


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
