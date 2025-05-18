#ifndef   __LZMA_C_H__
#define   __LZMA_C_H__

#include <stddef.h>
#include <stdint.h>

int lzmaC (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len);

#endif // __LZMA_C_H__
