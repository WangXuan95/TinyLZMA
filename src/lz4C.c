#include <stddef.h>   // size_t
#include <stdint.h>   // uint8_t, uint64_t

#define R_OK                            0
#define R_DST_OVERFLOW                  1
#define R_SRC_OVERFLOW                  2

#define RET_WHEN_ERR(err_code)          { int ec = (err_code); if (ec)  return ec; }
#define RET_ERR_IF(err_code,condition)  { if (condition) return err_code; }

#define MIN_ML                          4

#define MIN_COMPRESSED_BLOCK_SIZE       13
#define MAX_COMPRESSED_BLOCK_SIZE       4194304
#define MAX_OFFSET                      1024      // 65535


static int LZ4_write (uint8_t **pp_dst, uint8_t *p_dst_limit, uint8_t byte) {
    RET_ERR_IF(R_DST_OVERFLOW, (*pp_dst >= p_dst_limit));
    *((*pp_dst)++) = byte;
    return R_OK;
}


static int LZ4_write_vlc (uint8_t **pp_dst, uint8_t *p_dst_limit, uint64_t value) {
    for (;;) {
        if (value < 255) {
            RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, value));
            break;
        } else {
            RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 255));
            value -= 255;
        }
    }
    return R_OK;
}


static int LZ4_copy (uint8_t *p_src, uint8_t *p_src_end, uint8_t **pp_dst, uint8_t *p_dst_limit) {
    RET_ERR_IF(R_DST_OVERFLOW, (p_src_end - p_src > p_dst_limit - *pp_dst));
    for (; p_src<p_src_end; p_src++) {
        *((*pp_dst)++) = *p_src;
    }
    return R_OK;
}


static int LZ4_compress_seqence (uint8_t *p_src_lit, uint8_t *p_src, uint64_t ml, uint64_t of, uint8_t **pp_dst, uint8_t *p_dst_limit) {
    uint64_t ll = p_src - p_src_lit;
    uint8_t *p_token = *pp_dst;
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0));
    if (ll < 15) {
        (*p_token) = (ll << 4);
    } else {
        (*p_token) = (15 << 4);
        RET_WHEN_ERR(LZ4_write_vlc(pp_dst, p_dst_limit, ll-15));
    }
    RET_WHEN_ERR(LZ4_copy(p_src_lit, p_src, pp_dst, p_dst_limit));
    if (of) {      // when of==0, encode literal only
        RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, of&0xFF));
        RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, of>>8));
        ml -= MIN_ML;
        if (ml < 15) {
            (*p_token) |= ml;
        } else {
            (*p_token) |= 15;
            RET_WHEN_ERR(LZ4_write_vlc(pp_dst, p_dst_limit, ml-15));
        }
    }
    return R_OK;
}


static int LZ4_compress_block (uint8_t *p_src, uint8_t *p_src_end, uint8_t **pp_dst, uint8_t *p_dst_limit) {
    uint8_t *p_src_lit_start = p_src;
    uint8_t *p_src_base      = p_src;
    uint8_t *p_src_endlz     = p_src;
    if (p_src_end - p_src_endlz > MIN_COMPRESSED_BLOCK_SIZE) {
        p_src_endlz = p_src_end - MIN_COMPRESSED_BLOCK_SIZE;
    }
    while (p_src < p_src_end) {
        uint64_t ml=0, of=0;
        uint8_t *p_match = p_src_base;
        if (p_src - p_match > MAX_OFFSET) {
            p_match = p_src - MAX_OFFSET;
        }
        for (; p_match<p_src; p_match++) {
            uint8_t *p1 = p_match;
            uint8_t *p2 = p_src;
            while (*p1==*p2 && p2<p_src_endlz) {
                p1 ++;
                p2 ++;
            }
            if (MIN_ML <=p1 - p_match) {
                if (ml < p1 - p_match) {
                    ml = p1 - p_match;
                    of = p2 - p1;
                }
            }
        }
        if (ml != 0) {
            RET_WHEN_ERR(LZ4_compress_seqence(p_src_lit_start, p_src, ml, of, pp_dst, p_dst_limit));
            p_src += ml;
            p_src_lit_start = p_src;
        } else {
            p_src ++;
        }
    }
    RET_WHEN_ERR(LZ4_compress_seqence(p_src_lit_start, p_src, 0, 0, pp_dst, p_dst_limit));
    return R_OK;
}


static int LZ4_compress_or_copy_block_with_csize (uint8_t *p_src, uint8_t *p_src_end, uint8_t **pp_dst, uint8_t *p_dst_limit) {
    uint64_t csize = p_src_end - p_src;
    uint8_t *p_dst_base = (*pp_dst) + 4;
    RET_ERR_IF(R_DST_OVERFLOW, (p_dst_limit-(*pp_dst) < 4));
    (*pp_dst) += 4;
    if (csize <= MIN_COMPRESSED_BLOCK_SIZE) {
        RET_WHEN_ERR(LZ4_copy(p_src, p_src_end, pp_dst, p_dst_limit));
        csize |= 0x80000000U;
    } else {
        RET_WHEN_ERR(LZ4_compress_block(p_src, p_src_end, pp_dst, p_dst_limit));
        if (csize > (*pp_dst) - p_dst_base) {
            csize = (*pp_dst) - p_dst_base;
        } else {
            *pp_dst = p_dst_base;
            RET_WHEN_ERR(LZ4_copy(p_src, p_src_end, pp_dst, p_dst_limit));
            csize |= 0x80000000U;
        }
    }
    p_dst_base[-4] = 0xFF & (csize      );
    p_dst_base[-3] = 0xFF & (csize >>  8);
    p_dst_base[-2] = 0xFF & (csize >> 16);
    p_dst_base[-1] = 0xFF & (csize >> 24);
    return R_OK;
}


int lz4C (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len) {
    uint8_t  *p_src_limit = p_src + src_len;
    uint8_t  *p_dst_tmp   = p_dst;
    uint8_t **pp_dst      = &p_dst_tmp;
    uint8_t  *p_dst_limit = p_dst + (*p_dst_len);
    RET_ERR_IF(R_SRC_OVERFLOW, p_src > p_src_limit);
    RET_ERR_IF(R_DST_OVERFLOW, p_dst > p_dst_limit);
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x04));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x22));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x4D));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x18));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x60));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x70));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x73));
    while (p_src < p_src_limit) {
        uint8_t *p_src_end = p_src_limit;      // block end
        if (p_src_end - p_src > MAX_COMPRESSED_BLOCK_SIZE) {
            p_src_end = p_src + MAX_COMPRESSED_BLOCK_SIZE;
        }
        RET_WHEN_ERR(LZ4_compress_or_copy_block_with_csize(p_src, p_src_end, pp_dst, p_dst_limit));
        p_src = p_src_end;
    }
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x00));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x00));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x00));
    RET_WHEN_ERR(LZ4_write(pp_dst, p_dst_limit, 0x00));
    *p_dst_len = (*pp_dst) - p_dst;
    return R_OK;
}

