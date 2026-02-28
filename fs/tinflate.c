/*
 * tinflate - tiny inflate (DEFLATE decompression)
 * Adapted for Ark kernel: no stdlib, no assert.
 * Original: https://github.com/jibsen/tinf (Jorgen Ibsen)
 */
#include "ark/tinf.h"
#include "ark/types.h"

#define assert(x) ((void)0)

/* -- Internal data structures -- */
struct tinf_tree {
    u16 counts[16];
    u16 symbols[288];
    int max_sym;
};

struct tinf_data {
    const u8 *source;
    const u8 *source_end;
    u32 tag;
    int bitcount;
    int overflow;
    u8 *dest_start;
    u8 *dest;
    u8 *dest_end;
    struct tinf_tree ltree;
    struct tinf_tree dtree;
};

static u32 read_le16(const u8 *p) {
    return (u32)p[0] | ((u32)p[1] << 8);
}

static void tinf_build_fixed_trees(struct tinf_tree *lt, struct tinf_tree *dt) {
    int i;
    for (i = 0; i < 16; ++i) lt->counts[i] = 0;
    lt->counts[7] = 24;
    lt->counts[8] = 152;
    lt->counts[9] = 112;
    for (i = 0; i < 24; ++i) lt->symbols[i] = 256 + i;
    for (i = 0; i < 144; ++i) lt->symbols[24 + i] = i;
    for (i = 0; i < 8; ++i) lt->symbols[24 + 144 + i] = 280 + i;
    for (i = 0; i < 112; ++i) lt->symbols[24 + 144 + 8 + i] = 144 + i;
    lt->max_sym = 285;
    for (i = 0; i < 16; ++i) dt->counts[i] = 0;
    dt->counts[5] = 32;
    for (i = 0; i < 32; ++i) dt->symbols[i] = i;
    dt->max_sym = 29;
}

static int tinf_build_tree(struct tinf_tree *t, const u8 *lengths, u32 num) {
    u16 offs[16];
    u32 i, num_codes, available;
    for (i = 0; i < 16; ++i) t->counts[i] = 0;
    t->max_sym = -1;
    for (i = 0; i < num; ++i) {
        if (lengths[i]) {
            t->max_sym = (int)i;
            t->counts[lengths[i]]++;
        }
    }
    for (available = 1, num_codes = 0, i = 0; i < 16; ++i) {
        u32 used = t->counts[i];
        if (used > available) return TINF_DATA_ERROR;
        available = 2 * (available - used);
        offs[i] = (u16)num_codes;
        num_codes += used;
    }
    if ((num_codes > 1 && available > 0) || (num_codes == 1 && t->counts[1] != 1))
        return TINF_DATA_ERROR;
    for (i = 0; i < num; ++i) {
        if (lengths[i])
            t->symbols[offs[lengths[i]]++] = (u16)i;
    }
    if (num_codes == 1) {
        t->counts[1] = 2;
        t->symbols[1] = (u16)(t->max_sym + 1);
    }
    return TINF_OK;
}

static void tinf_refill(struct tinf_data *d, int num) {
    while (d->bitcount < num) {
        if (d->source != d->source_end)
            d->tag |= (u32)*d->source++ << d->bitcount;
        else
            d->overflow = 1;
        d->bitcount += 8;
    }
}

static u32 tinf_getbits_no_refill(struct tinf_data *d, int num) {
    u32 bits = d->tag & ((1UL << num) - 1);
    d->tag >>= num;
    d->bitcount -= num;
    return bits;
}

static u32 tinf_getbits(struct tinf_data *d, int num) {
    tinf_refill(d, num);
    return tinf_getbits_no_refill(d, num);
}

static u32 tinf_getbits_base(struct tinf_data *d, int num, int base) {
    return (u32)(base + (num ? tinf_getbits(d, num) : 0));
}

static int tinf_decode_symbol(struct tinf_data *d, const struct tinf_tree *t) {
    int base = 0, offs = 0, len;
    for (len = 1; ; ++len) {
        offs = 2 * offs + (int)tinf_getbits(d, 1);
        if (offs < (int)t->counts[len]) break;
        base += t->counts[len];
        offs -= t->counts[len];
    }
    return (int)t->symbols[base + offs];
}

static int tinf_decode_trees(struct tinf_data *d, struct tinf_tree *lt, struct tinf_tree *dt) {
    u8 lengths[288 + 32];
    static const u8 clcidx[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };
    u32 hlit, hdist, hclen, i, num, length;
    int res, sym;
    hlit = tinf_getbits_base(d, 5, 257);
    hdist = tinf_getbits_base(d, 5, 1);
    hclen = tinf_getbits_base(d, 4, 4);
    if (hlit > 286 || hdist > 30) return TINF_DATA_ERROR;
    for (i = 0; i < 19; ++i) lengths[i] = 0;
    for (i = 0; i < hclen; ++i)
        lengths[clcidx[i]] = (u8)tinf_getbits(d, 3);
    res = tinf_build_tree(lt, lengths, 19);
    if (res != TINF_OK) return res;
    if (lt->max_sym == -1) return TINF_DATA_ERROR;
    for (num = 0; num < hlit + hdist; ) {
        sym = tinf_decode_symbol(d, lt);
        if (sym > lt->max_sym) return TINF_DATA_ERROR;
        switch (sym) {
            case 16:
                if (num == 0) return TINF_DATA_ERROR;
                sym = lengths[num - 1];
                length = tinf_getbits_base(d, 2, 3);
                break;
            case 17:
                sym = 0;
                length = tinf_getbits_base(d, 3, 3);
                break;
            case 18:
                sym = 0;
                length = tinf_getbits_base(d, 7, 11);
                break;
            default:
                length = 1;
                break;
        }
        if (length > hlit + hdist - num) return TINF_DATA_ERROR;
        while (length--) lengths[num++] = (u8)sym;
    }
    if (lengths[256] == 0) return TINF_DATA_ERROR;
    res = tinf_build_tree(lt, lengths, hlit);
    if (res != TINF_OK) return res;
    res = tinf_build_tree(dt, lengths + hlit, hdist);
    if (res != TINF_OK) return res;
    return TINF_OK;
}

/* Length/distance tables */
static const u8 length_bits[30] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0,127
};
static const u16 length_base[30] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258,0
};
static const u8 dist_bits[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13
};
static const u16 dist_base[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577
};

static int tinf_inflate_block_data(struct tinf_data *d, struct tinf_tree *lt, struct tinf_tree *dt) {
    for (;;) {
        int sym = tinf_decode_symbol(d, lt);
        if (d->overflow) return TINF_DATA_ERROR;
        if (sym < 256) {
            if (d->dest == d->dest_end) return TINF_BUF_ERROR;
            *d->dest++ = (u8)sym;
        } else {
            u32 length, offs;
            int i, dist;
            if (sym == 256) return TINF_OK;
            if (sym > lt->max_sym || sym - 257 > 28 || dt->max_sym == -1) return TINF_DATA_ERROR;
            sym -= 257;
            length = tinf_getbits_base(d, (int)length_bits[sym], (int)length_base[sym]);
            dist = tinf_decode_symbol(d, dt);
            if (dist > dt->max_sym || dist > 29) return TINF_DATA_ERROR;
            offs = tinf_getbits_base(d, (int)dist_bits[dist], (int)dist_base[dist]);
            if (offs > (u32)(d->dest - d->dest_start)) return TINF_DATA_ERROR;
            if ((u32)(d->dest_end - d->dest) < length) return TINF_BUF_ERROR;
            for (i = 0; i < (int)length; ++i) d->dest[i] = d->dest[i - (int)offs];
            d->dest += length;
        }
    }
}

static int tinf_inflate_uncompressed_block(struct tinf_data *d) {
    u32 length, invlength;
    if ((u32)(d->source_end - d->source) < 4) return TINF_DATA_ERROR;
    length = read_le16(d->source);
    invlength = read_le16(d->source + 2);
    if (length != (~invlength & 0xFFFF)) return TINF_DATA_ERROR;
    d->source += 4;
    if ((u32)(d->source_end - d->source) < length) return TINF_DATA_ERROR;
    if ((u32)(d->dest_end - d->dest) < length) return TINF_BUF_ERROR;
    while (length--) *d->dest++ = *d->source++;
    d->tag = 0;
    d->bitcount = 0;
    return TINF_OK;
}

static int tinf_inflate_fixed_block(struct tinf_data *d) {
    tinf_build_fixed_trees(&d->ltree, &d->dtree);
    return tinf_inflate_block_data(d, &d->ltree, &d->dtree);
}

static int tinf_inflate_dynamic_block(struct tinf_data *d) {
    int res = tinf_decode_trees(d, &d->ltree, &d->dtree);
    if (res != TINF_OK) return res;
    return tinf_inflate_block_data(d, &d->ltree, &d->dtree);
}

int tinf_uncompress(void *dest, unsigned int *destLen, const void *source, unsigned int sourceLen) {
    struct tinf_data d;
    int bfinal, res;
    u32 btype;
    d.source = (const u8 *)source;
    d.source_end = d.source + sourceLen;
    d.tag = 0;
    d.bitcount = 0;
    d.overflow = 0;
    d.dest = (u8 *)dest;
    d.dest_start = d.dest;
    d.dest_end = d.dest + *destLen;
    do {
        bfinal = (int)tinf_getbits(&d, 1);
        btype = tinf_getbits(&d, 2);
        switch (btype) {
            case 0: res = tinf_inflate_uncompressed_block(&d); break;
            case 1: res = tinf_inflate_fixed_block(&d); break;
            case 2: res = tinf_inflate_dynamic_block(&d); break;
            default: res = TINF_DATA_ERROR; break;
        }
        if (res != TINF_OK) return res;
    } while (!bfinal);
    if (d.overflow) return TINF_DATA_ERROR;
    *destLen = (unsigned int)(d.dest - d.dest_start);
    return TINF_OK;
}
