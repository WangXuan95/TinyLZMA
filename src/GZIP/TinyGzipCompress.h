
#ifndef   __TINY_GZIP_COMPRESS_H__
#define   __TINY_GZIP_COMPRESS_H__

#include <stdint.h>                                                    // this code only use types uint8_t and uint32_t defined in stdint.h

uint32_t gzipCompress (uint8_t *p_dst, uint8_t *p_src, uint32_t len);  // import from TinyGZIP.c

#endif // __TINY_GZIP_COMPRESS_H__
