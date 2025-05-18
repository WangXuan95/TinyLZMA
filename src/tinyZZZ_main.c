#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "FileIO.h"

#include "gzipC.h"
#include "lz4D.h"
#include "lz4C.h"
#include "zstdD.h"
#include "lzmaD.h"
#include "lzmaC.h"
#include "lpaq8CD.h"
#include "zipC.h"



const char *USAGE =
    "|-------------------------------------------------------------------------------------------|\n"
    "|  TinyZZZ v0.5                                     https://github.com/WangXuan95/TinyZZZ   |\n"
    "|    TinyZZZ is a simple, standalone data compressor/decompressor with several popular data |\n"
    "|    compression algorithms, which are written in C language (C99). Unlike the official     |\n"
    "|    implementations, this code mainly focuses on simplicity and easy to understand.        |\n"
    "|-------------------------------------------------------------------------------------------|\n"
    "|  currently support:                                                                       |\n"
    "|   - GZIP  compress                                                                        |\n"
    "|   - LZ4   decompress and compress                                                         |\n"
    "|   - ZSTD  decompress                                                                      |\n"
    "|   - LZMA  decompress and compress                                                         |\n"
    "|   - LPAQ8 decompress and compress                                                         |\n"
    "|   - compress a file to ZIP container file using deflate (GZIP) method or LZMA method      |\n"
    "|-------------------------------------------------------------------------------------------|\n"
    "|  Usage :                                                                                  |\n"
    "|   - decompress a GZIP file       :  *** not yet supported! ***                            |\n"
    "|   - compress a file to GZIP file :  tinyZZZ -c --gzip <input_file> <output_file(.gz)>     |\n"
    "|   - decompress a LZ4 file        :  tinyZZZ -d --lz4  <input_file(.lz4)> <output_file>    |\n"
    "|   - compress a file to LZ4 file  :  tinyZZZ -c --lz4  <input_file> <output_file(.lz4)>    |\n"
    "|   - decompress a ZSTD file       :  tinyZZZ -d --zstd <input_file(.zst)> <output_file>    |\n"
    "|   - compress a file to ZSTD file :  *** not yet supported! ***                            |\n"
    "|   - decompress a LZMA file       :  tinyZZZ -d --lzma <input_file(.lzma)> <output_file>   |\n"
    "|   - compress a file to LZMA file :  tinyZZZ -c --lzma <input_file> <output_file(.lzma)>   |\n"
    "|   - decompress a LPAQ8 file      :  tinyZZZ -d --lpaq8 <input_file(.lpaq8)> <output_file> |\n"
    "|   - compress a file to LPAQ8 file:  tinyZZZ -c --lpaq8 <input_file> <output_file(.lpaq8)> |\n"
    "|-------------------------------------------------------------------------------------------|\n"
    "|  Usage (compress to ZIP container) :                                                      |\n"
    "|   - use Deflate method : tinyZZZ -c --gzip --zip <input_file> <output_file(.zip)>         |\n"
    "|   - use LZMA method    : tinyZZZ -c --lzma --zip <input_file> <output_file(.zip)>         |\n"
    "|-------------------------------------------------------------------------------------------|\n";




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



#define  IS_64b_SYSTEM  (sizeof(size_t) == 8)



int main (int argc, char **argv) {

    enum     {ACTION_NONE, COMPRESS, DECOMPRESS}         type_action = ACTION_NONE;
    enum     {FORMAT_NONE, GZIP, LZ4, ZSTD, LZMA, LPAQ8} type_format = FORMAT_NONE;
    enum     {NATIVE, ZIP}                            type_container = NATIVE;

    char    *fname_src=NULL, *fname_dst=NULL;
    uint8_t *p_src         , *p_dst;
    size_t   src_len       ,  dst_len , MAX_DST_LEN = IS_64b_SYSTEM ? 0x80000000 : 0x20000000;
    int      ret_code = 0;
    uint8_t  compress_level = 2;


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
            } else if (strcmp(arg, "--lz4" ) == 0) {
                type_format = LZ4;
            } else if (strcmp(arg, "--zstd") == 0) {
                type_format = ZSTD;
            } else if (strcmp(arg, "--lzma") == 0) {
                type_format = LZMA;
            } else if (strcmp(arg, "--lpaq8") == 0) {
                type_format = LPAQ8;
            } else if (strcmp(arg, "--zip" ) == 0) {
                type_container = ZIP;
            } else if ('0' <= arg[1] && arg[1] <= '9') {
                compress_level = arg[1] - '0';
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
            dst_len = MAX_DST_LEN;
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
        case GZIP : {
            if (type_action == DECOMPRESS) {
                printf("*** error : GZIP decompress is not yet supported\n");
                return -1;
            } else if (type_container != ZIP) {
                ret_code = gzipC(p_src, src_len, p_dst, &dst_len);
            } else {
                removeDirectoryPathFromFileName(fname_src);
                ret_code = zipCdeflate(p_src, src_len, p_dst, &dst_len, fname_src);
            }
            break;
        }
        case LZMA : {
            if (type_action == DECOMPRESS) {
                ret_code = lzmaD(p_src, src_len, p_dst, &dst_len);
            } else if (type_container != ZIP) {
                ret_code = lzmaC(p_src, src_len, p_dst, &dst_len);
            } else {
                removeDirectoryPathFromFileName(fname_src);
                ret_code = zipClzma(p_src, src_len, p_dst, &dst_len, fname_src);
            }
            break;
        }
        case LZ4 : {
            if (type_action == DECOMPRESS) {
                ret_code = lz4D(p_src, src_len, p_dst, &dst_len);
            } else if (type_container != ZIP) {
                ret_code = lz4C(p_src, src_len, p_dst, &dst_len);
            } else {
                printf("*** error : LZ4 compress to ZIP is not supported\n");
                return -1;
            }
            break;
        }
        case ZSTD : {
            if (type_action == DECOMPRESS) {
                zstdD(p_src, src_len, p_dst, &dst_len);
            } else {
                printf("*** error : ZSTD compress is not yet supported\n");
                return -1;
            }
            break;
        }
        case LPAQ8 : {
            size_t mem_usage = 0;
            if (type_action == DECOMPRESS) {
                ret_code = lpaq8D(p_src, src_len, p_dst, &dst_len, &compress_level, &mem_usage);
            } else if (type_container != ZIP) {
                ret_code = lpaq8C(p_src, src_len, p_dst, &dst_len,  compress_level, &mem_usage);
            } else {
                printf("*** error : LPAQ8 compress to ZIP is not supported\n");
                return -1;
            }
            printf("compress level   = %d\n", (int)compress_level);
            printf("memory usage     = %lu\n", mem_usage);
            break;
        }
        case FORMAT_NONE : {
            printf(USAGE);
            return -1;
        }
    }
    
    if (ret_code) {
        printf("*** error : failed (return_code = %d)\n", ret_code);
        return ret_code;
    }
    

    free(p_src);
    
    printf("output length    = %lu\n", dst_len);
    
    {   size_t decomp_size = (type_action==COMPRESS) ? src_len : dst_len;
        double time  = (double)clock() / CLOCKS_PER_SEC;
        double speed = (0.001*decomp_size) / (time + 0.00000001);
        printf("time consumed    = %.3f sec  (%.0f kB/s)\n", time, speed);
    }
    
    if (saveToFile(p_dst, dst_len, fname_dst)) {
        printf("*** error : save file %s failed\n", fname_dst);
        return -1;
    }
    
    free(p_dst);
    
    return 0;
}

