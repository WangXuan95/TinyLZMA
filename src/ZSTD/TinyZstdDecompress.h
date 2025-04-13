#ifndef   __TINY_ZSTD_DECOMPRESS_H__
#define   __TINY_ZSTD_DECOMPRESS_H__

#include <stddef.h>   // size_t
#include <stdint.h>   // uint8_t

size_t ZSTD_decompress (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t dst_capacity);

#endif // __TINY_ZSTD_DECOMPRESS_H__
