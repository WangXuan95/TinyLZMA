#ifndef   __TINY_ZSTD_DECOMPRESS_H__
#define   __TINY_ZSTD_DECOMPRESS_H__

#include <stddef.h>   // use size_t

/// Zstandard decompression functions.
/// `dst` must point to a space at least as large as the reconstructed output.
size_t ZSTD_decompress (void *src, size_t src_len, void *dst, size_t dst_len);

/// Get the decompressed size of an input stream so memory can be allocated in advance
/// Returns -1 if the size can't be determined. Assumes decompression of a single frame
size_t ZSTD_get_decompressed_size (void *src, size_t src_len);

#endif // __TINY_ZSTD_DECOMPRESS_H__
