#ifndef   __FILE_IO_H__
#define   __FILE_IO_H__

#include <stddef.h>
#include <stdint.h>


// Function  : read all data from file to a buffer.
// Note      : The buffer is malloc in this function and need to be free outside by user !
// Parameter :
//     size_t *p_len        : getting the data length, i.e. the file length.
//     const char *filename : file name
// Return    :
//     non-NULL pointer     : success. Return the data buffer pointer. The data length will be on *p_len
//     NULL                 : failed
uint8_t *loadFromFile (size_t *p_len, const char *filename);


// Function  : write data from buffer to a file.
// Parameter :
//     const uint8_t *p_buf : data buffer pointer
//     size_t len           : data length
//     const char *filename : file name
// Return    :
//     0 : failed
//     1 : success
int saveToFile (const uint8_t *p_buf, size_t len, const char *filename);


#endif // __FILE_IO_H__
