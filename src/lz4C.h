#ifndef   __LZ4_C_H__
#define   __LZ4_C_H__

#include <stddef.h>
#include <stdint.h>

int lz4C (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len);

#endif // __LZ4_C_H__
