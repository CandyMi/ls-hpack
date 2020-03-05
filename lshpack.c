/*
MIT License

Copyright (c) 2018 LiteSpeed Technologies Inc

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#ifdef WIN32
#include <vc_compat.h>
#endif

#include "lshpack.h"
#if LS_HPACK_EMIT_TEST_CODE
#include "lshpack-test.h"
#endif
#include XXH_HEADER_NAME

#ifndef LS_HPACK_USE_LARGE_TABLES
#define LS_HPACK_USE_LARGE_TABLES 1
#endif

#include "huff-tables.h"
#include "http/ls_http_header.h"

#define HPACK_STATIC_TABLE_SIZE   61
#define INITIAL_DYNAMIC_TABLE_SIZE  4096

/* RFC 7541, Section 4.1:
 *
 * " The size of the dynamic table is the sum of the size of its entries.
 * "
 * " The size of an entry is the sum of its name's length in octets (as
 * " defined in Section 5.2), its value's length in octets, and 32.
 */
#define DYNAMIC_ENTRY_OVERHEAD 32

#define NAME_VAL(a, b) sizeof(a) - 1, sizeof(b) - 1, (a), (b)


static const struct
{
    lshpack_strlen_t  name_len;
    lshpack_strlen_t  val_len;
    const char       *name;
    const char       *val;
}
static_table[HPACK_STATIC_TABLE_SIZE] =
{
    { NAME_VAL(":authority",                    "") },
    { NAME_VAL(":method",                       "GET") },
    { NAME_VAL(":method",                       "POST") },
    { NAME_VAL(":path",                         "/") },
    { NAME_VAL(":path",                         "/index.html") },
    { NAME_VAL(":scheme",                       "http") },
    { NAME_VAL(":scheme",                       "https") },
    { NAME_VAL(":status",                       "200") },
    { NAME_VAL(":status",                       "204") },
    { NAME_VAL(":status",                       "206") },
    { NAME_VAL(":status",                       "304") },
    { NAME_VAL(":status",                       "400") },
    { NAME_VAL(":status",                       "404") },
    { NAME_VAL(":status",                       "500") },
    { NAME_VAL("accept-charset",                "") },
    { NAME_VAL("accept-encoding",               "gzip, deflate") },
    { NAME_VAL("accept-language",               "") },
    { NAME_VAL("accept-ranges",                 "") },
    { NAME_VAL("accept",                        "") },
    { NAME_VAL("access-control-allow-origin",   "") },
    { NAME_VAL("age",                           "") },
    { NAME_VAL("allow",                         "") },
    { NAME_VAL("authorization",                 "") },
    { NAME_VAL("cache-control",                 "") },
    { NAME_VAL("content-disposition",           "") },
    { NAME_VAL("content-encoding",              "") },
    { NAME_VAL("content-language",              "") },
    { NAME_VAL("content-length",                "") },
    { NAME_VAL("content-location",              "") },
    { NAME_VAL("content-range",                 "") },
    { NAME_VAL("content-type",                  "") },
    { NAME_VAL("cookie",                        "") },
    { NAME_VAL("date",                          "") },
    { NAME_VAL("etag",                          "") },
    { NAME_VAL("expect",                        "") },
    { NAME_VAL("expires",                       "") },
    { NAME_VAL("from",                          "") },
    { NAME_VAL("host",                          "") },
    { NAME_VAL("if-match",                      "") },
    { NAME_VAL("if-modified-since",             "") },
    { NAME_VAL("if-none-match",                 "") },
    { NAME_VAL("if-range",                      "") },
    { NAME_VAL("if-unmodified-since",           "") },
    { NAME_VAL("last-modified",                 "") },
    { NAME_VAL("link",                          "") },
    { NAME_VAL("location",                      "") },
    { NAME_VAL("max-forwards",                  "") },
    { NAME_VAL("proxy-authenticate",            "") },
    { NAME_VAL("proxy-authorization",           "") },
    { NAME_VAL("range",                         "") },
    { NAME_VAL("referer",                       "") },
    { NAME_VAL("refresh",                       "") },
    { NAME_VAL("retry-after",                   "") },
    { NAME_VAL("server",                        "") },
    { NAME_VAL("set-cookie",                    "") },
    { NAME_VAL("strict-transport-security",     "") },
    { NAME_VAL("transfer-encoding",             "") },
    { NAME_VAL("user-agent",                    "") },
    { NAME_VAL("vary",                          "") },
    { NAME_VAL("via",                           "") },
    { NAME_VAL("www-authenticate",              "") }
};


static const uint32_t static_table_name_hash[HPACK_STATIC_TABLE_SIZE] =
{
    2291248681U,  2986188533U,  2986188533U,  682507278U,   682507278U,
    367166221U,   367166221U,   2654880352U,  2654880352U,  2654880352U,
    2654880352U,  2654880352U,  2654880352U,  2654880352U,  3524820720U,
    933295571U,   813015558U,   2455864366U,  691181430U,   242361332U,
    3834784808U,  1449090918U,  670945460U,   948268347U,   3892037397U,
    2390783832U,  10250307U,    3048951200U,  1992850710U,  717159389U,
    3971600902U,  361291225U,   3119783934U,  3405294591U,  2067499073U,
    1871984943U,  2214542492U,  4061963548U,  3058873016U,  1673242212U,
    177203806U,   3851048949U,  3204010285U,  4028795988U,  1319210624U,
    3083522210U,  1704740942U,  284081010U,   3557734150U,  80280704U,
    2330042822U,  4174856472U,  3865504522U,  385071982U,   2287223314U,
    3366877595U,  3850975681U,  1897333990U,  3090999634U,  1739293753U,
    655250052U,
};


struct encode_el
{
    uint32_t code;
    int      bits;
};


struct decode_el
{
    uint8_t state;
    uint8_t flags;
    uint8_t sym;
};


#define lshpack_arr_init(a) do {                                        \
    memset((a), 0, sizeof(*(a)));                                       \
} while (0)

#define lshpack_arr_cleanup(a) do {                                     \
    free((a)->els);                                                     \
    memset((a), 0, sizeof(*(a)));                                       \
} while (0)

#define lshpack_arr_get(a, i) (                                         \
    assert((i) < (a)->nelem),                                           \
    (a)->els[(a)->off + (i)]                                            \
)

#define lshpack_arr_shift(a) (                                          \
    assert((a)->nelem > 0),                                             \
    (a)->nelem -= 1,                                                    \
    (a)->els[(a)->off++]                                                \
)

#define lshpack_arr_pop(a) (                                            \
    assert((a)->nelem > 0),                                             \
    (a)->nelem -= 1,                                                    \
    (a)->els[(a)->off + (a)->nelem]                                     \
)

#define lshpack_arr_count(a) (+(a)->nelem)

static int
lshpack_arr_push (struct lshpack_arr *arr, uintptr_t val)
{
    uintptr_t *new_els;
    unsigned n;

    if (arr->off + arr->nelem < arr->nalloc)
    {
        arr->els[arr->off + arr->nelem] = val;
        ++arr->nelem;
        return 0;
    }

    if (arr->off > arr->nalloc / 2)
    {
        memmove(arr->els, arr->els + arr->off,
                                        sizeof(arr->els[0]) * arr->nelem);
        arr->off = 0;
        arr->els[arr->nelem] = val;
        ++arr->nelem;
        return 0;
    }

    if (arr->nalloc)
        n = arr->nalloc * 2;
    else
        n = 64;
    new_els = malloc(n * sizeof(arr->els[0]));
    if (!new_els)
        return -1;
    memcpy(new_els, arr->els + arr->off, sizeof(arr->els[0]) * arr->nelem);
    free(arr->els);
    arr->off = 0;
    arr->els = new_els;
    arr->nalloc = n;
    arr->els[arr->off + arr->nelem] = val;
    ++arr->nelem;
    return 0;
}


struct lshpack_double_enc_head
{
    struct lshpack_enc_head by_name;
    struct lshpack_enc_head by_nameval;
};

struct lshpack_enc_table_entry
{
    /* An entry always lives on all three lists */
    STAILQ_ENTRY(lshpack_enc_table_entry)
                                    ete_next_nameval,
                                    ete_next_name,
                                    ete_next_all;
    unsigned                        ete_id;
    unsigned                        ete_nameval_hash;
    unsigned                        ete_name_hash;
    lshpack_strlen_t                ete_name_len;
    lshpack_strlen_t                ete_val_len;
    char                            ete_buf[0];
};


struct lshpack_enc_input
{
    const ls_http_header_t         *hdr;
    unsigned                        nameval_hash;
    unsigned                        name_hash;
    unsigned                        static_table_id;
    unsigned                        val_matched;
};


#define ETE_NAME(ete) ((ete)->ete_buf)
#define ETE_VALUE(ete) (&(ete)->ete_buf[(ete)->ete_name_len])


#define N_BUCKETS(n_bits) (1U << (n_bits))
#define BUCKNO(n_bits, hash) ((hash) & (N_BUCKETS(n_bits) - 1))


/* We estimate average number of entries in the dynamic table to be 1/3
 * of the theoretical maximum.  This number is used to size the history
 * buffer: we want it large enough to cover recent entries, yet not too
 * large to cover entries that appear with a period larger than the
 * dynamic table.
 */
static unsigned
henc_hist_size (unsigned max_capacity)
{
    return max_capacity / DYNAMIC_ENTRY_OVERHEAD / 3;
}


int
lshpack_enc_init (struct lshpack_enc *enc)
{
    struct lshpack_double_enc_head *buckets;
    unsigned nbits = 2;
    unsigned i;

    buckets = malloc(sizeof(buckets[0]) * N_BUCKETS(nbits));
    if (!buckets)
        return -1;

    for (i = 0; i < N_BUCKETS(nbits); ++i)
    {
        STAILQ_INIT(&buckets[i].by_name);
        STAILQ_INIT(&buckets[i].by_nameval);
    }

    memset(enc, 0, sizeof(*enc));
    STAILQ_INIT(&enc->hpe_all_entries);
    enc->hpe_max_capacity = INITIAL_DYNAMIC_TABLE_SIZE;
    enc->hpe_buckets      = buckets;
    /* The initial value of the entry ID is completely arbitrary.  As long as
     * there are fewer than 2^32 dynamic table entries, the math to calculate
     * the entry ID works.  To prove to ourselves that the wraparound works
     * and to have the unit tests cover it, we initialize the next ID so that
     * it is just about to wrap around.
     */
    enc->hpe_next_id      = ~0 - 3;
    enc->hpe_nbits        = nbits;
    enc->hpe_nelem        = 0;
    
    return 0;
}


void
lshpack_enc_cleanup (struct lshpack_enc *enc)
{
    struct lshpack_enc_table_entry *entry, *next;
    for (entry = STAILQ_FIRST(&enc->hpe_all_entries); entry; entry = next)
    {
        next = STAILQ_NEXT(entry, ete_next_all);
        free(entry);
    }
    free(enc->hpe_hist_buf);
    free(enc->hpe_buckets);
}


static int
henc_use_hist (struct lshpack_enc *enc)
{
    unsigned hist_size;

    if (enc->hpe_hist_buf)
        return 0;

    hist_size = henc_hist_size(INITIAL_DYNAMIC_TABLE_SIZE);
    if (!hist_size)
        return 0;

    enc->hpe_hist_buf = malloc(sizeof(enc->hpe_hist_buf[0]) * (hist_size + 1));
    if (!enc->hpe_hist_buf)
        return -1;

    enc->hpe_hist_size = hist_size;
    enc->hpe_flags |= LSHPACK_ENC_USE_HIST;
    return 0;
}


int
lshpack_enc_use_hist (struct lshpack_enc *enc, int on)
{
    if (on)
        return henc_use_hist(enc);
    else
    {
        enc->hpe_flags &= ~LSHPACK_ENC_USE_HIST;
        free(enc->hpe_hist_buf);
        enc->hpe_hist_buf = NULL;
        enc->hpe_hist_size = 0;
        enc->hpe_hist_idx = 0;
        enc->hpe_hist_wrapped = 0;
        return 0;
    }
}


int
lshpack_enc_hist_used (const struct lshpack_enc *enc)
{
    return (enc->hpe_flags & LSHPACK_ENC_USE_HIST) != 0;
}


#define LSHPACK_XXH_SEED 0
#define XXH_NAMEVAL_WIDTH 9
#define XXH_NAMEVAL_SHIFT 0
#define XXH_NAME_WIDTH 9
#define XXH_NAME_SHIFT 9

static const unsigned char nameval2id[ 1 << XXH_NAMEVAL_WIDTH ] =
{
    [11]   =  2,   [472]  =  3,   [273]  =  4,   [248]  =  5,   [186]  =  6,
    [495]  =  7,   [199]  =  8,   [152]  =  9,   [72]   =  10,  [398]  =  11,
    [331]  =  12,  [82]   =  13,  [457]  =  14,  [280]  =  16,
};

static const unsigned char name2id[ 1 << XXH_NAME_WIDTH ] =
{
    [215]  =  1,   [207]  =  2,   [286]  =  4,   [321]  =  6,   [289]  =  8,
    [63]   =  15,  [122]  =  16,  [209]  =  17,  [194]  =  18,  [331]  =  19,
    [273]  =  20,  [278]  =  21,  [431]  =  22,  [232]  =  23,  [182]  =  24,
    [483]  =  25,  [59]   =  26,  [52]   =  27,  [422]  =  28,  [62]   =  29,
    [381]  =  30,  [233]  =  31,  [110]  =  32,  [15]   =  33,  [85]   =  34,
    [452]  =  35,  [28]   =  36,  [414]  =  37,  [82]   =  38,  [345]  =  39,
    [467]  =  40,  [501]  =  41,  [299]  =  42,  [168]  =  43,  [326]  =  44,
    [199]  =  45,  [360]  =  46,  [36]   =  47,  [349]  =  48,  [347]  =  49,
    [126]  =  50,  [208]  =  51,  [416]  =  52,  [373]  =  53,  [477]  =  54,
    [33]   =  55,  [316]  =  56,  [156]  =  57,  [386]  =  58,  [116]  =  59,
    [450]  =  60,  [297]  =  61,
};


static unsigned
lookup_static_nameval (const struct lshpack_enc_input *input)
{
    unsigned i;

    i = (input->nameval_hash >> XXH_NAMEVAL_SHIFT) & ((1 << XXH_NAMEVAL_WIDTH) - 1);
    if (nameval2id[i])
    {
        i = nameval2id[i] - 1;
        if (static_table[i].name_len == input->hdr->name.len
            && static_table[i].val_len == input->hdr->value.len
            && memcmp(input->hdr->name.ptr, static_table[i].name, input->hdr->name.len) == 0
            && memcmp(input->hdr->value.ptr, static_table[i].val, input->hdr->value.len) == 0)
        {
            return i + 1;
        }
    }

    return 0;
}


static unsigned
lookup_static_nameval2 (const struct lsxpack_header *input)
{
    unsigned i;

    i = (input->nameval_hash >> XXH_NAMEVAL_SHIFT) & ((1 << XXH_NAMEVAL_WIDTH) - 1);
    if (nameval2id[i])
    {
        i = nameval2id[i] - 1;
        if (static_table[i].name_len == input->name_len
            && static_table[i].val_len == input->val_len
            && memcmp(input->buf + input->name_offset, static_table[i].name, input->name_len) == 0
            && memcmp(input->buf + input->val_offset, static_table[i].val, input->val_len) == 0)
        {
            return i + 1;
        }
    }

    return LSHPACK_HDR_UNKNOWN;
}


static unsigned
lookup_static_name (const struct lshpack_enc_input *input)
{
    unsigned i;

    i = (input->name_hash >> XXH_NAME_SHIFT) & ((1 << XXH_NAME_WIDTH) - 1);
    if (name2id[i])
    {
        i = name2id[i] - 1;
        if (static_table[i].name_len == input->hdr->name.len
            && memcmp(input->hdr->name.ptr, static_table[i].name,
                                                input->hdr->name.len) == 0)
        {
            return i + 1;
        }
    }

    return 0;
}


static unsigned
lookup_static_name2 (const struct lsxpack_header *input)
{
    unsigned i;

    i = (input->name_hash >> XXH_NAME_SHIFT) & ((1 << XXH_NAME_WIDTH) - 1);
    if (name2id[i])
    {
        i = name2id[i] - 1;
        if (static_table[i].name_len == input->name_len
            && memcmp(input->buf + input->name_offset, static_table[i].name,
                      input->name_len) == 0)
        {
            return i + 1;
        }
    }

    return LSHPACK_HDR_UNKNOWN;
}


unsigned
lshpack_enc_get_stx_tab_id (const char *name, lshpack_strlen_t name_len,
                                    const char *val, lshpack_strlen_t val_len)
{
    uint32_t name_hash, nameval_hash;
    unsigned i;

    name_hash = XXH32(name, name_len, LSHPACK_XXH_SEED);
    nameval_hash = XXH32(val, val_len, name_hash);

    i = (nameval_hash >> XXH_NAMEVAL_SHIFT) & ((1 << XXH_NAMEVAL_WIDTH) - 1);
    if (nameval2id[i])
    {
        i = nameval2id[i] - 1;
        if (static_table[i].name_len == name_len
            && static_table[i].val_len == val_len
            && memcmp(name, static_table[i].name, name_len) == 0
            && memcmp(val, static_table[i].val, val_len) == 0)
        {
            return i + 1;
        }
    }

    i = (name_hash >> XXH_NAME_SHIFT) & ((1 << XXH_NAME_WIDTH) - 1);
    if (name2id[i])
    {
        i = name2id[i] - 1;
        if (static_table[i].name_len == name_len
            && memcmp(name, static_table[i].name, name_len) == 0)
        {
            return i + 1;
        }
    }

    return 0;
}


static inline void
lshpack_enc_update_hash(struct lsxpack_header *input)
{
    if (!(input->flags & LSXPACK_NAME_HASH))
        input->name_hash = XXH32(input->buf + input->name_offset,
                                 input->name_len, LSHPACK_XXH_SEED);
    else
        assert(input->name_hash == XXH32(input->buf + input->name_offset,
                                         input->name_len, LSHPACK_XXH_SEED));

    if (!(input->flags & LSXPACK_NAMEVAL_HASH))
        input->nameval_hash = XXH32(input->buf + input->val_offset,
                                    input->val_len, input->name_hash);
    else
        assert(input->nameval_hash == XXH32(input->buf + input->val_offset,
                                            input->val_len, input->name_hash));

    input->flags |= (LSXPACK_NAME_HASH | LSXPACK_NAMEVAL_HASH);
}


unsigned
lshpack_enc_get_stx_tab_id2 (struct lsxpack_header *input)
{
    unsigned i;

    lshpack_enc_update_hash(input);

    i = (input->nameval_hash >> XXH_NAMEVAL_SHIFT) & ((1 << XXH_NAMEVAL_WIDTH) - 1);
    if (nameval2id[i])
    {
        i = nameval2id[i] - 1;
        if (static_table[i].name_len == input->name_len
            && static_table[i].val_len == input->val_len
            && memcmp(input->buf + input->name_offset, static_table[i].name,
                      input->name_len) == 0
            && memcmp(input->buf + input->val_offset, static_table[i].val,
                      input->val_len) == 0)
        {
            return i + 1;
        }
    }

    i = (input->name_hash >> XXH_NAME_SHIFT) & ((1 << XXH_NAME_WIDTH) - 1);
    if (name2id[i])
    {
        i = name2id[i] - 1;
        if (static_table[i].name_len == input->name_len
            && memcmp(input->buf + input->name_offset, static_table[i].name,
                      input->name_len) == 0)
        {
            return i + 1;
        }
    }

    return 0;
}


/* Given a dynamic entry, return its table ID */
static unsigned
henc_calc_table_id (const struct lshpack_enc *enc,
                                    const struct lshpack_enc_table_entry *entry)
{
    return HPACK_STATIC_TABLE_SIZE
         + (enc->hpe_next_id - entry->ete_id)
    ;
}


static unsigned
henc_find_table_id (struct lshpack_enc *enc, struct lshpack_enc_input *input)
{
    struct lshpack_enc_table_entry *entry;
    unsigned buckno, id;

    /* First, look for a match in the static table: */
    if (input->static_table_id > 0)
    {
        assert(input->static_table_id == lshpack_enc_get_stx_tab_id(
                            input->hdr->name.ptr, input->hdr->name.len,
                            input->hdr->value.ptr, input->hdr->value.len));
        id = input->static_table_id - 1;
        if (id <= LSHPACK_HDR_ACCEPT_ENCODING)
        {
            if (static_table[id].val_len == input->hdr->value.len
                && memcmp(input->hdr->value.ptr, static_table[id].val,
                          input->hdr->value.len) == 0)
            {
                input->val_matched = 1;
                return input->static_table_id;
            }
        }
        input->name_hash = static_table_name_hash[ input->static_table_id - 1];
        input->nameval_hash = XXH32(input->hdr->value.ptr, input->hdr->value.len,
                                                            input->name_hash);
    }
    else
    {
        input->name_hash = XXH32(input->hdr->name.ptr, input->hdr->name.len,
                                 LSHPACK_XXH_SEED);
        input->nameval_hash = XXH32(input->hdr->value.ptr, input->hdr->value.len,
                                                            input->name_hash);
        input->static_table_id = lookup_static_nameval(input);
        if (input->static_table_id > 0)
        {
            input->val_matched = 1;
            return input->static_table_id;
        }
    }

    /* Search by name and value: */
    buckno = BUCKNO(enc->hpe_nbits, input->nameval_hash);
    STAILQ_FOREACH(entry, &enc->hpe_buckets[buckno].by_nameval,
                                                        ete_next_nameval)
        if (input->nameval_hash == entry->ete_nameval_hash &&
            input->hdr->name.len == entry->ete_name_len &&
            input->hdr->value.len == entry->ete_val_len &&
            0 == memcmp(input->hdr->name.ptr, ETE_NAME(entry), input->hdr->name.len) &&
            0 == memcmp(input->hdr->value.ptr, ETE_VALUE(entry), input->hdr->value.len))
        {
            input->val_matched = 1;
            return henc_calc_table_id(enc, entry);
        }

    /* Name/value match is not found, look for header: */
    input->static_table_id = lookup_static_name(input);
    if (input->static_table_id > 0)
    {
        input->val_matched = 0;
        return input->static_table_id;
    }

    /* Search by name only: */
    buckno = BUCKNO(enc->hpe_nbits, input->name_hash);
    STAILQ_FOREACH(entry, &enc->hpe_buckets[buckno].by_name, ete_next_name)
        if (input->name_hash == entry->ete_name_hash &&
            input->hdr->name.len == entry->ete_name_len &&
            0 == memcmp(input->hdr->name.ptr, ETE_NAME(entry), input->hdr->name.len))
        {
            input->val_matched = 0;
            return henc_calc_table_id(enc, entry);
        }

    return 0;
}


static unsigned
henc_find_table_id2 (struct lshpack_enc *enc, lsxpack_header_t *input)
{
    struct lshpack_enc_table_entry *entry;
    unsigned buckno, id;
    const char *val_ptr = input->buf + input->val_offset;
    const char *name_ptr = input->buf + input->name_offset;
    /* First, look for a match in the static table: */
    if (input->flags & LSXPACK_HPACK_IDX)
    {
        assert(input->hpack_index == lshpack_enc_get_stx_tab_id2(input));
        id = input->hpack_index - 1;
        if (id <= LSHPACK_HDR_ACCEPT_ENCODING)
        {
            if (static_table[id].val_len == input->val_len
                && memcmp(val_ptr, static_table[id].val,
                          input->val_len) == 0)
            {
                input->flags |= LSXPACK_VAL_MATCHED;
                return input->hpack_index;
            }
        }
        if (!(input->flags & LSXPACK_NAME_HASH))
            input->name_hash = static_table_name_hash[input->hpack_index];
        else
            assert(input->name_hash == static_table_name_hash[input->hpack_index]);
        if (!(input->flags & LSXPACK_NAMEVAL_HASH))
            input->nameval_hash = XXH32(val_ptr, input->val_len,
                                        input->name_hash);
        else
            assert(input->nameval_hash == XXH32(val_ptr, input->val_len,
                                                input->name_hash));
        input->flags |= (LSXPACK_NAME_HASH | LSXPACK_NAMEVAL_HASH);
    }
    else
    {
        lshpack_enc_update_hash(input);
        input->hpack_index = lookup_static_nameval2(input);
        if (input->hpack_index != LSHPACK_HDR_UNKNOWN)
        {
            input->flags |= LSXPACK_VAL_MATCHED;
            return input->hpack_index;
        }
    }

    /* Search by name and value: */
    buckno = BUCKNO(enc->hpe_nbits, input->nameval_hash);
    STAILQ_FOREACH(entry, &enc->hpe_buckets[buckno].by_nameval,
                                                        ete_next_nameval)
        if (input->nameval_hash == entry->ete_nameval_hash &&
            input->name_len == entry->ete_name_len &&
            input->val_len == entry->ete_val_len &&
            0 == memcmp(name_ptr, ETE_NAME(entry), input->name_len) &&
            0 == memcmp(val_ptr, ETE_VALUE(entry), input->val_len))
        {
            input->flags |= LSXPACK_VAL_MATCHED;
            return henc_calc_table_id(enc, entry);
        }

    /* Name/value match is not found, look for header: */
    input->hpack_index = lookup_static_name2(input);
    if (input->hpack_index != LSHPACK_HDR_UNKNOWN)
    {
        input->flags &= ~LSXPACK_VAL_MATCHED;
        return input->hpack_index;
    }

    /* Search by name only: */
    buckno = BUCKNO(enc->hpe_nbits, input->name_hash);
    STAILQ_FOREACH(entry, &enc->hpe_buckets[buckno].by_name, ete_next_name)
        if (input->name_hash == entry->ete_name_hash &&
            input->name_len == entry->ete_name_len &&
            0 == memcmp(name_ptr, ETE_NAME(entry), input->name_len))
        {
            input->flags &= ~LSXPACK_VAL_MATCHED;
            return henc_calc_table_id(enc, entry);
        }

    return 0;
}


static unsigned char *
henc_enc_int (unsigned char *dst, unsigned char *const end, uint32_t value,
                                                        uint8_t prefix_bits)
{
    unsigned char *const dst_orig = dst;

    /* This function assumes that at least one byte is available */
    assert(dst < end);
    if (value < (uint32_t)(1 << prefix_bits) - 1)
        *dst++ |= value;
    else
    {
        *dst++ |= (1 << prefix_bits) - 1;
        value -= (1 << prefix_bits) - 1;
        while (value >= 128)
        {
            if (dst < end)
            {
                *dst++ = (0x80 | value);
                value >>= 7;
            }
            else
                return dst_orig;
        }
        if (dst < end)
            *dst++ = value;
        else
            return dst_orig;
    }
    return dst;
}


int
lshpack_enc_huff_encode (const unsigned char *src,
    const unsigned char *const src_end, unsigned char *const dst, int dst_len)
{
    unsigned char *p_dst = dst;
    unsigned char *dst_end = p_dst + dst_len;
    uintptr_t bits;  /* OK not to initialize this variable */
    unsigned bits_used = 0, adj;
    struct encode_el cur_enc_code;

    while (src != src_end)
    {
        cur_enc_code = encode_table[*src++];
        if (bits_used + cur_enc_code.bits < sizeof(bits) * 8)
        {
            bits <<= cur_enc_code.bits;
            bits |= cur_enc_code.code;
            bits_used += cur_enc_code.bits;
            continue;
        }
        else if (p_dst + sizeof(bits) <= dst_end)
        {
            bits <<= sizeof(bits) * 8 - bits_used;
            bits_used = cur_enc_code.bits - (sizeof(bits) * 8 - bits_used);
            bits |= cur_enc_code.code >> bits_used;
#if UINTPTR_MAX == 18446744073709551615ull
            *p_dst++ = bits >> 56;
            *p_dst++ = bits >> 48;
            *p_dst++ = bits >> 40;
            *p_dst++ = bits >> 32;
#endif
            *p_dst++ = bits >> 24;
            *p_dst++ = bits >> 16;
            *p_dst++ = bits >> 8;
            *p_dst++ = bits;
            bits = cur_enc_code.code;   /* OK not to clear high bits */
        }
        else
            return -1;
    }

    adj = bits_used + (-bits_used & 7);     /* Round up to 8 */
    if (bits_used && p_dst + (adj >> 3) <= dst_end)
    {
        bits <<= -bits_used & 7;            /* Align to byte boundary */
        bits |= ((1 << (-bits_used & 7)) - 1);  /* EOF */
        switch (adj >> 3)
        {                               /* Write out */
#if UINTPTR_MAX == 18446744073709551615ull
        case 8: *p_dst++ = bits >> 56;
        /* fall through */
        case 7: *p_dst++ = bits >> 48;
        /* fall through */
        case 6: *p_dst++ = bits >> 40;
        /* fall through */
        case 5: *p_dst++ = bits >> 32;
#endif
        /* fall through */
        case 4: *p_dst++ = bits >> 24;
        /* fall through */
        case 3: *p_dst++ = bits >> 16;
        /* fall through */
        case 2: *p_dst++ = bits >> 8;
        /* fall through */
        default: *p_dst++ = bits;
        }
        return p_dst - dst;
    }
    else if (p_dst + (adj >> 3) <= dst_end)
        return p_dst - dst;
    else
        return -1;
}


#if !LS_HPACK_EMIT_TEST_CODE
static
#endif
       int
lshpack_enc_enc_str (unsigned char *const dst, size_t dst_len,
                        const unsigned char *str, lshpack_strlen_t str_len)
{
    unsigned char size_buf[4];
    unsigned char *p;
    unsigned size_len;
    int rc;

    if (dst_len > 1)
        /* We guess that the string size fits into a single byte -- meaning
         * compressed string of size 126 and smaller -- which is the normal
         * case.  Thus, we immediately write compressed string to the output
         * buffer.  If our guess is not correct, we fix it later.
         */
        rc = lshpack_enc_huff_encode(str, str + str_len, dst + 1, dst_len - 1);
    else if (dst_len == 1)
        /* Here, the call can only succeed if the string to encode is empty. */
        rc = 0;
    else
        return -1;

    /*
     * Check if need huffman encoding or not
     * Comment: (size_t)rc <= str_len   = means if same length, still use
     *                                                              Huffman
     *                     ^
     */
    if (rc > 0 && (size_t)rc <= str_len)
    {
        if (rc < 127)
        {
            *dst = 0x80 | rc;
            return 1 + rc;
        }
        size_buf[0] = 0x80;
        str_len = rc;
        str = dst + 1;
    }
    else if (str_len <= dst_len - 1)
    {
        if (str_len < 127)
        {
            *dst = str_len;
            memcpy(dst + 1, str, str_len);
            return 1 + str_len;
        }
        size_buf[0] = 0x00;
    }
    else
        return -1;

    /* The guess of one-byte size was incorrect.  Perform necessary
     * adjustments.
     */
    p = henc_enc_int(size_buf, size_buf + sizeof(size_buf), str_len, 7);
    if (p == size_buf)
        return -1;

    size_len = p - size_buf;
    assert(size_len > 1);

    /* Check if there is enough room in the output buffer for both
     * encoded size and the string.
     */
    if (size_len + str_len > dst_len)
        return -1;

    memmove(dst + size_len, str, str_len);
    memcpy(dst, size_buf, size_len);
    return size_len + str_len;
}


static void
henc_drop_oldest_entry (struct lshpack_enc *enc)
{
    struct lshpack_enc_table_entry *entry;
    unsigned buckno;

    entry = STAILQ_FIRST(&enc->hpe_all_entries);
    assert(entry);
    STAILQ_REMOVE_HEAD(&enc->hpe_all_entries, ete_next_all);
    buckno = BUCKNO(enc->hpe_nbits, entry->ete_nameval_hash);
    assert(entry == STAILQ_FIRST(&enc->hpe_buckets[buckno].by_nameval));
    STAILQ_REMOVE_HEAD(&enc->hpe_buckets[buckno].by_nameval, ete_next_nameval);
    buckno = BUCKNO(enc->hpe_nbits, entry->ete_name_hash);
    if (entry == STAILQ_FIRST(&enc->hpe_buckets[buckno].by_name))
        STAILQ_REMOVE_HEAD(&enc->hpe_buckets[buckno].by_name, ete_next_name);
    enc->hpe_cur_capacity -= DYNAMIC_ENTRY_OVERHEAD + entry->ete_name_len
                                                        + entry->ete_val_len;
    --enc->hpe_nelem;
    free(entry);
}


static void
henc_remove_overflow_entries (struct lshpack_enc *enc)
{
    while (enc->hpe_cur_capacity > enc->hpe_max_capacity)
        henc_drop_oldest_entry(enc);
}


static int
henc_grow_tables (struct lshpack_enc *enc)
{
    struct lshpack_double_enc_head *new_buckets, *heads[2];
    struct lshpack_enc_table_entry *entry;
    unsigned n, old_nbits;
    int idx;

    old_nbits = enc->hpe_nbits;
    new_buckets = malloc(sizeof(enc->hpe_buckets[0])
                                                * N_BUCKETS(old_nbits + 1));
    if (!new_buckets)
        return -1;

    for (n = 0; n < N_BUCKETS(old_nbits); ++n)
    {
        heads[0] = &new_buckets[n];
        heads[1] = &new_buckets[n + N_BUCKETS(old_nbits)];
        STAILQ_INIT(&heads[0]->by_name);
        STAILQ_INIT(&heads[1]->by_name);
        STAILQ_INIT(&heads[0]->by_nameval);
        STAILQ_INIT(&heads[1]->by_nameval);
        while ((entry = STAILQ_FIRST(&enc->hpe_buckets[n].by_name)))
        {
            STAILQ_REMOVE_HEAD(&enc->hpe_buckets[n].by_name, ete_next_name);
            idx = (BUCKNO(old_nbits + 1, entry->ete_name_hash)
                                                        >> old_nbits) & 1;
            STAILQ_INSERT_TAIL(&heads[idx]->by_name, entry, ete_next_name);
        }
        while ((entry = STAILQ_FIRST(&enc->hpe_buckets[n].by_nameval)))
        {
            STAILQ_REMOVE_HEAD(&enc->hpe_buckets[n].by_nameval,
                                                        ete_next_nameval);
            idx = (BUCKNO(old_nbits + 1, entry->ete_nameval_hash)
                                                        >> old_nbits) & 1;
            STAILQ_INSERT_TAIL(&heads[idx]->by_nameval, entry,
                                                        ete_next_nameval);
        }
    }

    free(enc->hpe_buckets);
    enc->hpe_nbits   = old_nbits + 1;
    enc->hpe_buckets = new_buckets;
    return 0;
}

#if !LS_HPACK_EMIT_TEST_CODE
static
#endif
       int
lshpack_enc_push_entry (struct lshpack_enc *enc,
                        const struct lshpack_enc_input *input)
{
    unsigned buckno;
    struct lshpack_enc_table_entry *entry;
    size_t size;

    if (enc->hpe_nelem >= N_BUCKETS(enc->hpe_nbits) / 2 &&
                                                0 != henc_grow_tables(enc))
        return -1;

    size = sizeof(*entry) + input->hdr->name.len + input->hdr->value.len;
    entry = malloc(size);
    if (!entry)
        return -1;

    entry->ete_name_hash = input->name_hash;
    entry->ete_nameval_hash = input->nameval_hash;
    entry->ete_name_len = input->hdr->name.len;
    entry->ete_val_len = input->hdr->value.len;
    entry->ete_id = enc->hpe_next_id++;
    memcpy(ETE_NAME(entry), input->hdr->name.ptr, input->hdr->name.len);
    memcpy(ETE_VALUE(entry), input->hdr->value.ptr, input->hdr->value.len);

    STAILQ_INSERT_TAIL(&enc->hpe_all_entries, entry, ete_next_all);
    buckno = BUCKNO(enc->hpe_nbits, input->nameval_hash);
    STAILQ_INSERT_TAIL(&enc->hpe_buckets[buckno].by_nameval, entry,
                                                        ete_next_nameval);
    if (input->static_table_id == 0)
    {
        buckno = BUCKNO(enc->hpe_nbits, input->name_hash);
        STAILQ_INSERT_TAIL(&enc->hpe_buckets[buckno].by_name, entry,
                                                            ete_next_name);
    }
    enc->hpe_cur_capacity += DYNAMIC_ENTRY_OVERHEAD + input->hdr->name.len
                             + input->hdr->value.len;
    ++enc->hpe_nelem;
    henc_remove_overflow_entries(enc);
    return 0;
}


#if !LS_HPACK_EMIT_TEST_CODE
static
#endif
       int
lshpack_enc_push_entry2 (struct lshpack_enc *enc,
                        const struct lsxpack_header *input)
{
    unsigned buckno;
    struct lshpack_enc_table_entry *entry;
    size_t size;

    if (enc->hpe_nelem >= N_BUCKETS(enc->hpe_nbits) / 2 &&
                                                0 != henc_grow_tables(enc))
        return -1;

    size = sizeof(*entry) + input->name_len + input->val_len;
    entry = malloc(size);
    if (!entry)
        return -1;

    entry->ete_name_hash = input->name_hash;
    entry->ete_nameval_hash = input->nameval_hash;
    entry->ete_name_len = input->name_len;
    entry->ete_val_len = input->val_len;
    entry->ete_id = enc->hpe_next_id++;
    memcpy(ETE_NAME(entry), input->buf + input->name_offset, input->name_len);
    memcpy(ETE_VALUE(entry), input->buf + input->val_offset, input->val_len);

    STAILQ_INSERT_TAIL(&enc->hpe_all_entries, entry, ete_next_all);
    buckno = BUCKNO(enc->hpe_nbits, input->nameval_hash);
    STAILQ_INSERT_TAIL(&enc->hpe_buckets[buckno].by_nameval, entry,
                                                        ete_next_nameval);
    if (input->hpack_index == LSHPACK_HDR_UNKNOWN)
    {
        buckno = BUCKNO(enc->hpe_nbits, input->name_hash);
        STAILQ_INSERT_TAIL(&enc->hpe_buckets[buckno].by_name, entry,
                                                            ete_next_name);
    }
    enc->hpe_cur_capacity += DYNAMIC_ENTRY_OVERHEAD + input->name_len
                             + input->val_len;
    ++enc->hpe_nelem;
    henc_remove_overflow_entries(enc);
    return 0;
}


static void
henc_resize_history (struct lshpack_enc *enc)
{
    uint32_t *hist_buf;
    unsigned hist_size, first, count, i, j;

    hist_size = henc_hist_size(enc->hpe_max_capacity);

    if (hist_size == enc->hpe_hist_size)
        return;

    if (hist_size == 0)
    {
        free(enc->hpe_hist_buf);
        enc->hpe_hist_buf = NULL;
        enc->hpe_hist_size = 0;
        enc->hpe_hist_idx = 0;
        enc->hpe_hist_wrapped = 0;
        return;
    }

    hist_buf = malloc(sizeof(hist_buf[0]) * (hist_size + 1));
    if (!hist_buf)
        return;

    if (enc->hpe_hist_wrapped)
    {
        first = (enc->hpe_hist_idx + 1) % enc->hpe_hist_size;
        count = enc->hpe_hist_size;
    }
    else
    {
        first = 0;
        count = enc->hpe_hist_idx;
    }
    for (i = 0, j = 0; count > 0 && j < hist_size; ++i, ++j, --count)
        hist_buf[j] = enc->hpe_hist_buf[ (first + i) % enc->hpe_hist_size ];
    enc->hpe_hist_size = hist_size;
    enc->hpe_hist_idx = j % hist_size;
    enc->hpe_hist_wrapped = enc->hpe_hist_idx == 0;
    free(enc->hpe_hist_buf);
    enc->hpe_hist_buf = hist_buf;
}


/* Returns true if `nameval_hash' was already in history, false otherwise. */
static int
henc_hist_add (struct lshpack_enc *enc, uint32_t nameval_hash)
{
    unsigned last;
    uint32_t *p;

    if (enc->hpe_hist_wrapped)
        last = enc->hpe_hist_size;
    else
        last = enc->hpe_hist_idx;

    enc->hpe_hist_buf[ last ] = nameval_hash;
    for (p = enc->hpe_hist_buf; *p != nameval_hash; ++p)
        ;
    enc->hpe_hist_buf[ enc->hpe_hist_idx ] = nameval_hash;
    enc->hpe_hist_idx = (enc->hpe_hist_idx + 1) % enc->hpe_hist_size;
    enc->hpe_hist_wrapped |= enc->hpe_hist_idx == 0;

    return p < enc->hpe_hist_buf + last;
}


unsigned char *
lshpack_enc_encode (struct lshpack_enc *enc, unsigned char *dst,
        unsigned char *dst_end, int hpack_idx,
        const lshpack_header_t *header, int indexed_type)
{
    //indexed_type: 0, Add, 1,: without, 2: never
    static const char indexed_prefix_number[] = {0x40, 0x00, 0x10};
    unsigned char *const dst_org = dst;
    int rc;
    unsigned table_id;
    struct lshpack_enc_input input;

    assert(indexed_type >= 0 && indexed_type <= 2);

    if (dst_end <= dst)
        return dst_org;

    memset(&input, 0, sizeof(input));
    input.hdr = header;
    if (hpack_idx > 0)
        input.static_table_id = hpack_idx;

    table_id = henc_find_table_id(enc, &input);

    if (enc->hpe_hist_buf)
    {
        rc = henc_hist_add(enc, input.nameval_hash);
        if (!rc && enc->hpe_hist_wrapped && indexed_type == 0)
            indexed_type = 1;
    }

    if (table_id > 0)
    {
        if (input.val_matched)
        {
            *dst = 0x80;
            dst = henc_enc_int(dst, dst_end, table_id, 7);
            /* No need to check return value: we pass it up as-is because
             * the behavior is the same.
             */
            return dst;
        }
        else
        {
            *dst = indexed_prefix_number[indexed_type];
            dst = henc_enc_int(dst, dst_end, table_id,
                                            ((indexed_type == 0) ? 6 : 4));
            if (dst == dst_org)
                return dst_org;
        }
    }
    else
    {
        *dst++ = indexed_prefix_number[indexed_type];
        rc = lshpack_enc_enc_str(dst, dst_end - dst,
                                 (const unsigned char *)header->name.ptr,
                                 header->name.len);
        if (rc < 0)
            return dst_org; //Failed to enc this header, return unchanged ptr.
        dst += rc;
    }

    rc = lshpack_enc_enc_str(dst, dst_end - dst,
                             (const unsigned char *)header->value.ptr,
                             header->value.len);
    if (rc < 0)
        return dst_org; //Failed to enc this header, return unchanged ptr.
    dst += rc;

    if (indexed_type == 0)
    {
        rc = lshpack_enc_push_entry(enc, &input);
        if (rc != 0)
            return dst_org; //Failed to enc this header, return unchanged ptr.
    }

    return dst;
}


unsigned char *
lshpack_encode (struct lshpack_enc *enc, unsigned char *dst,
        unsigned char *dst_end, lsxpack_header_t *input)
{
    //indexed_type: 0, Add, 1,: without, 2: never
    static const char indexed_prefix_number[] = {0x40, 0x00, 0x10};
    unsigned char *const dst_org = dst;
    int rc;
    unsigned table_id;
    int indexed_type = 0;

    if (dst_end <= dst)
        return dst_org;

    if (input->flags & LSXPACK_NEVER_INDEX)
        indexed_type = 2;

    table_id = henc_find_table_id2(enc, input);

    if (enc->hpe_hist_buf)
    {
        rc = henc_hist_add(enc, input->nameval_hash);
        if (!rc && enc->hpe_hist_wrapped && indexed_type == 0)
            indexed_type = 1;
    }

    if (table_id > 0)
    {
        if (input->flags & LSXPACK_VAL_MATCHED)
        {
            *dst = 0x80;
            dst = henc_enc_int(dst, dst_end, table_id, 7);
            /* No need to check return value: we pass it up as-is because
             * the behavior is the same.
             */
            return dst;
        }
        else
        {
            *dst = indexed_prefix_number[indexed_type];
            dst = henc_enc_int(dst, dst_end, table_id,
                                            ((indexed_type == 0) ? 6 : 4));
            if (dst == dst_org)
                return dst_org;
        }
    }
    else
    {
        *dst++ = indexed_prefix_number[indexed_type];
        rc = lshpack_enc_enc_str(dst, dst_end - dst,
                                 (const unsigned char *)input->buf + input->name_offset,
                                 input->name_len);
        if (rc < 0)
            return dst_org; //Failed to enc this header, return unchanged ptr.
        dst += rc;
    }

    rc = lshpack_enc_enc_str(dst, dst_end - dst,
                             (const unsigned char *)input->buf + input->val_offset,
                             input->val_len);
    if (rc < 0)
        return dst_org; //Failed to enc this header, return unchanged ptr.
    dst += rc;

    if (indexed_type == 0)
    {
        rc = lshpack_enc_push_entry2(enc, input);
        if (rc != 0)
            return dst_org; //Failed to enc this header, return unchanged ptr.
    }

    return dst;
}


void
lshpack_enc_set_max_capacity (struct lshpack_enc *enc, unsigned max_capacity)
{
    enc->hpe_max_capacity = max_capacity;
    henc_remove_overflow_entries(enc);
    if (lshpack_enc_hist_used(enc))
        henc_resize_history(enc);
}

#if LS_HPACK_EMIT_TEST_CODE
void
lshpack_enc_iter_init (struct lshpack_enc *enc, void **iter)
{
    *iter = STAILQ_FIRST(&enc->hpe_all_entries);
}


/* Returns 0 if entry is found */
int
lshpack_enc_iter_next (struct lshpack_enc *enc, void **iter,
                                        struct enc_dyn_table_entry *retval)
{
    const struct lshpack_enc_table_entry *entry;

    entry = *iter;
    if (!entry)
        return -1;

    *iter = STAILQ_NEXT(entry, ete_next_all);

    retval->name = ETE_NAME(entry);
    retval->value = ETE_VALUE(entry);
    retval->name_len = entry->ete_name_len;
    retval->value_len = entry->ete_val_len;
    retval->entry_id = henc_calc_table_id(enc, entry);
    return 0;
}
#endif


/* Dynamic table entry: */
struct dec_table_entry
{
    uint16_t    dte_name_len;
    uint16_t    dte_val_len;
    uint8_t     dte_name_idx;
    char        dte_buf[0];     /* Contains both name and value */
};

#define DTE_NAME(dte) ((dte)->dte_buf)
#define DTE_VALUE(dte) (&(dte)->dte_buf[(dte)->dte_name_len])

enum
{
    HPACK_HUFFMAN_FLAG_ACCEPTED = 0x01,
    HPACK_HUFFMAN_FLAG_SYM = 0x02,
    HPACK_HUFFMAN_FLAG_FAIL = 0x04,
};

struct decode_status
{
    uint8_t state;
    uint8_t eos;
};


void
lshpack_dec_init (struct lshpack_dec *dec)
{
    memset(dec, 0, sizeof(*dec));
    dec->hpd_max_capacity = INITIAL_DYNAMIC_TABLE_SIZE;
    dec->hpd_cur_max_capacity = INITIAL_DYNAMIC_TABLE_SIZE;
    lshpack_arr_init(&dec->hpd_dyn_table);
}


void
lshpack_dec_cleanup (struct lshpack_dec *dec)
{
    uintptr_t val;

    while (lshpack_arr_count(&dec->hpd_dyn_table) > 0)
    {
        val = lshpack_arr_pop(&dec->hpd_dyn_table);
        free((struct dec_table_entry *) val);
    }
    lshpack_arr_cleanup(&dec->hpd_dyn_table);
}


#if !LS_HPACK_EMIT_TEST_CODE
static
#endif
       int
lshpack_dec_dec_int (const unsigned char **src, const unsigned char *src_end,
                                        uint8_t prefix_bits, uint32_t *value)
{
    uint32_t B, M;
    uint8_t prefix_max = (1 << prefix_bits) - 1;

    *value = (*(*src)++ & prefix_max);

    if (*value < prefix_max)
        return 0;

    /* To optimize the loop for the normal case, the overflow is checked
     * outside the loop.  The decoder is limited to 28-bit integer values,
     * which is far above limitations imposed by the APIs (16-bit integers).
     */
    M = 0;
    do
    {
        if ((*src) >= src_end)
            return -1;
        B = *(*src)++;
        *value = *value + ((B & 0x7f) << M);
        M += 7;
    }
    while (B & 0x80);

    return -(M > sizeof(*value) * 8);
}


static void
hdec_drop_oldest_entry (struct lshpack_dec *dec)
{
    struct dec_table_entry *entry;
    entry = (void *) lshpack_arr_shift(&dec->hpd_dyn_table);
    dec->hpd_cur_capacity -= DYNAMIC_ENTRY_OVERHEAD + entry->dte_name_len
                                                        + entry->dte_val_len;
    ++dec->hpd_state;
    free(entry);
}


static void
hdec_remove_overflow_entries (struct lshpack_dec *dec)
{
    while (dec->hpd_cur_capacity > dec->hpd_cur_max_capacity)
        hdec_drop_oldest_entry(dec);
}


static void
hdec_update_max_capacity (struct lshpack_dec *dec, uint32_t new_capacity)
{
    dec->hpd_cur_max_capacity = new_capacity;
    hdec_remove_overflow_entries(dec);
}


void
lshpack_dec_set_max_capacity (struct lshpack_dec *dec, unsigned max_capacity)
{
    dec->hpd_max_capacity = max_capacity;
    hdec_update_max_capacity(dec, max_capacity);
}


static unsigned char *
hdec_huff_dec4bits (uint8_t src_4bits, unsigned char *dst,
                                        struct decode_status *status)
{
    const struct decode_el cur_dec_code =
        decode_tables[status->state][src_4bits];
    if (cur_dec_code.flags & HPACK_HUFFMAN_FLAG_FAIL) {
        return NULL; //failed
    }
    if (cur_dec_code.flags & HPACK_HUFFMAN_FLAG_SYM)
    {
        *dst = cur_dec_code.sym;
        dst++;
    }

    status->state = cur_dec_code.state;
    status->eos = ((cur_dec_code.flags & HPACK_HUFFMAN_FLAG_ACCEPTED) != 0);
    return dst;
}


int
lshpack_dec_huff_decode_full (const unsigned char *src, int src_len,
                                            unsigned char *dst, int dst_len)
{
    const unsigned char *p_src = src;
    const unsigned char *const src_end = src + src_len;
    unsigned char *p_dst = dst;
    unsigned char *dst_end = dst + dst_len;
    struct decode_status status = { 0, 1 };

    while (p_src != src_end)
    {
        if (p_dst == dst_end)
            return -2;
        if ((p_dst = hdec_huff_dec4bits(*p_src >> 4, p_dst, &status))
                == NULL)
            return -1;
        if (p_dst == dst_end)
            return -2;
        if ((p_dst = hdec_huff_dec4bits(*p_src & 0xf, p_dst, &status))
                == NULL)
            return -1;
        ++p_src;
    }

    if (!status.eos)
        return -1;

    return p_dst - dst;
}


int
lshpack_dec_huff_decode (const unsigned char *src, int src_len,
                                            unsigned char *dst, int dst_len);


//reutrn the length in the dst, also update the src
#if !LS_HPACK_EMIT_TEST_CODE
static
#endif
       int
hdec_dec_str (unsigned char *dst, size_t dst_len, const unsigned char **src,
        const unsigned char *src_end)
{
    if ((*src) == src_end)
        return 0;

    int is_huffman = (*(*src) & 0x80);
    uint32_t len;
    if (0 != lshpack_dec_dec_int(src, src_end, 7, &len))
        return -2;  //wrong int

    int ret = 0;
    if ((uint32_t)(src_end - (*src)) < len) {
        return -2;  //wrong int
    }

    if (is_huffman)
    {
        ret = lshpack_dec_huff_decode(*src, len, dst, dst_len);
        if (ret < 0)
            return -3; //Wrong code

        (*src) += len;
    }
    else
    {
        if (dst_len < len)
            ret = -3;  //dst not enough space
        else
        {
            memcpy(dst, (*src), len);
            (*src) += len;
            ret = len;
        }
    }

    return ret;
}


/* hpd_dyn_table is a dynamic array.  New entries are pushed onto it,
 * while old entries are shifted from it.
 */
static struct dec_table_entry *
hdec_get_table_entry (struct lshpack_dec *dec, uint32_t index)
{
    uintptr_t val;

    index -= HPACK_STATIC_TABLE_SIZE;
    if (index == 0 || index > lshpack_arr_count(&dec->hpd_dyn_table))
        return NULL;

    index = lshpack_arr_count(&dec->hpd_dyn_table) - index;
    val = lshpack_arr_get(&dec->hpd_dyn_table, index);
    return (struct dec_table_entry *) val;
}


#if !LS_HPACK_EMIT_TEST_CODE
static
#endif
       int
lshpack_dec_push_entry (struct lshpack_dec *dec, uint8_t name_idx, const char *name,
                        uint16_t name_len, const char *val, uint16_t val_len)
{
    struct dec_table_entry *entry;
    size_t size;

    size = sizeof(*entry) + name_len + val_len;
    entry = malloc(size);
    if (!entry)
        return -1;

    if (0 != lshpack_arr_push(&dec->hpd_dyn_table, (uintptr_t) entry))
    {
        free(entry);
        return -1;
    }
    ++dec->hpd_state;
    dec->hpd_cur_capacity += DYNAMIC_ENTRY_OVERHEAD + name_len + val_len;
    entry->dte_name_len = name_len;
    entry->dte_val_len = val_len;
    entry->dte_name_idx = name_idx;
    memcpy(DTE_NAME(entry), name, name_len);
    memcpy(DTE_VALUE(entry), val, val_len);
    return 0;
}


int
lshpack_dec_decode (struct lshpack_dec *dec,
    const unsigned char **src, const unsigned char *src_end,
    char *dst, char *const dst_end, uint16_t *name_len, uint16_t *val_len,
    uint32_t *name_idx)
{
    struct dec_table_entry *entry;
    uint32_t index, new_capacity;
    int indexed_type, len;

    if ((*src) == src_end)
        return -1;

    while ((*(*src) & 0xe0) == 0x20)    //001 xxxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 5, &new_capacity))
            return -1;
        if (new_capacity > dec->hpd_max_capacity)
            return -1;
        hdec_update_max_capacity(dec, new_capacity);
        if (*src == src_end)
            return -1;
    }

    /* lshpack_dec_dec_int() sets `index' and advances `src'.  If we do not
     * call it, we set `index' and advance `src' ourselves:
     */
    if (*(*src) & 0x80) //1 xxxxxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 7, &index))
            return -1;
        if (index == 0)
            return -1;
        indexed_type = 3; //need to parse value
    }
    else if (*(*src) > 0x40) //01 xxxxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 6, &index))
            return -1;

        indexed_type = 0;
    }
    else if (*(*src) == 0x40) //custmized //0100 0000
    {
        indexed_type = 0;
        index = 0;
        ++(*src);
    }

    //Never indexed
    else if (*(*src) == 0x10)  //00010000
    {
        indexed_type = 2;
        index = 0;
        ++(*src);
    }
    else if ((*(*src) & 0xf0) == 0x10)  //0001 xxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 4, &index))
            return -1;

        indexed_type = 2;
    }

    //without indexed
    else if (*(*src) == 0x00)  //0000 0000
    {
        indexed_type = 1;
        index = 0;
        ++(*src);
    }
    else // 0000 xxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 4, &index))
            return -1;

        indexed_type = 1;
    }
    *name_idx = index;

    char *const name = dst;
    if (index > 0)
    {
        if (index <= HPACK_STATIC_TABLE_SIZE) //static table
        {
            if (static_table[index - 1].name_len > (unsigned) (dst_end - dst))
                return -1;
            *name_len = static_table[index - 1].name_len;
            memcpy(name, static_table[index - 1].name, *name_len);
            if (indexed_type == 3)
            {
                if (static_table[index - 1].name_len +
                    static_table[index - 1].val_len > (unsigned)(dst_end - dst))
                    return -1;
                *val_len = static_table[index - 1].val_len;
                memcpy(name + *name_len, static_table[index - 1].val, *val_len);
                return 0;
            }
        }
        else
        {
            entry = hdec_get_table_entry(dec, index);
            if (entry == NULL)
                return -1;
            if (entry->dte_name_len > (unsigned) (dst_end - dst))
                return -1;

            *name_len = entry->dte_name_len;
            memcpy(name, DTE_NAME(entry), *name_len);
            if (entry->dte_name_idx)
                *name_idx = entry->dte_name_idx;
            else
                *name_idx = 0;
            if (indexed_type == 3)
            {
                if (entry->dte_name_len + entry->dte_val_len
                                                > (unsigned) (dst_end - dst))
                    return -1;
                *val_len = entry->dte_val_len;
                memcpy(name + *name_len, DTE_VALUE(entry), *val_len);
                return 0;
            }
        }
    }
    else
    {
        len = hdec_dec_str((unsigned char *)name, dst_end - dst, src, src_end);
        if (len < 0)
            return len; //error
        if (len > UINT16_MAX)
            return -2;
        *name_len = len;
    }

    len = hdec_dec_str((unsigned char *)name + *name_len,
                                    dst_end - dst - *name_len, src, src_end);
    if (len < 0)
        return len; //error
    if (len > UINT16_MAX)
        return -2;
    *val_len = len;

    if (indexed_type == 0)
    {
        if (index > HPACK_STATIC_TABLE_SIZE)
            index = 0;
        if (0 != lshpack_dec_push_entry(dec, index, name, *name_len,
                                            name + *name_len, *val_len))
            return -1;  //error
    }

    return 0;
}


static inline int
lshpack_dec_copy_value(lsxpack_header_t *output, char *dest, const char *val,
                       unsigned val_len)
{
    if (val_len + 2 > (unsigned)output->val_len)
        return -1;
    output->val_offset = output->name_offset + output->name_len + 2;
    assert(dest == output->buf + output->val_offset);
    output->val_len = val_len;
    memcpy(dest, val, output->val_len);
    dest += output->val_len;
    *dest++ = '\r';
    *dest++ = '\n';
    return 0;
}

enum
{
    LSHPACK_ADD_INDEX = 0,
    LSHPACK_NO_INDEX  = 1,
    LSHPACK_NEVER_INDEX = 2,
    LSHPACK_VAL_INDEX = 3,
};

int
lshpack_decode (struct lshpack_dec *dec,
    const unsigned char **src, const unsigned char *src_end,
    lsxpack_header_t *output)
{
    struct dec_table_entry *entry;
    uint32_t index, new_capacity;
    int indexed_type, len;

    if ((*src) == src_end)
        return -1;

    while ((*(*src) & 0xe0) == 0x20)    //001 xxxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 5, &new_capacity))
            return -1;
        if (new_capacity > dec->hpd_max_capacity)
            return -1;
        hdec_update_max_capacity(dec, new_capacity);
        if (*src == src_end)
            return -1;
    }

    /* lshpack_dec_dec_int() sets `index' and advances `src'.  If we do not
     * call it, we set `index' and advance `src' ourselves:
     */
    if (*(*src) & 0x80) //1 xxxxxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 7, &index))
            return -1;
        if (index == 0)
            return -1;
        indexed_type = LSHPACK_VAL_INDEX; //need to parse value
    }
    else if (*(*src) > 0x40) //01 xxxxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 6, &index))
            return -1;

        indexed_type = LSHPACK_ADD_INDEX;
    }
    else if (*(*src) == 0x40) //custmized //0100 0000
    {
        indexed_type = LSHPACK_ADD_INDEX;
        index = LSHPACK_HDR_UNKNOWN;
        ++(*src);
    }

    //Never indexed
    else if (*(*src) == 0x10)  //00010000
    {
        indexed_type = LSHPACK_NEVER_INDEX;
        output->flags |= LSXPACK_NEVER_INDEX;
        index = LSHPACK_HDR_UNKNOWN;
        ++(*src);
    }
    else if ((*(*src) & 0xf0) == 0x10)  //0001 xxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 4, &index))
            return -1;

        indexed_type = LSHPACK_NEVER_INDEX;
        output->flags |= LSXPACK_NEVER_INDEX;
    }

    //without indexed
    else if (*(*src) == 0x00)  //0000 0000
    {
        indexed_type = LSHPACK_NO_INDEX;
        index = LSHPACK_HDR_UNKNOWN;
        ++(*src);
    }
    else // 0000 xxxx
    {
        if (0 != lshpack_dec_dec_int(src, src_end, 4, &index))
            return -1;

        indexed_type = LSHPACK_NO_INDEX;
    }
    if (index != LSHPACK_HDR_UNKNOWN && index <= LSHPACK_HDR_WWW_AUTHENTICATE)
    {
        output->hpack_index = index;
        output->flags |= LSXPACK_HPACK_IDX;
    }

    char *name = output->buf + output->name_offset;
    if (index > 0)
    {
        if (index <= HPACK_STATIC_TABLE_SIZE) //static table
        {
            if (static_table[index - 1].name_len + 2 > (unsigned) output->val_len)
                return -1;
            output->val_len -= static_table[index - 1].name_len + 2;
            output->name_len = static_table[index - 1].name_len;
            memcpy(name, static_table[index - 1].name, output->name_len);
            name += output->name_len;
            *name++ = ':';
            *name++ = ' ';

            if (indexed_type == LSHPACK_VAL_INDEX)
            {
                return lshpack_dec_copy_value(output, name,
                                              static_table[index - 1].val,
                                              static_table[index - 1].val_len);
            }
        }
        else
        {
            entry = hdec_get_table_entry(dec, index);
            if (entry == NULL)
                return -1;
            if (entry->dte_name_len + 2 > (unsigned)output->val_len)
                return -1;

            output->val_len -= entry->dte_name_len + 2;
            output->name_len = entry->dte_name_len;
            memcpy(name, DTE_NAME(entry), output->name_len);
            name += output->name_len;
            *name++ = ':';
            *name++ = ' ';

            if (entry->dte_name_idx)
                output->hpack_index = index;
            else
                output->hpack_index = LSHPACK_HDR_UNKNOWN;
            output->flags |= LSXPACK_HPACK_IDX;

            if (indexed_type == LSHPACK_VAL_INDEX)
            {
                return lshpack_dec_copy_value(output, name, DTE_VALUE(entry),
                                              entry->dte_val_len);
            }
        }
    }
    else
    {
        len = hdec_dec_str((unsigned char *)name, output->val_len, src, src_end);
        if (len < 0)
            return len; //error
        if (len > UINT16_MAX)
            return -2;
        output->name_len = len;
        name += output->name_len;
        *name++ = ':';
        *name++ = ' ';
        output->val_len -= len + 2;
    }

    len = hdec_dec_str((unsigned char *)name, output->val_len, src, src_end);
    if (len < 0)
        return len; //error
    if (len > UINT16_MAX)
        return -2;
    output->val_len = len;
    memcpy(name + len, "\r\n", 2);

    if (indexed_type == LSHPACK_ADD_INDEX)
    {
        if (index > HPACK_STATIC_TABLE_SIZE)
            index = 0;
        if (0 != lshpack_dec_push_entry(dec, index, name - output->name_len - 2,
                            output->name_len, name , output->val_len))
            return -1;  //error
    }

    return 0;
}


#define SHORTEST_CODE 5


/* The decoder is optimized for the common case.  Most of the time, we decode
 * data whose encoding is 16 bits or shorter.  This lets us use a 64 KB table
 * indexed by two bytes of input and outputs 1, 2, or 3 bytes at a time.
 *
 * In the case a longer code is encoutered, we fall back to the original
 * Huffman decoder that supports all code lengths.
 */
int
lshpack_dec_huff_decode (const unsigned char *src, int src_len,
                                            unsigned char *dst, int dst_len)
{
    unsigned char *const orig_dst = dst;
    const unsigned char *const src_end = src + src_len;
    unsigned char *const dst_end = dst + dst_len;
    uintptr_t buf;              /* OK not to initialize */
    unsigned avail_bits, len;
    struct hdec hdec;
    uint16_t idx;
    int r;

    avail_bits = 0;
    while (1)
    {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
        if (src + sizeof(buf) <= src_end)
        {
            len = (sizeof(buf) * 8 - avail_bits) >> 3;
            avail_bits += len << 3;
            switch (len)
            {
#if UINTPTR_MAX == 18446744073709551615ull
            case 8:
                buf <<= 8;
                buf |= (uintptr_t) *src++;
                /* fall through */
            case 7:
                buf <<= 8;
                buf |= (uintptr_t) *src++;
                /* fall through */
            default:
                buf <<= 48;
                buf |= (uintptr_t) *src++ << 40;
                buf |= (uintptr_t) *src++ << 32;
                buf |= (uintptr_t) *src++ << 24;
                buf |= (uintptr_t) *src++ << 16;
#else
                /* fall through */
            case 4:
                buf <<= 8;
                buf |= (uintptr_t) *src++;
                /* fall through */
            case 3:
                buf <<= 8;
                buf |= (uintptr_t) *src++;
                /* fall through */
            default:
                buf <<= 16;
#endif
                buf |= (uintptr_t) *src++ <<  8;
                buf |= (uintptr_t) *src++ <<  0;
            }
        }
        else if (src < src_end)
            do
            {
                buf <<= 8;
                buf |= (uintptr_t) *src++;
                avail_bits += 8;
            }
            while (src < src_end && avail_bits <= sizeof(buf) * 8 - 8);
        else
            break;  /* Normal case terminating condition: out of input */
#pragma GCC diagnostic pop

        if (dst_end - dst >= (ptrdiff_t) (8 * sizeof(buf) / SHORTEST_CODE)
                                                            && avail_bits >= 16)
        {
            /* Fast path: don't check destination bounds */
            do
            {
                idx = buf >> (avail_bits - 16);
                hdec = hdecs[idx];
                dst[0] = hdec.out[0];
                dst[1] = hdec.out[1];
                dst[2] = hdec.out[2];
                dst += hdec.lens & 3;
                avail_bits -= hdec.lens >> 2;
            }
            while (avail_bits >= 16 && hdec.lens);
            if (avail_bits < 16)
                continue;
            goto slow_path;
        }
        else
            while (avail_bits >= 16)
            {
                idx = buf >> (avail_bits - 16);
                hdec = hdecs[idx];
                len = hdec.lens & 3;
                if (len && dst + len <= dst_end)
                {
                    switch (len)
                    {
                    case 3:
                        *dst++ = hdec.out[0];
                        *dst++ = hdec.out[1];
                        *dst++ = hdec.out[2];
                        break;
                    case 2:
                        *dst++ = hdec.out[0];
                        *dst++ = hdec.out[1];
                        break;
                    default:
                        *dst++ = hdec.out[0];
                        break;
                    }
                    avail_bits -= hdec.lens >> 2;
                }
                else if (dst + len > dst_end)
                    return -2;
                else
                    goto slow_path;
            }
    }

    if (avail_bits >= SHORTEST_CODE)
    {
        idx = buf << (16 - avail_bits);
        idx |= (1 << (16 - avail_bits)) - 1;    /* EOF */
        if (idx == 0xFFFF && avail_bits < 8)
            goto end;
        /* If a byte or more of input is left, this mean there is a valid
         * encoding, not just EOF.
         */
        hdec = hdecs[idx];
        len = hdec.lens & 3;
        if ((hdec.lens >> 2) > avail_bits)
            return -1;
        if (len && dst + len <= dst_end)
        {
            switch (len)
            {
            case 3:
                *dst++ = hdec.out[0];
                *dst++ = hdec.out[1];
                *dst++ = hdec.out[2];
                break;
            case 2:
                *dst++ = hdec.out[0];
                *dst++ = hdec.out[1];
                break;
            default:
                *dst++ = hdec.out[0];
                break;
            }
            avail_bits -= hdec.lens >> 2;
        }
        else if (dst + len > dst_end)
            return -2;
        else
            /* This must be an invalid code, otherwise it would have fit */
            return -1;
    }

    if (avail_bits > 0)
    {
        if (((1u << avail_bits) - 1) != (buf & ((1u << avail_bits) - 1)))
            return -1;  /* Not EOF as expected */
    }

  end:
    return dst - orig_dst;

  slow_path:
    /* Find previous byte boundary and finish decoding thence. */
    while ((avail_bits & 7) && dst > orig_dst)
        avail_bits += encode_table[ *--dst ].bits;
    assert((avail_bits & 7) == 0);
    src -= avail_bits >> 3;
    r = lshpack_dec_huff_decode_full(src, src_end - src, dst, dst_end - dst);
    if (r >= 0)
        return dst - orig_dst + r;
    else
        return r;
}
