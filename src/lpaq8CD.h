#ifndef   __LPAQ8_CD_H__
#define   __LPAQ8_CD_H__

#include <stddef.h>
#include <stdint.h>

int lpaq8D (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, uint8_t *p_level, size_t *p_mem_usage);
int lpaq8C (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, uint8_t    level, size_t *p_mem_usage);

#endif // __LPAQ8_CD_H__
