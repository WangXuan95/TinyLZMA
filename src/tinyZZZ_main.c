#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "FileIO.h"
#include "GZIP/TinyGzipCompress.h"
#include "ZSTD/TinyZstdDecompress.h"
#include "LZMA/TinyLzmaCompress.h"
#include "LZMA/TinyLzmaDecompress.h"



const char *USAGE =
    "|----------------------------------------------------------------------|\n"
    "|  TinyZZZ v0.3                https://github.com/WangXuan95/TinyZZZ   |\n"
    "|    TinyZZZ is a simple, standalone data compressor/decompressor      |\n"
    "|    which supports several popular data compression algorithms,       |\n"
    "|    including GZIP, ZSTD, and LZMA.                                   |\n"
    "|    These algorithms are written in C language, unlike the official   |\n"
    "|    code implementation, this code mainly focuses on simplicity       |\n"
    "|    and easy to understand.                                           |\n"
    "|----------------------------------------------------------------------|\n"
    "|  currently support:                                                  |\n"
    "|     - GZIP compress                                                  |\n"
    "|     - ZSTD decompress                                                |\n"
    "|     - LZMA compress and decompress                                   |\n"
    "|----------------------------------------------------------------------|\n"
    "|  Usage :                                                             |\n"
    "|   1. decompress a GZIP file                                          |\n"
    "|        (not yet supported!)                                          |\n"
    "|   2. compress a file to GZIP file                                    |\n"
    "|        tinyZZZ.exe -c --gzip <input_file> <output_file(.gz)>         |\n"
    "|   3. decompress a ZSTD file                                          |\n"
    "|        tinyZZZ.exe -d --zstd <input_file(.zst)> <output_file>        |\n"
    "|   4. compress a file to ZSTD file                                    |\n"
    "|        (not yet supported!)                                          |\n"
    "|   5. decompress a LZMA file                                          |\n"
    "|        tinyZZZ.exe -d --lzma <input_file(.lzma)> <output_file>       |\n"
    "|   6. compress a file to LZMA file                                    |\n"
    "|        tinyZZZ.exe -c --lzma <input_file> <output_file(.lzma)>       |\n"
    "|   7. compress a file to LZMA and pack to a .zip container file       |\n"
    "|        tinyZZZ.exe -c --lzma --zip <input_file> <output_file(.zip)>  |\n"
    "|----------------------------------------------------------------------|\n";




/// remove a filename's path prefix. e.g., if fname is "a/b/c.txt", we will get "c.txt"
static void removeDirectoryPathFromFileName (char *fname) {
    char *p = fname;
    char *q = fname;
    for (; *p; p++) {
        *q = *p;
        if (*p == '/' || *p == '\\')      // '/' is file sep of linux, '\' is file sep of windows
            q = fname;                    // back to base
        else
            q ++;
    };
    *q = '\0';
}



int main (int argc, char **argv) {

    enum     {ACTION_NONE, COMPRESS, DECOMPRESS} type_action = ACTION_NONE;
    enum     {FORMAT_NONE, GZIP, ZSTD, LZMA}     type_format = FORMAT_NONE;
    enum     {NATIVE, ZIP}                    type_container = NATIVE;

    char    *fname_src=NULL, *fname_dst=NULL;
    uint8_t *p_src         , *p_dst;
    size_t   src_len       ,  dst_len        , MAX_DST_LEN=0x80000000;


    // parse command line --------------------------------------------------------------------------------------------------
    for (argc--; argc>=1; argc--) {                    // for all argv (inversely)
        char *arg = argv[argc];
        if (arg[0] == '-') {
            if        (strcmp(arg, "-c"    ) == 0) {
                type_action = COMPRESS;
            } else if (strcmp(arg, "-d"    ) == 0) {
                type_action = DECOMPRESS;
            } else if (strcmp(arg, "--gzip") == 0) {
                type_format = GZIP;
            } else if (strcmp(arg, "--zstd") == 0) {
                type_format = ZSTD;
            } else if (strcmp(arg, "--lzma") == 0) {
                type_format = LZMA;
            } else if (strcmp(arg, "--zip" ) == 0) {
                type_container = ZIP;
            } else {
                printf(USAGE);  // unknown switch
                return -1;
            }
        } else if (fname_dst == NULL) {
            fname_dst = arg;    // get destination file name
        } else if (fname_src == NULL) {
            fname_src = arg;    // get source file name
        } else {
            printf(USAGE);      // too many file name
            return -1;
        }
    }

    if (fname_dst == NULL || fname_src == NULL || type_action == ACTION_NONE || type_format == FORMAT_NONE) {
        printf(USAGE);      // insufficient file name
        return -1;
    }
    
    printf("input  file name = %s\n", fname_src);
    printf("output file name = %s\n", fname_dst);
    
    
    // read source file --------------------------------------------------------------------------------------------------
    p_src = loadFromFile(&src_len, fname_src);
    if (p_src == NULL) {
        printf("*** error : load file %s failed\n", fname_src);
        return -1;
    }
    printf("input  length    = %lu\n", src_len);
    
    
    // estimate destination size, and allocate destination buffer --------------------------------------------------------
    switch (type_action) {
        case COMPRESS :
            dst_len = src_len + (src_len>>3) + 1048576;  // estimate maximum compressed size based on original size
            break;
        case DECOMPRESS :
            dst_len = (src_len * 500) + 1048576;         // estimate maximum decompressed size based on compressed size
            if (type_format == ZSTD) {
                size_t parsed_dst_len = ZSTD_get_decompressed_size(p_src, src_len);
                if (parsed_dst_len != (size_t)-1) {
                    dst_len = parsed_dst_len + 1048576;
                }
            }
            break;
        case ACTION_NONE :
            printf(USAGE);
            return -1;
    }

    if (dst_len > MAX_DST_LEN) {
        dst_len = MAX_DST_LEN;
    }
    
    p_dst = (uint8_t*)malloc(dst_len);
    
    if (p_dst == NULL) {
        printf("*** error : allocate destination buffer failed\n");
        return -1;
    }
    
    
    // do compress / decompress --------------------------------------------------------------------------------------------
    switch (type_format) {
        case GZIP :
            if (type_action == DECOMPRESS) {
                printf("*** error : GZIP decompress is not yet supported\n");
                return -1;
            } else {
                if (type_container != ZIP) {
                    dst_len = gzipCompress(p_dst, p_src, src_len);  // TODO: handle dst buffer overflow
                } else {
                    printf("*** error : GZIP compress to ZIP container is not yet supported\n");
                    return -1;
                }
            }
            break;
        
        case ZSTD :
            if (type_action == DECOMPRESS) {
                dst_len = ZSTD_decompress(p_src, src_len, p_dst, dst_len);
            } else {
                printf("*** error : ZSTD compress is not yet supported\n");
                return -1;
            }
            break;
        
        case LZMA : {
            int res_code;
            if (type_action == DECOMPRESS) {
                printf("LZMA decompressing...\n");
                res_code = tinyLzmaDecompress(p_src, src_len, p_dst, &dst_len);
            } else {
                if (type_container != ZIP) {
                    printf("LZMA compressing ...\n");
                    res_code = tinyLzmaCompress(p_src, src_len, p_dst, &dst_len);
                } else {
                    printf("LZMA compressing (to ZIP container)...\n");
                    removeDirectoryPathFromFileName(fname_src);
                    res_code = tinyLzmaCompressToZipContainer(p_src, src_len, p_dst, &dst_len, fname_src);
                }
            }
            if (res_code) {
                printf("*** error : failed (return_code = %d)\n", res_code);
                return res_code;
            }
            break;
        }

        case FORMAT_NONE :
            printf(USAGE);
            return -1;
    }
    

    free(p_src);
    
    printf("output length    = %lu\n", dst_len);
    
    {
        size_t decomp_size = (type_action==COMPRESS) ? src_len : dst_len;
        double time = (double)clock()/CLOCKS_PER_SEC;
        printf("time consumed    = %.3f s\n", time);
        printf("speed            = %.0f kB/s\n", (0.001*decomp_size)/time );
    }
    
    if (saveToFile(p_dst, dst_len, fname_dst) < 0) {
        printf("*** error : save file %s failed\n", fname_dst);
        return -1;
    }
    
    free(p_dst);
    
    return 0;
}

