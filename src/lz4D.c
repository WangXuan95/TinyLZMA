#include <stddef.h>   // size_t
#include <stdint.h>   // uint8_t, uint64_t

#define R_OK                            0
#define R_DST_OVERFLOW                  1
#define R_SRC_OVERFLOW                  2
#define R_CORRUPT                       3
#define R_VERSION                       4
#define R_NOT_LZ4                       5
#define R_NOT_YET_SUPPORT               101

#define RET_WHEN_ERR(err_code)          { int ec = (err_code); if (ec)  return ec; }
#define RET_ERR_IF(err_code,condition)  { if (condition) return err_code; }

#define MAGIC_LZ4LEGACY                 0x184C2102U
#define MAGIC_LZ4FRAME                  0x184D2204U
#define MAGIC_SKIPFRAME_MIN             0x184D2A50U
#define MAGIC_SKIPFRAME_MAX             0x184D2A5FU

#define MIN_ML                          4


static int LZ4_skip (uint8_t **pp_src, uint8_t *p_src_limit, uint64_t n_bytes) {
    RET_ERR_IF(R_SRC_OVERFLOW, (n_bytes > p_src_limit - *pp_src));
    (*pp_src) += n_bytes;
    return R_OK;
}


static int LZ4_read (uint8_t **pp_src, uint8_t *p_src_limit, uint64_t n_bytes, uint64_t *p_value) {
    uint64_t i;
    RET_ERR_IF(R_SRC_OVERFLOW, (n_bytes > p_src_limit - *pp_src));
    (*p_value) = 0;
    for (i=0; i<n_bytes; i++) {
        (*p_value) += (((uint64_t)(uint8_t)(**pp_src)) << (i*8));
        (*pp_src) ++;
    }
    return R_OK;
}


static int LZ4_copy (uint8_t **pp_src, uint8_t *p_src_limit, uint8_t **pp_dst, uint8_t *p_dst_limit, uint64_t n_bytes) {
    RET_ERR_IF(R_SRC_OVERFLOW, (n_bytes > p_src_limit - *pp_src));
    RET_ERR_IF(R_DST_OVERFLOW, (n_bytes > p_dst_limit - *pp_dst));
    for (; n_bytes>0; n_bytes--) {
        **pp_dst = **pp_src;
        (*pp_src) ++;
        (*pp_dst) ++;
    }
    return R_OK;
}


static int LZ4_decompress_block (uint8_t **pp_src, uint8_t *p_src_limit, uint8_t **pp_dst, uint8_t *p_dst_limit, uint64_t block_csize) {
    RET_ERR_IF(R_SRC_OVERFLOW, (block_csize > p_src_limit - *pp_src));
    p_src_limit = (*pp_src) + block_csize;
    for (;;) {
        uint8_t *p_match;
        uint64_t byte, ll, ml, of;
        RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 1, &byte));
        ml = byte & 15;
        ll = byte >> 4;
        if (ll == 15) {
            do {
                RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 1, &byte));
                ll += byte;
            } while (byte == 255);
        }
        RET_WHEN_ERR(LZ4_copy(pp_src, p_src_limit, pp_dst, p_dst_limit, ll));   // copy literals from src to dst
        if (*pp_src == p_src_limit) {
            break;
        }
        RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 2, &of));
        RET_ERR_IF(R_CORRUPT, of==0);
        if (ml == 15) {
            do {
                RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 1, &byte));
                ml += byte;
            } while (byte == 255);
        }
        p_match = (*pp_dst) - of;
        RET_WHEN_ERR(LZ4_copy(&p_match, p_dst_limit, pp_dst, p_dst_limit, (ml+MIN_ML)));  // copy match
    }
    return R_OK;
}


static int LZ4_decompress_blocks_until_endmark (uint8_t **pp_src, uint8_t *p_src_limit, uint8_t **pp_dst, uint8_t *p_dst_limit, uint8_t block_checksum_flag) {
    uint64_t block_csize;
    RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 4, &block_csize));
    while  (block_csize != 0x00000000U) {
        if (block_csize <  0x80000000U) {
            RET_WHEN_ERR(LZ4_decompress_block(pp_src, p_src_limit, pp_dst, p_dst_limit, block_csize));
        } else {
            block_csize -= 0x80000000U;
            RET_WHEN_ERR(LZ4_copy(pp_src, p_src_limit, pp_dst, p_dst_limit, block_csize));
        }
        if (block_checksum_flag) {
            RET_WHEN_ERR(LZ4_skip(pp_src, p_src_limit, 4));  // block checksum, TODO: check it
        }
        RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 4, &block_csize));
    }
    return R_OK;
}


static int LZ4_decompress_blocks_legacy (uint8_t **pp_src, uint8_t *p_src_limit, uint8_t **pp_dst, uint8_t *p_dst_limit) {
    while (*pp_src != p_src_limit) {
        uint64_t block_csize;
        RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 4, &block_csize));
        if (block_csize == MAGIC_LZ4LEGACY || block_csize == MAGIC_LZ4FRAME || (MAGIC_SKIPFRAME_MIN <= block_csize && block_csize <= MAGIC_SKIPFRAME_MAX)) { // meeting known magic
            (*pp_src) -= 4;                                                                                                                                       // give back 4 bytes to input stream
            break;
        } else {
            RET_WHEN_ERR(LZ4_decompress_block(pp_src, p_src_limit, pp_dst, p_dst_limit, block_csize));
        }
    }
    return R_OK;
}


static int LZ4_parse_frame_descriptor (uint8_t **pp_src, uint8_t *p_src_limit, uint8_t *p_block_checksum_flag, uint8_t *p_content_checksum_flag, uint8_t *p_content_size_flag, uint64_t *p_content_size) {
    uint64_t bd_flg;
    RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 2, &bd_flg));
    RET_ERR_IF(R_NOT_YET_SUPPORT,  ((bd_flg & 1) != 0));  // currently do not support dictionary
    RET_ERR_IF(R_VERSION,   (((bd_flg >> 1) & 1) != 0));  // reserved must be 0
    *p_content_checksum_flag = ((bd_flg >> 2) & 1);
    *p_content_size_flag     = ((bd_flg >> 3) & 1);
    *p_block_checksum_flag   = ((bd_flg >> 4) & 1);
    RET_ERR_IF(R_VERSION,   (((bd_flg >> 6) & 3) != 1));  // version must be 1
    RET_ERR_IF(R_VERSION,   (((bd_flg >> 8)&0xF) != 0));  // reserved must be 0
    RET_ERR_IF(R_VERSION,   (((bd_flg >>12) & 7) <  4));  // Block MaxSize must be 4, 5, 6, 7
    RET_ERR_IF(R_VERSION,   (((bd_flg >>15) & 1) != 0));  // reserved must be 0
    if (*p_content_size_flag) {
        RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 8, p_content_size));
    } else {
        *p_content_size = 0;
    }
    RET_WHEN_ERR(LZ4_skip(pp_src, p_src_limit, 1));   // skip header checksum (HC) byte, TODO: check it
    return R_OK;
}


static int LZ4_decompress_frame (uint8_t **pp_src, uint8_t *p_src_limit, uint8_t **pp_dst, uint8_t *p_dst_limit) {
    uint64_t magic;
    RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 4, &magic));
    if        (magic == MAGIC_LZ4LEGACY) {
        RET_WHEN_ERR(LZ4_decompress_blocks_legacy(pp_src, p_src_limit, pp_dst, p_dst_limit));
    } else if (magic == MAGIC_LZ4FRAME) {
        uint8_t  block_checksum_flag, content_checksum_flag, content_size_flag;
        uint64_t content_size;
        uint8_t *p_dst_base = *pp_dst;
        RET_WHEN_ERR(LZ4_parse_frame_descriptor(pp_src, p_src_limit, &block_checksum_flag, &content_checksum_flag, &content_size_flag, &content_size));
        RET_WHEN_ERR(LZ4_decompress_blocks_until_endmark(pp_src, p_src_limit, pp_dst, p_dst_limit, block_checksum_flag));
        if (content_checksum_flag) {
            RET_WHEN_ERR(LZ4_skip(pp_src, p_src_limit, 4));      // content checksum, TODO: check it
        }
        if (content_size_flag) {
            RET_ERR_IF(R_CORRUPT, ((*pp_dst - p_dst_base) != content_size));
        }
    } else if (MAGIC_SKIPFRAME_MIN <= magic && magic <= MAGIC_SKIPFRAME_MAX) {
        uint64_t skip_frame_len;
        RET_WHEN_ERR(LZ4_read(pp_src, p_src_limit, 4, &skip_frame_len));
        RET_WHEN_ERR(LZ4_skip(pp_src, p_src_limit, skip_frame_len));
    } else {
        RET_ERR_IF(R_NOT_LZ4, 1);
    }
    return R_OK;
}


int lz4D (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len) {
    uint8_t *p_src_curr  = p_src;
    uint8_t *p_src_limit = p_src + src_len;
    uint8_t *p_dst_curr  = p_dst;
    uint8_t *p_dst_limit = p_dst + (*p_dst_len);
    RET_ERR_IF(R_SRC_OVERFLOW, p_src_curr > p_src_limit);
    RET_ERR_IF(R_DST_OVERFLOW, p_dst_curr > p_dst_limit);
    while (p_src_curr < p_src_limit) {
        RET_WHEN_ERR(LZ4_decompress_frame(&p_src_curr, p_src_limit, &p_dst_curr, p_dst_limit));
    }
    *p_dst_len = p_dst_curr - p_dst;
    return R_OK;
}

