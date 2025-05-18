#include <stddef.h>
#include <stdint.h>


int writeZipLzmaProperty (uint8_t *p_dst, size_t *p_dst_len);                                                   // lzmaC.c
int lzmaEncode    (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, uint8_t with_end_mark);   // lzmaC.c
int deflateEncode (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len);                          // gzipC.c


#define   R_OK                           0
#define   R_ERR_UNSUPPORTED              2
#define   R_ERR_OUTPUT_OVERFLOW          3

#define   RET_WHEN_ERR(err_code)          { int ec = (err_code); if (ec)  return ec; }


static size_t getStringLength (const char *string) {
    size_t i;
    for (i=0; *string; string++, i++);
    return i;
}


#define   ZIP_HEADER_LEN_EXCLUDE_FILENAME   30
#define   ZIP_FOOTER_LEN_EXCLUDE_FILENAME   (46 + 22)

#define   FILE_NAME_IN_ZIP_MAX_LEN          ((size_t)0xFF00)
#define   ZIP_UNCOMPRESSED_MAX_LEN          ((size_t)0xFFFF0000)
#define   ZIP_COMPRESSED_MAX_LEN            ((size_t)0xFFFF0000)

#define   COMP_METHOD_LZMA                  0x0E
#define   COMP_METHOD_DEFLATE               0x08


static int writeZipHeader (uint8_t *p_dst, size_t *p_dst_len, uint32_t crc, size_t compressed_len, size_t uncompressed_len, const char *file_name, uint8_t comp_method) {
    size_t i;
    const size_t file_name_len = getStringLength(file_name);
    
    if (file_name_len > FILE_NAME_IN_ZIP_MAX_LEN)
        return R_ERR_UNSUPPORTED;
    
    if (uncompressed_len > ZIP_UNCOMPRESSED_MAX_LEN)                   // ".zip" format don't support uncompressed size > 32-bit integer
        return R_ERR_UNSUPPORTED;
    
    if (compressed_len > ZIP_COMPRESSED_MAX_LEN)                       // ".zip" format don't support compressed size > 32-bit integer
        return R_ERR_UNSUPPORTED;
    
    if (*p_dst_len < ZIP_HEADER_LEN_EXCLUDE_FILENAME + file_name_len)  // no enough space for writing ZIP header
        return R_ERR_OUTPUT_OVERFLOW;
    
    *p_dst_len = ZIP_HEADER_LEN_EXCLUDE_FILENAME + file_name_len;
    
    // Local File Header ----------------------------------------------------
    *(p_dst++) = 0x50;                               // 0~3 Local file header signature # 0x04034b50 (read as a little-endian number)
    *(p_dst++) = 0x4B;
    *(p_dst++) = 0x03;
    *(p_dst++) = 0x04;
    *(p_dst++) = 0x3F;                               // 4~5 Version needed to extract (minimum)
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 6~7 General purpose bit flag
    *(p_dst++) = 0x00;
    *(p_dst++) = comp_method;                        // 8~9 Compression method
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 10~11 File last modification time
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 12~13 File last modification date
    *(p_dst++) = 0x00;
    *(p_dst++) = (uint8_t)(crc              >> 0);   // 14~17 CRC-32
    *(p_dst++) = (uint8_t)(crc              >> 8);
    *(p_dst++) = (uint8_t)(crc              >>16);
    *(p_dst++) = (uint8_t)(crc              >>24);
    *(p_dst++) = (uint8_t)(compressed_len   >> 0);   // 18~21 Compressed size
    *(p_dst++) = (uint8_t)(compressed_len   >> 8);
    *(p_dst++) = (uint8_t)(compressed_len   >>16);
    *(p_dst++) = (uint8_t)(compressed_len   >>24);
    *(p_dst++) = (uint8_t)(uncompressed_len >> 0);   // 22~25 Uncompressed size
    *(p_dst++) = (uint8_t)(uncompressed_len >> 8);
    *(p_dst++) = (uint8_t)(uncompressed_len >>16);
    *(p_dst++) = (uint8_t)(uncompressed_len >>24);
    *(p_dst++) = (uint8_t)(file_name_len    >> 0);   // 26~27 File name length (n)
    *(p_dst++) = (uint8_t)(file_name_len    >> 8);
    *(p_dst++) = 0x00;                               // 28~29 Extra field length (m)
    *(p_dst++) = 0x00;
    
    for (i=0; i<file_name_len; i++)                  // 46~46+file_name_len-1 : File Name
        *(p_dst++) = file_name[i];
    
    return R_OK;
}


static int writeZipFooter (uint8_t *p_dst, size_t *p_dst_len, uint32_t crc, size_t compressed_len, size_t uncompressed_len, const char *file_name, size_t offset, uint8_t comp_method) {
    size_t i;
    const size_t file_name_len = getStringLength(file_name);
    
    if (*p_dst_len < ZIP_FOOTER_LEN_EXCLUDE_FILENAME + file_name_len)  // no enough space for writing ZIP footer
        return R_ERR_OUTPUT_OVERFLOW;
    
    *p_dst_len = ZIP_FOOTER_LEN_EXCLUDE_FILENAME + file_name_len;
    
    // Central Directory File Header ----------------------------------------------------
    *(p_dst++) = 0x50;                               // 0~3 Central directory file header signature # 0x02014b50
    *(p_dst++) = 0x4B;
    *(p_dst++) = 0x01;
    *(p_dst++) = 0x02;
    *(p_dst++) = 0x1E;                               // 4~5 Version made by
    *(p_dst++) = 0x03;
    *(p_dst++) = 0x3F;                               // 6~7 Version needed to extract (minimum)
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 8~9 General purpose bit flag
    *(p_dst++) = 0x00;
    *(p_dst++) = comp_method;                        // 10~11 Compression method
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 12~13 File last modification time
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 14~15 File last modification date
    *(p_dst++) = 0x00;
    *(p_dst++) = (uint8_t)(crc              >> 0);   // 16~19 CRC-32
    *(p_dst++) = (uint8_t)(crc              >> 8);
    *(p_dst++) = (uint8_t)(crc              >>16);
    *(p_dst++) = (uint8_t)(crc              >>24);
    *(p_dst++) = (uint8_t)(compressed_len   >> 0);   // 20~23 Compressed size
    *(p_dst++) = (uint8_t)(compressed_len   >> 8);
    *(p_dst++) = (uint8_t)(compressed_len   >>16);
    *(p_dst++) = (uint8_t)(compressed_len   >>24);
    *(p_dst++) = (uint8_t)(uncompressed_len >> 0);   // 24~27 Uncompressed size
    *(p_dst++) = (uint8_t)(uncompressed_len >> 8);
    *(p_dst++) = (uint8_t)(uncompressed_len >>16);
    *(p_dst++) = (uint8_t)(uncompressed_len >>24);
    *(p_dst++) = (uint8_t)(file_name_len    >> 0);   // 28~29 File name length (n)
    *(p_dst++) = (uint8_t)(file_name_len    >> 8);
    *(p_dst++) = 0x00;                               // 30~31 Extra field length (m)
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 32~33 File comment length (k)
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 34~35 Disk number where file starts
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 36~37 Internal file attributes
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 38~41 External file attributes
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 42~45 Relative offset of local file header.
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;
    
    for (i=0; i<file_name_len; i++)                  // 46~46+file_name_len-1 : File Name
        *(p_dst++) = file_name[i];
    
    // End of Central Directory Record ----------------------------------------------------
    *(p_dst++) = 0x50;                               // 0~3 End of central directory signature # 0x06054b50
    *(p_dst++) = 0x4B;
    *(p_dst++) = 0x05;
    *(p_dst++) = 0x06;
    *(p_dst++) = 0x00;                               // 4~5 Number of this disk
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x00;                               // 6~7 Disk where central directory starts
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x01;                               // 8~9 Number of central directory records on this disk
    *(p_dst++) = 0x00;
    *(p_dst++) = 0x01;                               // 10~11 Total number of central directory records
    *(p_dst++) = 0x00;
    *(p_dst++) = (uint8_t)((46+file_name_len) >> 0); // 12~15 Size of central directory (bytes)
    *(p_dst++) = (uint8_t)((46+file_name_len) >> 8);
    *(p_dst++) = (uint8_t)((46+file_name_len) >>16);
    *(p_dst++) = (uint8_t)((46+file_name_len) >>24);
    *(p_dst++) = (uint8_t)(offset             >> 0); // 16~19 Offset of start of central directory, relative to start of archive (pos of p_dst)
    *(p_dst++) = (uint8_t)(offset             >> 8);
    *(p_dst++) = (uint8_t)(offset             >>16);
    *(p_dst++) = (uint8_t)(offset             >>24);
    *(p_dst++) = 0x00;                               // 20~21 Comment length (n)
    *(p_dst++) = 0x00;
    
    return R_OK;
}


static uint32_t calcCrc32 (const uint8_t *p_src, size_t src_len) {
    static const uint32_t TABLE_CRC32 [] = { 0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
    
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t *p_end = p_src + src_len;
    
    for (; p_src<p_end; p_src++) {
        crc ^= *p_src;
        crc = TABLE_CRC32[crc & 0x0f] ^ (crc >> 4);
        crc = TABLE_CRC32[crc & 0x0f] ^ (crc >> 4);
    }
    
    return ~crc;
}


static int zipC (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip, uint8_t comp_method) {
    size_t zip_hdr_len, lzma_prop_len, cmprs_len, zip_ftr_len;                                          // there are 4 parts of the final output data : ZIP header, ZIP LZMA property, LZMA compressed data, and ZIP footer
    uint32_t crc;
    
    zip_hdr_len = *p_dst_len;                                                                           // set available space for ZIP header
    
    RET_WHEN_ERR( writeZipHeader(p_dst, &zip_hdr_len, 0, 0, src_len, file_name_in_zip, comp_method) );  // note that some fields are unknown and filled using "0", we should rewrite it later
    
    if (comp_method == COMP_METHOD_LZMA) {
        lzma_prop_len = *p_dst_len - zip_hdr_len;                                                       // set available space for ZIP LZMA property
        RET_WHEN_ERR( writeZipLzmaProperty(p_dst+zip_hdr_len, &lzma_prop_len) );
    } else {
        lzma_prop_len = 0;
    }

    cmprs_len = *p_dst_len - zip_hdr_len - lzma_prop_len;                                               // set available space for LZMA compressed data
    
    if (comp_method == COMP_METHOD_LZMA) {
        RET_WHEN_ERR(   lzmaEncode(p_src, src_len, p_dst+zip_hdr_len+lzma_prop_len, &cmprs_len, 1));
    } else {
        RET_WHEN_ERR(deflateEncode(p_src, src_len, p_dst+zip_hdr_len+lzma_prop_len, &cmprs_len));
    }
    
    if (cmprs_len > ZIP_COMPRESSED_MAX_LEN) {
        return R_ERR_UNSUPPORTED;
    }
    
    cmprs_len += lzma_prop_len;                                                                         // ZIP's LZMA property is actually a part of compressed data
    
    crc = calcCrc32(p_src, src_len);
    
    zip_ftr_len = *p_dst_len - zip_hdr_len - cmprs_len;                                                 // set available space for ZIP footer
    
    RET_WHEN_ERR( writeZipFooter(p_dst+zip_hdr_len+cmprs_len, &zip_ftr_len, crc, cmprs_len, src_len, file_name_in_zip, zip_hdr_len+cmprs_len, comp_method) );
    
    RET_WHEN_ERR( writeZipHeader(p_dst,                       &zip_hdr_len, crc, cmprs_len, src_len, file_name_in_zip, comp_method) );   // rewrite ZIP header, since some fields are not writed previously.
    
    *p_dst_len = zip_hdr_len + cmprs_len + zip_ftr_len;                                                 // the total output length = ZIP header length + compressed data length (include ZIP LZMA property) + ZIP footer length
    
    return R_OK;
}


int zipClzma    (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip) {
    return zipC(p_src, src_len, p_dst, p_dst_len, file_name_in_zip, COMP_METHOD_LZMA);
}


int zipCdeflate (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip) {
    return zipC(p_src, src_len, p_dst, p_dst_len, file_name_in_zip, COMP_METHOD_DEFLATE);
}

