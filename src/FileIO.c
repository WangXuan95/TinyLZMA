
#include <stdlib.h>
#include <stdio.h>

#include "FileIO.h"



// Function  : read all data from file to a buffer.
// Note      : The buffer is malloc in this function and need to be free outside by user !
// Parameter :
//     size_t *p_len        : getting the data length, i.e. the file length.
//     const char *filename : file name
// Return    :
//     non-NULL pointer     : success. Return the data buffer pointer. The data length will be on *p_len
//     NULL                 : failed
uint8_t *loadFromFile (size_t *p_len, const char *filename) {
    size_t   rlen  = 0;
    FILE    *fp    = NULL;
    uint8_t *p_buf = NULL;
    
    *p_len = 0;
    
    fp = fopen(filename, "rb");
    
    if (fp == NULL)
        return NULL;
    
    if (0 != fseek(fp, 0, SEEK_END)) {
        fclose(fp);
        return NULL;
    }
    
    *p_len = ftell(fp);                  // get file data length
    
    if (0 != fseek(fp, 0, SEEK_SET)) {
        fclose(fp);
        return NULL;
    }
    
    if (*p_len == 0) {                   // special case : file length = 0 (empty file)
        fclose(fp);
        p_buf = (uint8_t*)malloc(1);     // malloc a 1-byte buffer
        return p_buf;                    // directly return it without filling any data
    }
    
    p_buf = (uint8_t*)malloc((*p_len) + 65536);
    
    if (p_buf == NULL) {
        fclose(fp);
        return NULL;
    }
    
    rlen = fread(p_buf, sizeof(uint8_t), (*p_len), fp);
    
    fclose(fp);
    
    if (rlen != (*p_len)) {             // actual readed length is not equal to expected readed length
        free(p_buf);
        return NULL;
    }
    
    return p_buf;
}



// Function  : write data from buffer to a file.
// Parameter :
//     const uint8_t *p_buf : data buffer pointer
//     size_t len           : data length
//     const char *filename : file name
// Return    :
//     0 : failed
//     1 : success
int saveToFile (const uint8_t *p_buf, size_t len, const char *filename) {
    size_t  wlen = 0;
    FILE   *fp;
    
    fp = fopen(filename, "wb");
    
    if (fp == NULL)
        return 1;
    
    if (len > 0)
        wlen = fwrite(p_buf, sizeof(uint8_t), len, fp);
    
    fclose(fp);
    
    if (wlen != len)
        return 1;
    
    return 0;
}

