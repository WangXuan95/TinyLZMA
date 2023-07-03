// TinyLZMA
// Source from https://github.com/WangXuan95/TinyLzma


#ifndef   __TINY_LZMA_COMPRESS_H__
#define   __TINY_LZMA_COMPRESS_H__

#include <stdint.h>
#include <stddef.h>

int tinyLzmaCompress               (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len);

int tinyLzmaCompressToZipContainer (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip);

// return codes of tinyLzmaCompressor and tinyLzmaCompressToZipContainer --------------------
#define   R_OK                           0
#define   R_ERR_MEMORY_RUNOUT            1
#define   R_ERR_UNSUPPORTED              2
#define   R_ERR_OUTPUT_OVERFLOW          3

#endif // __TINY_LZMA_COMPRESS_H__
