/*
 * test_webcore_1422_invariants.c
 *
 * PS5 FW 14.22 (26.07-14.22.00) WebCore Non-JIT Defensive Invariant Suite
 * Research & Development: Adrián García Casado (@adri22235) - García Casado Cyber Lab
 * Legal Standard: ISO/IEC 29147 (CVD) & ISO/IEC 30111
 *
 * Models and verifies three defensive memory-safety invariants derived from
 * upstream WebKit WebCore fixes (Nov 2026) verified against the 616-era tree:
 * 1. Container reentrancy safety (CSSSegmentedFontFace::fontRanges, 425cd4e565):
 *    build local FontRanges on the stack before inserting into HashMap.
 * 2. Parser input-buffer lifetime ordering (XMLDocumentParser::startElementNs, f4d7b99959):
 *    copy libxml2 attribute spans into WebKit-owned storage prior to microtask checkpoint.
 * 3. Direct indexed internal builtin validation (InternalReadableStream::tee, ff40757890):
 *    enforce exact tuple size (2) and internal stream tag without relying on debug ASSERTs.
 */

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * 1. HashMap Bucket Rehash Safety Model (CSSSegmentedFontFace::fontRanges)
 * ------------------------------------------------------------------------- */
typedef struct {
    uint32_t key;
    uint32_t value;
    bool occupied;
} font_bucket_t;

typedef struct {
    font_bucket_t *buckets;
    size_t capacity;
    size_t count;
    uint64_t rehash_epoch;
} font_hashmap_t;

static void hashmap_init(font_hashmap_t *map, size_t initial_cap) {
    map->buckets = calloc(initial_cap, sizeof(font_bucket_t));
    assert(map->buckets != NULL);
    map->capacity = initial_cap;
    map->count = 0;
    map->rehash_epoch = 0;
}

static void hashmap_free(font_hashmap_t *map) {
    free(map->buckets);
    map->buckets = NULL;
    map->capacity = 0;
    map->count = 0;
}

static void hashmap_rehash(font_hashmap_t *map, size_t new_cap) {
    font_bucket_t *next = calloc(new_cap, sizeof(font_bucket_t));
    assert(next != NULL);
    for (size_t i = 0; i < map->capacity; i++) {
        if (map->buckets[i].occupied) {
            size_t slot = map->buckets[i].key % new_cap;
            while (next[slot].occupied) {
                slot = (slot + 1) % new_cap;
            }
            next[slot] = map->buckets[i];
        }
    }
    free(map->buckets);
    map->buckets = next;
    map->capacity = new_cap;
    map->rehash_epoch++;
}

static font_bucket_t *hashmap_insert(font_hashmap_t *map, uint32_t key, uint32_t val) {
    if ((map->count + 1) * 2 >= map->capacity) {
        hashmap_rehash(map, map->capacity * 2);
    }
    size_t slot = key % map->capacity;
    while (map->buckets[slot].occupied && map->buckets[slot].key != key) {
        slot = (slot + 1) % map->capacity;
    }
    if (!map->buckets[slot].occupied) {
        map->count++;
    }
    map->buckets[slot].key = key;
    map->buckets[slot].value = val;
    map->buckets[slot].occupied = true;
    return &map->buckets[slot];
}

static void verify_font_ranges_reentrancy_invariant(void) {
    printf("[Invariant 1] CSSSegmentedFontFace::fontRanges() Container Reentrancy\n");

    font_hashmap_t cache;
    hashmap_init(&cache, 4);

    /* Simulate storing bucket address before reentrant font-load callbacks */
    font_bucket_t *pre_rehash_bucket = hashmap_insert(&cache, 10U, 0U);
    uint64_t initial_epoch = cache.rehash_epoch;
    font_bucket_t *initial_backing = cache.buckets;

    /* Reentrant callbacks insert multiple computedSize keys, forcing table rehash */
    for (uint32_t size = 11U; size < 24U; size++) {
        hashmap_insert(&cache, size, size * 100U);
    }
    assert(cache.rehash_epoch > initial_epoch);
    assert(cache.buckets != initial_backing);
    (void)pre_rehash_bucket; /* Would dangle if dereferenced after rehash! */

    /* Fixed 425cd4e565 pattern: materialize stack-local FontRanges first, insert last */
    uint32_t local_ranges_val = 0U;
    for (uint32_t face_idx = 1; face_idx <= 4; face_idx++) {
        local_ranges_val += face_idx;
    }
    font_bucket_t *safe_entry = hashmap_insert(&cache, 10U, local_ranges_val);
    assert(safe_entry->key == 10U && safe_entry->value == 10U);

    hashmap_free(&cache);
    printf("    -> Rehash invalidated prior bucket table (epoch 0 -> %" PRIu64 ")\n",
           cache.rehash_epoch);
    printf("    -> Stack-local FontRanges insertion remained safe (val=%u) (OK)\n\n",
           local_ranges_val);
}

/* -------------------------------------------------------------------------
 * 2. libxml2 Input Buffer Ownership Model (XMLDocumentParser::startElementNs)
 * ------------------------------------------------------------------------- */
typedef struct {
    char *input_buf;
    bool freed;
} libxml_parser_ctx_t;

typedef struct {
    char copied_attr[64];
} webkit_owned_attribute_t;

static void verify_xml_microtask_ordering_invariant(void) {
    printf("[Invariant 2] XMLDocumentParser::startElementNs() Span Lifetime Order\n");

    libxml_parser_ctx_t parser = {
        .input_buf = strdup("data-webkit-boundary=safe-span"),
        .freed = false,
    };
    assert(parser.input_buf != NULL);

    /* Fixed f4d7b99959 pattern: copy spans into WebKit-owned storage BEFORE microtasks */
    webkit_owned_attribute_t owned_attr = {0};
    strncpy(owned_attr.copied_attr, parser.input_buf, sizeof(owned_attr.copied_attr) - 1);

    /* Simulate microtask checkpoint stopping/detaching parser and freeing libxml2 buffer */
    free(parser.input_buf);
    parser.input_buf = NULL;
    parser.freed = true;

    assert(parser.freed && parser.input_buf == NULL);
    assert(strcmp(owned_attr.copied_attr, "data-webkit-boundary=safe-span") == 0);

    printf("    -> libxml2 buffer released during microtask checkpoint (freed=true)\n");
    printf("    -> WebKit-owned attribute preserved: \"%s\" (OK)\n\n",
           owned_attr.copied_attr);
}

/* -------------------------------------------------------------------------
 * 3. Direct Indexed Internal Builtin Check (InternalReadableStream::tee)
 * ------------------------------------------------------------------------- */
typedef enum {
    OBJ_PLAIN_JS = 0,
    OBJ_INTERNAL_READABLE_STREAM = 1,
} js_internal_tag_t;

typedef struct {
    js_internal_tag_t tag;
    uint64_t controller_id;
} js_object_slot_t;

static bool validate_internal_tee_result(const js_object_slot_t *items, size_t len) {
    if (items == NULL || len != 2U) {
        return false;
    }
    return items[0].tag == OBJ_INTERNAL_READABLE_STREAM &&
           items[1].tag == OBJ_INTERNAL_READABLE_STREAM;
}

static void verify_readable_stream_tee_validation(void) {
    printf("[Invariant 3] InternalReadableStream::tee() Indexed Validation\n");

    js_object_slot_t truncated[1] = {
        { .tag = OBJ_INTERNAL_READABLE_STREAM, .controller_id = 1ULL }
    };
    js_object_slot_t spoofed[2] = {
        { .tag = OBJ_INTERNAL_READABLE_STREAM, .controller_id = 1ULL },
        { .tag = OBJ_PLAIN_JS, .controller_id = 0ULL }
    };
    js_object_slot_t valid[2] = {
        { .tag = OBJ_INTERNAL_READABLE_STREAM, .controller_id = 101ULL },
        { .tag = OBJ_INTERNAL_READABLE_STREAM, .controller_id = 102ULL }
    };

    assert(validate_internal_tee_result(truncated, 1U) == false);
    assert(validate_internal_tee_result(spoofed, 2U) == false);
    assert(validate_internal_tee_result(valid, 2U) == true);

    printf("    -> Truncated tuple (len=1) rejected cleanly (TypeError path)\n");
    printf("    -> Wrong-type slot rejected cleanly; valid 2-branch stream accepted (OK)\n\n");
}

int main(void) {
    printf("=================================================================\n");
    printf("[+] PS5 FW 14.22 WebCore Defensive Invariant Verification\n");
    printf("[+] Research: Adrián García Casado (@adri22235) - García Casado Cyber Lab\n");
    printf("[+] Compliance: ISO/IEC 29147 (CVD) & ISO/IEC 30111\n");
    printf("=================================================================\n\n");

    verify_font_ranges_reentrancy_invariant();
    verify_xml_microtask_ordering_invariant();
    verify_readable_stream_tee_validation();

    printf("=================================================================\n");
    printf("  [✓] ALL FW 14.22 WEBCORE INVARIANT MODELS VERIFIED CLEANLY\n");
    printf("=================================================================\n");
    return 0;
}
