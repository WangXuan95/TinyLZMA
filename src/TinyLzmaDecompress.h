// TinyLZMA
// Source from https://github.com/WangXuan95/TinyLzma


#ifndef   __TINY_LZMA_DECOMPRESS_H__
#define   __TINY_LZMA_DECOMPRESS_H__

#include <stdint.h>
#include <stddef.h>

int tinyLzmaDecompress (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len);

// return codes of tinyLzmaDecompressor --------------------
#define   R_OK                           0
#define   R_ERR_MEMORY_RUNOUT            1
#define   R_ERR_UNSUPPORTED              2
#define   R_ERR_OUTPUT_OVERFLOW          3
#define   R_ERR_INPUT_OVERFLOW           4
#define   R_ERR_DATA                     5
#define   R_ERR_OUTPUT_LEN_MISMATCH      6

#endif // __TINY_LZMA_DECOMPRESS_H__
