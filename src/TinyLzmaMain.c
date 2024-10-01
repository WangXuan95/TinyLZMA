// TinyLZMA
// Source from https://github.com/WangXuan95/TinyLzma


#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "FileIO.h"
#include "TinyLzmaCompress.h"
#include "TinyLzmaDecompress.h"



// Function : convert a char from upper case to lower case
static char toLowerCase (const char ch) {
    if (ch >= 'A' && ch <= 'Z')
        return ch + ('a' - 'A');
    else
        return ch;
}


// Function : assert that a string is end with pattern (ignore upper/lower case).
//            e.g., if pattern is ".lzma", string is "a.lzma", it returns 1
//                  if pattern is ".zip" , string is "a.lzma", it returns 0
static int strEndswith (const char *pattern, const char *string) {
    const char *p = pattern;
    const char *q = string;
    
    for (; *p; p++);                             // goto pattern's end
    for (; *q; q++);                             // goto string's end
    
    for (; !(p<pattern); p--, q--) {
        if (q<string)                            // string is shorter than pattern
            return 0;
        if (toLowerCase(*p) != toLowerCase(*q))  // mismatch
            return 0;
    }
    
    return 1;
}


// Function : remove a filename's path prefix.
//            e.g., if fname is "a/b/c.txt", we will get "c.txt"
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



const char *USAGE =
    "|-----------------------------------------------------------------|\n"
    "|  Tiny LZMA compressor & decompressor v0.2                       |\n"
    "|  Source from https://github.com/WangXuan95/TinyLZMA             |\n"
    "|-----------------------------------------------------------------|\n"
    "|  Usage :                                                        |\n"
    "|     mode1 : decompress .lzma file :                             |\n"
    "|       tlzma  <input_file(.lzma)>  <output_file>                 |\n"
    "|                                                                 |\n"
    "|     mode2 : compress a file to .lzma file :                     |\n"
    "|       tlzma  <input_file>  <output_file(.lzma)>                 |\n"
    "|                                                                 |\n"
    "|     mode3 : compress a file to .zip file (use lzma algorithm) : |\n"
    "|       tlzma  <input_file>  <output_file(.zip)>                  |\n"
    "|-----------------------------------------------------------------|\n"
;



#define   DECOMPRESS_OUTPUT_MAX_LEN            0x20000000UL


int main(int argc, char **argv) {
    char    *fname_src = NULL;
    char    *fname_dst = NULL;
    uint8_t *p_src  , *p_dst;
    size_t   src_len,  dst_len;
    int      res, mode;
    
    if (argc < 3) {
        printf(USAGE);
        return -1;
    }
    
    fname_src = argv[1];
    fname_dst = argv[2];
    
    printf("input  file name = %s\n", fname_src);
    printf("output file name = %s\n", fname_dst);
    
    
    if        ( strEndswith(".zip" , fname_dst) ) {
        mode = 3;
        printf("mode             = 3 (compress to .zip file)\n");
    } else if ( strEndswith(".lzma", fname_dst) ) {
        mode = 2;
        printf("mode             = 2 (compress to .lzma file)\n");
    } else if ( strEndswith(".lzma", fname_src) ) {
        mode = 1;
        printf("mode             = 1 (decompress .lzma file)\n");
    } else {
        printf(USAGE);
        printf("*** error : unsupported command\n");
        return -1;
    }
    
    
    p_src = loadFromFile(&src_len, fname_src);
    
    if (p_src == NULL) {
        printf("*** error : load file %s failed\n", fname_src);
        return -1;
    }
    
    printf("input  length    = %lu\n", src_len);
    
    
    switch (mode) {
        case 1  :
            dst_len = DECOMPRESS_OUTPUT_MAX_LEN;
            break;
        default :  // case 2 or 3 : 
            dst_len = src_len + (src_len>>2) + 4096;
            if (dst_len < src_len)                        // size_t data type overflow
                dst_len = (~((size_t)0));                 // max value of size_t
            break;
    }
    
    
    p_dst = (uint8_t*)malloc(dst_len);
    
    if (p_dst == NULL) {
        free(p_src);
        printf("*** error : allocate output buffer failed\n");
        return -1;
    }
    
    
    switch (mode) {
        case 1  :
            printf("decompressing...\n");
            res = tinyLzmaDecompress(p_src, src_len, p_dst, &dst_len);
            break;
        case 2  :
            printf("compressing...\n");
            res = tinyLzmaCompress  (p_src, src_len, p_dst, &dst_len);
            break;
        default :  // case 3 :
            printf("compressing...\n");
            removeDirectoryPathFromFileName(fname_src);
            res = tinyLzmaCompressToZipContainer(p_src, src_len, p_dst, &dst_len, fname_src);
            break;
    }
    
    free(p_src);
    
    if (res) {
        free(p_dst);
        printf("*** error : failed (return_code = %d)\n", res);
        return res;
    }
    
    printf("output length    = %lu\n", dst_len);
    
    {
    size_t decomp_size = (mode==1) ? dst_len : src_len;
    double time = (double)clock()/CLOCKS_PER_SEC;
    printf("time consumed    = %.3f s\n", time);
    printf("speed            = %.0f kB/s\n", (0.001*decomp_size)/time );
    }
    
    if (saveToFile(p_dst, dst_len, fname_dst) < 0) {
        free(p_dst);
        printf("*** error : save file %s failed\n", fname_dst);
        return -1;
    }
    
    free(p_dst);
    
    return 0;
}

