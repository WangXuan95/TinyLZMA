#ifndef   __ZIP_C_H__
#define   __ZIP_C_H__

#include <stddef.h>
#include <stdint.h>

int zipClzma    (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip);
int zipCdeflate (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip);

#endif // __ZIP_C_H__
