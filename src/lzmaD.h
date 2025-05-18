#ifndef   __LZMA_D_H__
#define   __LZMA_D_H__

#include <stddef.h>
#include <stdint.h>

int lzmaD (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len);

#endif // __LZMA_D_H__
