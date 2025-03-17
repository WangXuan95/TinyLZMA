/// Zstandard educational decoder implementation
/// See https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md

#include <stdint.h>   // data types
#include <stdlib.h>   // malloc, free, exit
#include <stdio.h>    // fprintf
#include <string.h>   // memset, memcpy

#include "TinyZstdDecompress.h"


#define ZSTD_MAGIC_NUMBER 0xFD2FB528U
#define ZSTD_BLOCK_SIZE_MAX ((size_t)128 * 1024)  // The size of `Block_Content` is limited by `Block_Maximum_Size`,
#define MAX_LITERALS_SIZE ZSTD_BLOCK_SIZE_MAX     // literal blocks can't be larger than their block

/// This decoder calls exit(1) when it encounters an error, however a production library should propagate error codes
#define ERROR(s)          do { fprintf(stderr, "Error: %s\n", s); exit(1); } while (0)
#define ERROR_IN_SIZE()   ERROR("Input buffer smaller than it should be or input is corrupted")
#define ERROR_OUT_SIZE()  ERROR("Output buffer too small for output")
#define ERROR_CORRUPT()   ERROR("Corruption detected while decompressing")
#define ERROR_MALLOC()    ERROR("Memory allocation error")

typedef uint8_t  u8;
typedef int32_t  i32;
typedef uint64_t u64;



typedef struct {
    u8    *ptr;          // 起始地址
    size_t len;          // 长度 (单位:byte)
    i32    bit_offset;   // 当前偏移 (单位:bit) ， (bit_offset/8)代表字节偏移，而(bit_offset%8)代表字节内的比特偏移位置
} istream_t;             // 输入流类型

#define ostream_t istream_t  // 输出流类型实际上与输入流一模一样，区别在于并没有用到 bit_offset，都是整字节寻址

#define HUF_MAX_BITS          (16)
#define HUF_MAX_SYMBS         (256)
#define FSE_MAX_ACCURACY_LOG  (15)
#define FSE_MAX_SYMBS         (256)

#define MAX_CODE_LIT_LEN      (35)
#define MAX_CODE_MAT_LEN      (52)

typedef struct {
    u8 *symbols;
    u8 *num_bits;
    i32 max_bits;
} HUF_dtable;

typedef struct {
    u8  *symbols;
    u8  *num_bits;
    i32 *new_state_base;
    i32  accuracy_log;
} FSE_dtable;

/// The context needed to decode blocks in a frame
typedef struct {
    // The total amount of data available for backreferences, to determine if an offset too large to be correct
    size_t n_bytes_decoded;

    // parsed from frame header
    size_t window_size;          // The size of window that we need to be able to contiguously store for references
    size_t frame_content_size;   // The total output size of this compressed frame
    i32 content_checksum_flag;   // 1-bit, Whether or not the content of this frame has a checksum
    i32 single_segment_flag;     // 1-bit, Whether or not the output for this frame is in a single segment

    // 同一个frame内跨block复用的huffman编码表和fse编码表
    HUF_dtable literals_dtable;
    FSE_dtable ll_dtable;
    FSE_dtable ml_dtable;
    FSE_dtable of_dtable;

    // The last 3 offsets for the special "repeat offsets".
    u64 prev_offsets[3];
} frame_context_t;







/******* STREAM OPERATIONS *************************************************/

/// Returns the bit position of the highest `1` bit in `num`
///   000 -> -1
///   001 -> 0
///   01x -> 1
///   1xx -> 2
///   ...
static i32 highest_set_bit (u64 num) {
    for (i32 i=63; i>=0; i--) {
        if (((u64)1 << i) & num) {
            return i;
        }
    }
    return -1;
}

/// Returns an `istream_t` constructed from the given pointer and length
static istream_t ST_new (u8 *ptr, size_t len) {
    return (istream_t) {ptr, len, 0};
}

/// 以src为起始地址，读取偏移量为offset处的num个bit（num最多为64），其中offset的单位为bit。注意：src被视作小端序
/// 如果 offset<0 ， <0 的bit全被设置为0
static u64 ST_get_bits (istream_t *stream, i32 num_bits) {
    i32 offset = stream->bit_offset;

    if        (num_bits > 64) {
        ERROR("Attempt to read an invalid number of bits");
    } else if (offset <= -64) {
        return 0;
    } else {
        i32 shift = 0;

        if (offset < 0) {
            shift = -offset;
            num_bits += offset;
            offset = 0;
        }

        if (num_bits <= 0) {
            return 0;
        } else {
            u8 *src = (stream->ptr) + (offset / 8);

            u64 res = (u64)src[1];
            res += (u64)src[2] << 8;
            res += (u64)src[3] << 16;
            res += (u64)src[4] << 24;
            res += (u64)src[5] << 32;
            res += (u64)src[6] << 40;
            res += (u64)src[7] << 48;
            res += (u64)src[8] << 56;

            res<<= (8 - (offset % 8));
            res |= ((u64)src[0] >> (offset % 8));

            res &= (((u64)1<<num_bits) - (u64)1);

            return (res << shift);
        }
    }
}

///  
static void ST_backward_prepare (istream_t *stream) {
    if (stream->bit_offset != 0) {
        ERROR("Attempting to init backward stream on a non-byte aligned stream");
    }
    stream->bit_offset = (stream->len * 8);
    stream->bit_offset -= (8 - highest_set_bit(stream->ptr[stream->len-1]));
}

///
static u64 ST_backward_read_bits (istream_t *stream, i32 num_bits) {
    stream->bit_offset = stream->bit_offset - num_bits;
    return ST_get_bits(stream, num_bits);
}

/// Reads `num` bits from a bitstream, and updates the internal offset
static u64 ST_forward_read_bits (istream_t *stream, i32 num_bits) {
    if (num_bits > 64 || num_bits <= 0) {
        ERROR("Attempt to read an invalid number of bits");
    }

    size_t bytes = (num_bits + stream->bit_offset + 7) / 8;
    size_t full_bytes = (num_bits + stream->bit_offset) / 8;
    if (bytes > stream->len) {
        ERROR_IN_SIZE();
    }

    u64 result = ST_get_bits(stream, num_bits);

    stream->bit_offset = (num_bits + stream->bit_offset) % 8;
    stream->ptr += full_bytes;
    stream->len -= full_bytes;

    return result;
}

/// If the remaining bits in a byte will be unused, advance to the end of the byte
static void ST_forward_align (istream_t *stream) {
    if (stream->bit_offset != 0) {
        if (stream->len == 0) {
            ERROR_IN_SIZE();
        }
        stream->ptr++;
        stream->len--;
        stream->bit_offset = 0;
    }
}

/// 把 stream 对象的起始地址ptr向后移动 len ，同时返回旧的起始地址
static u8 *ST_forward_skip (istream_t* stream, size_t len) {
    if (len > stream->len) {
        ERROR_IN_SIZE();
    }
    if (stream->bit_offset != 0) {
        ERROR("Attempting to operate on a non-byte aligned stream");
    }
    u8 *ptr = stream->ptr;
    stream->ptr += len;
    stream->len -= len;
    return ptr;
}

/// 返回一个新的 istream_t ，其起始地址 `ptr` 等于 `stream` 的起始地址，长度等于 `len`。
/// 然后，让原始的 `stream` 对象向后移 `len` 个字节。
static istream_t ST_forward_fork_sub (istream_t *stream, size_t len) {
    u8 *ptr = ST_forward_skip(stream, len);
    return ST_new(ptr, len);
}

/// 向 ostream 中写入一个字节，并向后移动 ptr
static void ST_write_byte (ostream_t *stream, u8 symb) {
    if (stream->len == 0) {
        ERROR_OUT_SIZE();
    }
    stream->ptr[0] = symb;
    stream->ptr++;
    stream->len--;
}
/******* END STREAM OPERATIONS *********************************************/






/******* HUFFMAN PRIMITIVES ***************************************************/
static void HUF_decompress_1stream (istream_t *in, u8 **pp_out, HUF_dtable *dtable) {
    if (in->len == 0) {
        ERROR_IN_SIZE();
    }
    ST_backward_prepare(in);
    i32 shift_bits = ST_backward_read_bits(in, ((u8)dtable->max_bits));
    while (in->bit_offset > (-dtable->max_bits)) {
        *((*pp_out)++) = dtable->symbols[shift_bits]; // `shift_bits` 中存放的是移位读取到的数据，其bit位数为 `max_bits` ，高位较旧，低位较新。查询 symbols 数组会根据高位来判断当前要解码出的数据是什么。
        u8 bits = dtable->num_bits[shift_bits];
        i32 rest = ST_backward_read_bits(in, bits);
        shift_bits = ((shift_bits << bits) + rest);   // Shift `bits` bits out of the shift_bits, keeping the low order bits that weren't necessary to determine this symbol.
        shift_bits &= ((1 << dtable->max_bits) - 1);  // keep shift_bits do not exceed `max_bits` bits
    }
    if (in->bit_offset != -dtable->max_bits) {
        ERROR_CORRUPT();
    }
}

static void HUF_decompress_4stream (istream_t *in, u8 **pp_out, HUF_dtable *dtable) {
    size_t csize1 = ST_forward_read_bits(in, 16);
    size_t csize2 = ST_forward_read_bits(in, 16);
    size_t csize3 = ST_forward_read_bits(in, 16);

    istream_t in1 = ST_forward_fork_sub(in, csize1);
    istream_t in2 = ST_forward_fork_sub(in, csize2);
    istream_t in3 = ST_forward_fork_sub(in, csize3);
    istream_t in4 = ST_forward_fork_sub(in, in->len);

    HUF_decompress_1stream(&in1, pp_out, dtable);
    HUF_decompress_1stream(&in2, pp_out, dtable);
    HUF_decompress_1stream(&in3, pp_out, dtable);
    HUF_decompress_1stream(&in4, pp_out, dtable);
}

/// Initializes a Huffman table using canonical Huffman codes. Codes within a level are allocated in symbol order (i.e. smaller symbols get earlier codes)
/// For more explanation on canonical Huffman codes see https://www.cs.scranton.edu/~mccloske/courses/cmps340/huff_canonical_dec2015.html
static void HUF_init_dtable (HUF_dtable *table, u8 *bits, i32 num_symbs) {
    memset(table, 0, sizeof(HUF_dtable));
    if (num_symbs > HUF_MAX_SYMBS) {
        ERROR("Too many symbols for Huffman");
    }

    u8 max_bits = 0;
    i32 rank_count[HUF_MAX_BITS + 1];
    memset(rank_count, 0, sizeof(rank_count));

    // Count the number of symbols for each number of bits, and determine the depth of the tree
    for (i32 i = 0; i < num_symbs; i++) {
        if (bits[i] > HUF_MAX_BITS) {
            ERROR("Huffman table depth too large");
        }
        rank_count[bits[i]]++;
        if (max_bits < bits[i]) {
            max_bits = bits[i];
        }
    }

    table->max_bits = max_bits;
    table->symbols  = malloc(1 << max_bits);
    table->num_bits = malloc(1 << max_bits);

    if (!table->symbols || !table->num_bits) {
        free(table->symbols);
        free(table->num_bits);
        ERROR_MALLOC();
    }

    // "Symbols are sorted by Weight. Within same Weight, symbols keep natural order. 
    // Symbols with a Weight of zero are removed. Then, starting from lowest weight, prefix codes are distributed in order."
    // symbols根据bits的大小排序，bits大的symbols排在前面。对于bits相同的若干symbol，按natrual order（也即ASCII码）顺序排列

    u64 rank_idx[HUF_MAX_BITS + 1];
    rank_idx[max_bits] = 0;   // Initialize the starting codes for each rank (number of bits)
    for (i32 i = max_bits; i >= 1; i--) {
        rank_idx[i - 1] = rank_idx[i] + rank_count[i] * (1 << (max_bits - i));
        // The entire range takes the same number of bits so we can memset it
        memset(&table->num_bits[rank_idx[i]], i, rank_idx[i - 1] - rank_idx[i]);
    }

    if (rank_idx[0] != (1 << max_bits)) {
        ERROR_CORRUPT();
    }

    // Allocate codes and fill in the table
    for (i32 i = 0; i < num_symbs; i++) {
        if (bits[i] != 0) {
            // Allocate a code for this symbol and set its range in the table
            i32 code = rank_idx[bits[i]];
            // Since the code doesn't care about the bottom `max_bits - bits[i]` bits of state, it gets a range that spans all possible values of the lower bits
            i32 len = 1 << (max_bits - bits[i]);
            memset(&table->symbols[code], i, len);
            rank_idx[bits[i]] += len;
        }
    }
}

static void HUF_free_dtable(HUF_dtable * dtable) {
    free(dtable->symbols);
    free(dtable->num_bits);
    memset(dtable, 0, sizeof(HUF_dtable));
}
/******* END HUFFMAN PRIMITIVES ***********************************************/




/******* FSE PRIMITIVES *******************************************************/
/// For more description of FSE see https://github.com/Cyan4973/FiniteStateEntropy/

static size_t FSE_decompress_interleaved2 (istream_t* in, ostream_t* out, FSE_dtable *dtable) {
    if (in->len == 0) {
        ERROR_IN_SIZE();
    }
    
    ST_backward_prepare(in);
    i32 state1 = ST_backward_read_bits(in, ((u8)dtable->accuracy_log));
    i32 state2 = ST_backward_read_bits(in, ((u8)dtable->accuracy_log));

    for (size_t n_bytes = 0 ; ; ) { // Decode until we overflow the stream Since we decode in reverse order, overflowing the stream is offset going negative
        ST_write_byte(out, dtable->symbols[state1]);
        n_bytes ++;
        state1 = dtable->new_state_base[state1] + ST_backward_read_bits(in, dtable->num_bits[state1]);
        if (in->bit_offset < 0) {   // There's still a symbol to decode in state2
            ST_write_byte(out, dtable->symbols[state2]);
            return n_bytes+1;
        }

        ST_write_byte(out, dtable->symbols[state2]);
        n_bytes ++;
        state2 = dtable->new_state_base[state2] + ST_backward_read_bits(in, dtable->num_bits[state2]);
        if (in->bit_offset < 0) {   // There's still a symbol to decode in state1
            ST_write_byte(out, dtable->symbols[state1]);
            return n_bytes+1;
        }
    }
}

static void FSE_init_dtable (FSE_dtable *dtable, i32 *norm_freqs, i32 num_symbs, i32 accuracy_log) {
    if (accuracy_log > FSE_MAX_ACCURACY_LOG) {
        ERROR("FSE accuracy too large");
    }
    if (num_symbs > FSE_MAX_SYMBS) {
        ERROR("Too many symbols for FSE");
    }

    dtable->accuracy_log = accuracy_log;

    size_t size = (size_t)1 << accuracy_log;
    dtable->symbols = malloc(size * sizeof(u8));
    dtable->num_bits = malloc(size * sizeof(u8));
    dtable->new_state_base = malloc(size * sizeof(i32));

    if (!dtable->symbols || !dtable->num_bits || !dtable->new_state_base) {
        ERROR_MALLOC();
    }

    // Used to determine how many bits need to be read for each state, and where the destination range should start
    // Needs to be i32 because max value is 2 * max number of symbols, which can be larger than a byte can store
    i32 state_desc[FSE_MAX_SYMBS];

    // "Symbols are scanned in their natural order for "less than 1" probabilities. Symbols with this probability are being attributed a
    // single cell, starting from the end of the table. These symbols define a full state reset, reading Accuracy_Log bits."
    i32 high_threshold = size;
    for (i32 s = 0; s < num_symbs; s++) {
        // Scan for low probability symbols to put at the top
        if (norm_freqs[s] == -1) {
            dtable->symbols[--high_threshold] = s;
            state_desc[s] = 1;
        }
    }

    // "All remaining symbols are sorted in their natural order. Starting from symbol 0 and table position 0, each symbol gets attributed as many cells
    // as its probability. Cell allocation is spread, not linear." Place the rest in the table
    i32 step = (size >> 1) + (size >> 3) + 3;
    i32 mask = size - 1;
    i32 pos = 0;
    for (i32 s = 0; s < num_symbs; s++) {
        if (norm_freqs[s] <= 0) {
            continue;
        }

        state_desc[s] = norm_freqs[s];

        for (i32 i = 0; i < norm_freqs[s]; i++) {
            // Give `norm_freqs[s]` states to symbol s
            dtable->symbols[pos] = s;
            // "A position is skipped if already occupied, typically by a "less than 1" probability symbol."
            do {
                pos = (pos + step) & mask;
            } while (pos >= high_threshold);
            // Note: no other collision checking is necessary as `step` is coprime to `size`, so the cycle will visit each position exactly once
        }
    }

    if (pos != 0) {
        ERROR_CORRUPT();
    }

    // Now we can fill baseline and num bits
    for (size_t i = 0; i < size; i++) {
        u8 symbol = dtable->symbols[i];
        i32 next_state_desc = state_desc[symbol]++;
        dtable->num_bits[i] = (u8)(accuracy_log - highest_set_bit(next_state_desc));       // Fills in the table appropriately, next_state_desc increases by symbol over time, decreasing number of bits
        dtable->new_state_base[i] = ((i32)next_state_desc << dtable->num_bits[i]) - size;  // Baseline increases until the bit threshold is passed, at which point it resets to 0
    }
}

/// Decode an FSE header as defined in the Zstandard format specification and use the decoded frequencies to initialize a decoding table.
static void FSE_decode_header (istream_t *in, FSE_dtable *dtable, i32 max_accuracy_log) {
    if (max_accuracy_log > FSE_MAX_ACCURACY_LOG) {
        ERROR("FSE accuracy too large");
    }

    // The bitstream starts by reporting on which scale it operates. Accuracy_Log = low4bits + 5. Note that maximum Accuracy_Log for literal
    // and match lengths is 9, and for offsets is 8. Higher values are considered errors."
    i32 accuracy_log = 5 + ST_forward_read_bits(in, 4);
    if (accuracy_log > max_accuracy_log) {
        ERROR("FSE accuracy too large");
    }

    // "Then follows each symbol value, from 0 to last present one. The number of bits used by each field is variable. It depends on :
    //
    // Remaining probabilities + 1 : example : Presuming an Accuracy_Log of 8, and presuming 100 probabilities points have already been distributed, the
    // decoder may read any value from 0 to 255 - 100 + 1 == 156 (inclusive). Therefore, it must read log2sup(156) == 8 bits.
    //
    // Value decoded : small values use 1 less bit : example : Presuming values from 0 to 156 (inclusive) are possible, 255-156 = 99 values are remaining
    // in an 8-bits field. They are used this way : first 99 values (hence from 0 to 98) use only 7 bits, values from 99 to 156 use 8 bits. "

    i32 remaining = 1 + (1 << accuracy_log);
    i32 frequencies[FSE_MAX_SYMBS];

    i32 symb = 0;
    while (remaining > 1 && symb < FSE_MAX_SYMBS) {
        i32 bits = highest_set_bit(remaining);
        i32 val  = ST_forward_read_bits(in, bits);
        i32 thresh = ((i32)1 << (bits+1)) - 1 - remaining;
        if (val >= thresh) {
            if (ST_forward_read_bits(in, 1)) {
                val |= (1 << bits);
                val -= thresh;
            }
        }

        // "Probability is obtained from Value decoded by following formula : Proba = value - 1"
        i32 proba = (i32)val - 1;

        // "It means value 0 becomes negative probability -1. -1 is a special probability, which means "less than 1". Its effect on distribution
        // table is described in next paragraph. For the purpose of calculating cumulated distribution, it counts as one."
        remaining -= proba < 0 ? -proba : proba;

        frequencies[symb] = proba;
        symb ++;

        // "When a symbol has a probability of zero, it is followed by a 2-bits repeat flag. 
        // This repeat flag tells how many probabilities of zeroes follow the current one.
        // It provides a number ranging from 0 to 3. If it is a 3, another 2-bits repeat flag follows, and so on."
        if (proba == 0) {
            // Read the next two bits to see how many more 0s
            i32 repeat = ST_forward_read_bits(in, 2);

            while (1) {
                for (i32 i = 0; i < repeat && symb < FSE_MAX_SYMBS; i++) {
                    frequencies[symb++] = 0;
                }
                if (repeat == 3) {
                    repeat = ST_forward_read_bits(in, 2);
                } else {
                    break;
                }
            }
        }
    }

    ST_forward_align(in);

    // "When last symbol reaches cumulated total of 1 << Accuracy_Log, decoding is complete. If the last symbol makes cumulated total go above 1 << Accuracy_Log, distribution is considered corrupted."
    if (remaining != 1 || symb >= FSE_MAX_SYMBS) {
        ERROR_CORRUPT();
    }

    // Initialize the decoding table using the determined weights
    FSE_init_dtable(dtable, frequencies, symb, accuracy_log);
}

static void FSE_init_dtable_rle (FSE_dtable *dtable, u8 symb) {
    dtable->symbols = malloc(sizeof(u8));
    dtable->num_bits = malloc(sizeof(u8));
    dtable->new_state_base = malloc(sizeof(i32));

    if (!dtable->symbols || !dtable->num_bits || !dtable->new_state_base) {
        ERROR_MALLOC();
    }

    // This setup will always have a state of 0, always return symbol `symb`, and never consume any bits
    dtable->symbols[0] = symb;
    dtable->num_bits[0] = 0;
    dtable->new_state_base[0] = 0;
    dtable->accuracy_log = 0;
}

static void FSE_free_dtable(FSE_dtable * dtable) {
    free(dtable->symbols);
    free(dtable->num_bits);
    free(dtable->new_state_base);
    memset(dtable, 0, sizeof(FSE_dtable));
}
/******* END FSE PRIMITIVES ***************************************************/




/******* SEQUENCE EXECUTION ***************************************************/

static size_t parse_offset (u64 offset, u64 *prev_offsets, u64 lit_len) {
    if (offset > 3) {
        prev_offsets[2] = prev_offsets[1];
        prev_offsets[1] = prev_offsets[0];
        prev_offsets[0] = (offset - 3);
        return prev_offsets[0];
    } else {
        offset -= ((lit_len == 0) ? 0 : 1);
        if (offset == 0) {
            return prev_offsets[0];
        } else {
            u64 real_offset = (offset < 3) ? prev_offsets[offset] : (prev_offsets[0] - 1);
            if (offset > 1) {
                prev_offsets[2] = prev_offsets[1];
            }
            prev_offsets[1] = prev_offsets[0];
            prev_offsets[0] = real_offset;
            return real_offset;
        }
    }
}

static void execute_sequences (ostream_t *out, u8 *literals, size_t n_lit, u64 *seq_lit_len, u64 *seq_mat_len, u64 *seq_offset, size_t n_seq, frame_context_t *ctx) {
    for (size_t i=0; i<n_seq; i++) {
        u64 lit_len = seq_lit_len[i];
        u64 mat_len = seq_mat_len[i];
        
        memcpy(ST_forward_skip(out, lit_len), literals, lit_len);
        literals += lit_len;
        n_lit -= lit_len;
        ctx->n_bytes_decoded += lit_len;

        size_t offset = parse_offset(seq_offset[i], ctx->prev_offsets, lit_len);
        
        size_t max_offset = ctx->n_bytes_decoded;
        if (max_offset > ctx->window_size) {
            max_offset = ctx->window_size;
        }
        if (offset > max_offset) {
            ERROR_CORRUPT();
        }

        u8 *dst_ptr = ST_forward_skip(out, mat_len);
        ctx->n_bytes_decoded += mat_len;

        for (; mat_len>0; mat_len--) {
            *dst_ptr = *(dst_ptr - offset);
            dst_ptr ++;
        }
    }

    ctx->n_bytes_decoded += n_lit;
    memcpy(ST_forward_skip(out, n_lit), literals, n_lit);    // Copy any leftover literals
}

/******* END SEQUENCE EXECUTION ***********************************************/




/******* SEQUENCE DECODING ****************************************************/

/// The predefined FSE distribution tables for `seq_predefined` mode
static i32 SEQ_LITERAL_LENGTH_DEFAULT_DIST[36] = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};

static i32 SEQ_OFFSET_DEFAULT_DIST[29] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};

static i32 SEQ_MATCH_LENGTH_DEFAULT_DIST[53] = {
    1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1,  1,  1,  1,  1,  1,  1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};

/// The sequence decoding baseline and number of additional bits to read/add. https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#the-codes-for-literals-lengths-match-lengths-and-offsets
static u64 SEQ_LITERAL_LENGTH_BASELINES[36] = {
    0,  1,  2,   3,   4,   5,    6,    7,    8,    9,     10,    11,
    12, 13, 14,  15,  16,  18,   20,   22,   24,   28,    32,    40,
    48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};

static u8 SEQ_LITERAL_LENGTH_EXTRA_BITS[36] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  1,  1,
    1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

static u64 SEQ_MATCH_LENGTH_BASELINES[53] = {
    3,  4,   5,   6,   7,    8,    9,    10,   11,    12,    13,   14, 15, 16,
    17, 18,  19,  20,  21,   22,   23,   24,   25,    26,    27,   28, 29, 30,
    31, 32,  33,  34,  35,   37,   39,   41,   43,    47,    51,   59, 67, 83,
    99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};

static u8 SEQ_MATCH_LENGTH_EXTRA_BITS[53] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  1,  1,  1, 1,
    2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};

/// Given a sequence part and table mode, decode the FSE distribution. Errors if the mode is `seq_repeat` without a pre-existing table in `table`
static void decode_seq_table (istream_t *in, FSE_dtable *table, i32 type, i32 mode) {
    // Constant arrays indexed by seq_part_t
    i32  * default_distributions[] = {SEQ_LITERAL_LENGTH_DEFAULT_DIST, SEQ_OFFSET_DEFAULT_DIST, SEQ_MATCH_LENGTH_DEFAULT_DIST};
    size_t default_distribution_lengths[] = {36, 29, 53};
    size_t default_distribution_accuracies[] = {6, 5, 6};

    size_t max_accuracies[] = {9, 8, 9};

    switch (mode) {
        case 0: { // "Predefined_Mode : uses a predefined distribution table."
            i32 *distribution = default_distributions[type];
            size_t symbs = default_distribution_lengths[type];
            size_t accuracy_log = default_distribution_accuracies[type];
            FSE_free_dtable(table); // Free old one before overwriting
            FSE_init_dtable(table, distribution, symbs, accuracy_log);
            break;
        }
        case 1: { // "RLE_Mode : it's a single code, repeated Number_of_Sequences times."
            u8 symb = ST_forward_skip(in, 1)[0];
            FSE_free_dtable(table); // Free old one before overwriting
            FSE_init_dtable_rle(table, symb);
            break;
        }
        case 2: { // "FSE_Compressed_Mode : standard FSE compression. A distribution table will be present "
            FSE_free_dtable(table); // Free old one before overwriting
            FSE_decode_header(in, table, max_accuracies[type]);
            break;
        }
        default:{ // "Repeat_Mode : reuse distribution table from previous compressed block."
            if (!table->symbols) { // Nothing to do here, table will be unchanged
                ERROR_CORRUPT();   // This mode is invalid if we don't already have a table
            }
            break;
        }
    }
}

/// Decompress the FSE encoded sequence commands
static void decompress_sequences (istream_t *in, u64 *seq_lit_len, u64 *seq_mat_len, u64 *seq_offset, size_t n_seq, frame_context_t *ctx) {
    // Bit number : Field name
    // 7-6        : Literals_Lengths_Mode
    // 5-4        : Offsets_Mode
    // 3-2        : Match_Lengths_Mode
    // 1-0        : Reserved
    u8 compression_modes = ST_forward_read_bits(in, 8);

    if ((compression_modes & 3) != 0) {
        ERROR_CORRUPT(); // Reserved bits set
    }

    // "Following the header, up to 3 distribution tables can be described. When present, they are in this order : Literals lengths, Offsets, Match Lengths"
    decode_seq_table(in, &ctx->ll_dtable, 0, (compression_modes >> 6) & 3);
    decode_seq_table(in, &ctx->of_dtable, 1, (compression_modes >> 4) & 3);
    decode_seq_table(in, &ctx->ml_dtable, 2, (compression_modes >> 2) & 3);

    ST_backward_prepare(in);

    // "The bitstream starts with initial state values, each using the required umber of bits in their respective accuracy, decoded previously from their normalized distribution.
    // It starts by Literals_Length_State, followed by Offset_State, and finally Match_Length_State."
    i32 ll_state, of_state, ml_state;

    for (size_t i=0; i<n_seq; i++) {
        if (i == 0) {
            ll_state = ST_backward_read_bits(in, ctx->ll_dtable.accuracy_log);
            of_state = ST_backward_read_bits(in, ctx->of_dtable.accuracy_log);
            ml_state = ST_backward_read_bits(in, ctx->ml_dtable.accuracy_log);
        } else {
            ll_state = ctx->ll_dtable.new_state_base[ll_state] + ST_backward_read_bits(in, ctx->ll_dtable.num_bits[ll_state]);
            ml_state = ctx->ml_dtable.new_state_base[ml_state] + ST_backward_read_bits(in, ctx->ml_dtable.num_bits[ml_state]);
            of_state = ctx->of_dtable.new_state_base[of_state] + ST_backward_read_bits(in, ctx->of_dtable.num_bits[of_state]);
        }

        // Decode symbols, but don't update states
        u8 ll_code = ctx->ll_dtable.symbols[ll_state];
        u8 of_code = ctx->of_dtable.symbols[of_state];
        u8 ml_code = ctx->ml_dtable.symbols[ml_state];
        
        // Offset doesn't need a max value as it's not decoded using a table
        if (ll_code > MAX_CODE_LIT_LEN || ml_code > MAX_CODE_MAT_LEN) {
            ERROR_CORRUPT();
        }
        
        seq_offset [i] = ((u64)1 << of_code)                   + ST_backward_read_bits(in, of_code);   // "Decoding starts by reading the Number_of_Bits required to decode Offset. It then does the same for Match_Length, and then for Literals_Length."
        seq_mat_len[i] = SEQ_MATCH_LENGTH_BASELINES[ml_code]   + ST_backward_read_bits(in, SEQ_MATCH_LENGTH_EXTRA_BITS[ml_code]);
        seq_lit_len[i] = SEQ_LITERAL_LENGTH_BASELINES[ll_code] + ST_backward_read_bits(in, SEQ_LITERAL_LENGTH_EXTRA_BITS[ll_code]);
    }

    if (in->bit_offset != 0) {
        ERROR_CORRUPT();
    }
}
/******* END SEQUENCE DECODING ************************************************/




/******* LITERALS DECODING ****************************************************/

static void convert_huf_weights_to_bits (u8 *weights, u8 *bits, i32 num_symbs) {
    if (num_symbs+1 > HUF_MAX_SYMBS) {   // +1 because the last weight is not transmitted in the header
        ERROR("Too many symbols for Huffman");
    }

    u64 weight_sum = 0;
    for (i32 i = 0; i < num_symbs; i++) {
        // Weights are in the same range as bit count
        if (weights[i] > HUF_MAX_BITS) {
            ERROR_CORRUPT();
        }
        weight_sum += weights[i] > 0 ? (u64)1 << (weights[i] - 1) : 0;
    }

    // Find the first power of 2 larger than the sum
    i32 max_bits = highest_set_bit(weight_sum) + 1;
    u64 left_over = ((u64)1 << max_bits) - weight_sum;
    
    if (left_over & (left_over - 1)) {
        ERROR_CORRUPT();  // If the left over isn't a power of 2, the weights are invalid
    }

    // left_over is used to find the last weight as it's not transmitted by inverting 2^(weight - 1) we can determine the value of last_weight
    i32 last_weight = highest_set_bit(left_over) + 1;

    for (i32 i = 0; i < num_symbs; i++) {
        bits[i] = weights[i] > 0 ? (max_bits + 1 - weights[i]) : 0;  // "Number_of_Bits = Number_of_Bits ? Max_Number_of_Bits + 1 - Weight : 0"
    }

    bits[num_symbs] = max_bits + 1 - last_weight; // Last weight is always non-zero
}

// Decode the Huffman table description
static void decode_huf_table (istream_t *in, HUF_dtable *dtable) {
    u8 hbyte = ST_forward_read_bits(in, 8);  // "This is a single byte value (0-255), which describes how to decode the list of weights."

    u8 weights[HUF_MAX_SYMBS];
    u8    bits[HUF_MAX_SYMBS];
    memset(weights, 0, sizeof(weights));

    i32 num_symbs;

    if (hbyte >= 128) {  // "This is a direct representation, where each Weight is written directly as a 4 bits field (0-15). The full representation occupies ((Number_of_Symbols+1)/2) bytes, meaning it uses a last full byte even if Number_of_Symbols is odd. Number_of_Symbols = headerByte - 127"
        num_symbs = hbyte - 127;

        u8 *weight_src = ST_forward_skip(in, ((num_symbs + 1)/2));

        for (i32 i = 0; i < num_symbs; i++) {
            // "They are encoded forward, 2 weights to a byte with the first weight taking the top four bits and the second taking the
            // bottom four (e.g. the following operations could be used to read the weights: Weight[0] = (Byte[0] >> 4), Weight[1] = (Byte[0] & 0xf), etc.)."
            if (i % 2 == 0) {
                weights[i] = weight_src[i / 2] >> 4;
            } else {
                weights[i] = weight_src[i / 2] & 0xf;
            }
        }
        
    } else {  // The weights are FSE encoded, decode them before we can construct the table
        istream_t fse_stream    = ST_forward_fork_sub(in, hbyte);
        ostream_t weight_stream = ST_new(weights, HUF_MAX_SYMBS);
        FSE_dtable fse_dtable;
        FSE_decode_header(&fse_stream, &fse_dtable, 7);  // "An FSE bitstream starts by a header, describing probabilities distribution. It will create a Decoding Table. For a list of Huffman weights, maximum accuracy is 7 bits."
        num_symbs = FSE_decompress_interleaved2(&fse_stream, &weight_stream, &fse_dtable);    // Decode the weights
        FSE_free_dtable(&fse_dtable);
    }
    
    convert_huf_weights_to_bits(weights, bits, num_symbs);
    HUF_init_dtable(dtable, bits, num_symbs+1);
}

/// Decodes Huffman compressed literals
static size_t decode_literals_compressed (istream_t *in, u8 **literals, frame_context_t *ctx, i32 block_type) {
    i32 stream_x1 = 0;
    size_t regenerated_size, compressed_size;
     i32 size_format = (i32)ST_forward_read_bits(in, 2);

    switch (size_format) {
        case 0:  // "A single stream. Both Compressed_Size and Regenerated_Size use 10 bits (0-1023)."
            stream_x1 = 1;   // Fall through as it has the same size format
        case 1:  // "4 streams. Both Compressed_Size and Regenerated_Size use 10 bits (0-1023)."
            regenerated_size = ST_forward_read_bits(in, 10);
            compressed_size  = ST_forward_read_bits(in, 10);
            break;
        case 2:  // "4 streams. Both Compressed_Size and Regenerated_Size use 14 bits (0-16383)."
            regenerated_size = ST_forward_read_bits(in, 14);
            compressed_size  = ST_forward_read_bits(in, 14);
            break;
        default: // case3: "4 streams. Both Compressed_Size and Regenerated_Size use 18 bits (0-262143)."
            regenerated_size = ST_forward_read_bits(in, 18);
            compressed_size  = ST_forward_read_bits(in, 18);
            break;
    }
    
    if (regenerated_size > MAX_LITERALS_SIZE) {
        ERROR_CORRUPT();
    }

    if (!(*literals = malloc(regenerated_size+32))) {
        ERROR_MALLOC();
    }
    
    istream_t huf_stream = ST_forward_fork_sub(in, compressed_size);

    if (block_type == 2) {
        HUF_free_dtable(&ctx->literals_dtable);
        decode_huf_table(&huf_stream, &ctx->literals_dtable);  // Decode the provided Huffman table "This section is only present when Literals_Block_Type type is Compressed_Literals_Block (2)."
    } else if (!ctx->literals_dtable.symbols) {   // If the previous Huffman table is being repeated, ensure it exists
        ERROR_CORRUPT();
    }

    u8 *p_lit = *literals;
    if (stream_x1) {
        HUF_decompress_1stream(&huf_stream, &p_lit, &ctx->literals_dtable);
    } else {
        HUF_decompress_4stream(&huf_stream, &p_lit, &ctx->literals_dtable);
    }

    if ((p_lit-(*literals)) != regenerated_size) {
        ERROR_CORRUPT();
    }

    return regenerated_size;
}

/// Decodes literals blocks in raw or RLE form
static size_t decode_literals_simple (istream_t *in, u8 **literals, i32 block_type) {
    i32 size_format = (i32)ST_forward_read_bits(in, 2);

    size_t size;
    switch (size_format) {  // These cases are in the form ?0 In this case, the ? bit is actually part of the size field
        case 0:
        case 2: // "Size_Format uses 1 bit. Regenerated_Size uses 5 bits (0-31)."
            size = (ST_forward_read_bits(in, 4) << 1) + (size_format >> 1);
            break;
        case 1: // "Size_Format uses 2 bits. Regenerated_Size uses 12 bits (0-4095)."
            size = ST_forward_read_bits(in, 12);
            break;
            default: // case3: "Size_Format uses 2 bits. Regenerated_Size uses 20 bits (0-1048575)."
            size = ST_forward_read_bits(in, 20);
            break;
    }

    if (size > MAX_LITERALS_SIZE) {
        ERROR_CORRUPT();
    }

    if (!(*literals = malloc(size))) {
        ERROR_MALLOC();
    }

    switch (block_type) {
        case 0: {   // "Raw_Literals_Block - Literals are stored uncompressed."
             u8 * read_ptr = ST_forward_skip(in, size);
            memcpy(*literals, read_ptr, size);
            break;
        }
        default: {  // case1: "RLE_Literals_Block - Literals consist of a single byte value repeated N times."
             u8 * read_ptr = ST_forward_skip(in, 1);
            memset(*literals, read_ptr[0], size);
            break;
        }
    }

    return size;
}
/******* END LITERALS DECODING ************************************************/




/******* FRAME DECODING ******************************************************/

static void parse_frame_header (istream_t *in, frame_context_t *ctx) {
    // "The first header's byte is called the Frame_Header_Descriptor. It tells which other fields are present. Decoding this byte is enough to tell the size of Frame_Header.
    u8 dictionary_id_flag    = ST_forward_read_bits(in, 2);    // 1-0  Dictionary_ID_flag"
    u8 content_checksum_flag = ST_forward_read_bits(in, 1);    // 2    Content_Checksum_flag
    u8 reserved_bit          = ST_forward_read_bits(in, 1);    // 3    Reserved_bit
    ST_forward_read_bits(in, 1);                               // 4    Unused_bit
    u8 single_segment_flag   = ST_forward_read_bits(in, 1);    // 5    Single_Segment_flag
    u8 frame_content_size_flag = ST_forward_read_bits(in, 2);  // 7-6  Frame_Content_Size_flag

    if (reserved_bit != 0) {
        ERROR_CORRUPT();
    }

    if (dictionary_id_flag) {   // decode dictionary id if it exists
        ERROR("This zst file is compressed using a dictionary, but this decoder do not support dictionary");
    }

    ctx->single_segment_flag   = single_segment_flag;
    ctx->content_checksum_flag = content_checksum_flag;

    // decode window_size if it exists
    if (!single_segment_flag) {
        // Provides guarantees on maximum back-reference distance that will be used within compressed data. 
        // This information is important for decoders to allocate enough memory.
        // Use the algorithm from the specification to compute window size: https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#window_descriptor
        u8 mantissa = ST_forward_read_bits(in, 3);  // mantissa: low  3-bit
        u8 exponent = ST_forward_read_bits(in, 5);  // exponent: high 5-bit
        size_t window_base = (size_t)1 << (10 + exponent);
        size_t window_add = (window_base / 8) * mantissa;
        ctx->window_size = window_base + window_add;
    }

    // decode frame content size if it exists
    if (single_segment_flag || frame_content_size_flag) {
        // "This is the original (uncompressed) size. This information is optional. The Field_Size is provided according to value of
        // Frame_Content_Size_flag. The Field_Size can be equal to 0 (not present), 1, 2, 4 or 8 bytes. Format is little-endian."
        // if frame_content_size_flag == 0 but single_segment_flag is set, we still have a 1 byte field
         i32 bytes_array[] = {1, 2, 4, 8};
         i32 bytes = bytes_array[frame_content_size_flag];

        ctx->frame_content_size = ST_forward_read_bits(in, bytes * 8);
        if (bytes == 2) {
            ctx->frame_content_size += 256;    // "When Field_Size is 2, the offset of 256 is added."
        }
    } else {
        ctx->frame_content_size = 0;
    }

    if (single_segment_flag) {
        // when Single_Segment_flag=1 , the maximum back-reference distance is the content size itself, which can be any value from 1 to 2^64-1 bytes (16 EB)."
        ctx->window_size = ctx->frame_content_size;
    }
}

/// Decompress the data from a frame block by block
static void decompress_blocks_in_frame (istream_t *in, ostream_t *out, frame_context_t *ctx) {
    i32 last_block = 0;
    do {
        last_block = (i32)ST_forward_read_bits(in, 1);
        i32 block_type = (i32)ST_forward_read_bits(in, 2);
        size_t block_len = ST_forward_read_bits(in, 21);   // the compressed length of a block

        switch (block_type) {
            case 0: {  // "Raw_Block - this is an uncompressed block. Block_Size is the number of bytes to read and copy."
                u8 *read_ptr  = ST_forward_skip(in, block_len);
                u8 *write_ptr = ST_forward_skip(out, block_len);
                memcpy(write_ptr, read_ptr, block_len);    // Copy the raw data into the output
                ctx->n_bytes_decoded += block_len;
                break;
            }

            case 1: {  // "RLE_Block - this is a single byte, repeated N times. In which case, Block_Size is the size to regenerate, while the "compressed" block is just 1 byte (the byte to repeat)."
                u8 *read_ptr  = ST_forward_skip(in, 1);
                u8 *write_ptr = ST_forward_skip(out, block_len);
                memset(write_ptr, read_ptr[0], block_len); // Copy `block_len` copies of `read_ptr[0]` to the output
                ctx->n_bytes_decoded += block_len;
                break;
            }

            case 2: {  // "Compressed_Block - this is a Zstandard compressed block, detailed in another section of this specification. Block_Size is the compressed size.
                istream_t in_blk = ST_forward_fork_sub(in, block_len);  // Create a sub-stream for the block
                
                u8  *literals    = NULL;
                u64 *seq_lit_len = NULL;
                u64 *seq_mat_len = NULL;
                u64 *seq_offset  = NULL;
                size_t n_lit, n_seq;
                
                i32 block_type  = (i32)ST_forward_read_bits(&in_blk, 2);

                if (block_type <= 1) {
                    n_lit = decode_literals_simple(&in_blk, &literals, block_type);          // Raw or RLE literals block
                } else {
                    n_lit = decode_literals_compressed(&in_blk, &literals, ctx, block_type); // Huffman compressed literals
                }
                
                // decode Number_of_Sequences. This is a variable size field using between 1 and 3 bytes. Let's call its first byte byte0."
                u8 hbyte = ST_forward_read_bits(&in_blk, 8);
                if (hbyte < 128) {
                    n_seq = hbyte;                                                             // "Number_of_Sequences = byte0 . Uses 1 byte."
                } else if (hbyte < 255) {
                    n_seq = ((hbyte - 128) << 8) + ST_forward_read_bits(&in_blk, 8); // "Number_of_Sequences = ((byte0-128) << 8) + byte1 . Uses 2 bytes."
                } else {
                    n_seq = ST_forward_read_bits(&in_blk, 16) + 0x7F00;              // "Number_of_Sequences = byte1 + (byte2<<8) + 0x7F00 . Uses 3 bytes."
                }
            
                if (n_seq) {
                    seq_lit_len = malloc(3 * n_seq * sizeof(u64));
                    seq_mat_len = seq_lit_len + n_seq;
                    seq_offset  = seq_mat_len + n_seq;
                    if (!seq_lit_len) {
                        ERROR_MALLOC();
                    }
                    decompress_sequences(&in_blk, seq_lit_len, seq_mat_len, seq_offset, n_seq, ctx);
                }

                execute_sequences(out, literals, n_lit, seq_lit_len, seq_mat_len, seq_offset, n_seq, ctx);
                
                free(literals);
                free(seq_lit_len);

                break;
            }

            default: { // case3: "Reserved - this is not a block. This value cannot be used with current version of this specification."
                ERROR_CORRUPT();    
                break;
            }
        }
    } while (!last_block);

    if (ctx->content_checksum_flag) {
        ST_forward_read_bits(in, 32);  // This program does not support checking the checksum, so skip over it if it's present
    }
}

/// Decode a frame that contains compressed data. Not all frames do as there are skippable frames.
/// See https://github.com/facebook/zstd/blob/dev/doc/zstd_compression_format.md#general-structure-of-zstandard-frame-format
static void decode_frame (istream_t *in, ostream_t *out) {
    if (((u64)ST_forward_read_bits(in, 32)) != ZSTD_MAGIC_NUMBER) {
        ERROR("Tried to decode non-ZSTD frame");
    }

    frame_context_t ctx;                      //  Initialize the context that needs to be carried from block to block
    memset(&ctx, 0, sizeof(frame_context_t)); // Most fields in context are correct when initialized to 0

    parse_frame_header(in, &ctx);             // Parse data from the frame header

    // Set up the offset history for the repeat offset commands
    ctx.prev_offsets[0] = 1;
    ctx.prev_offsets[1] = 4;
    ctx.prev_offsets[2] = 8;

    if (ctx.frame_content_size != 0 && ctx.frame_content_size > out->len) {
        ERROR_OUT_SIZE();
    }

    decompress_blocks_in_frame(in, out, &ctx);

    // free frame context
    HUF_free_dtable(&ctx.literals_dtable);
    FSE_free_dtable(&ctx.ll_dtable);
    FSE_free_dtable(&ctx.ml_dtable);
    FSE_free_dtable(&ctx.of_dtable);
    memset(&ctx, 0, sizeof(frame_context_t));\
}
/******* END FRAME DECODING ***************************************************/





/******* ZSTD DECODING API ******************************************************/

size_t ZSTD_decompress (void *src, size_t src_len, void *dst, size_t dst_len) {
    istream_t in  = ST_new(src, src_len);
    ostream_t out = ST_new(dst, dst_len);
    decode_frame(&in, &out);  // Warning: this decoder assumes decompression of a single frame
    return (size_t)(out.ptr - (u8*)dst);
}

size_t ZSTD_get_decompressed_size (void *src, size_t src_len) {
    istream_t in = ST_new(src, src_len);
    if ((u64)ST_forward_read_bits(&in, 32) != ZSTD_MAGIC_NUMBER) {
        ERROR("ZSTD frame magic number did not match");
    }
    frame_context_t ctx;
    parse_frame_header(&in, &ctx);
    if (ctx.frame_content_size == 0 && !ctx.single_segment_flag) {
        return (size_t)-1;
    } else {
        return ctx.frame_content_size;
    }
}

/******* END ZSTD DECODING API ******************************************************/

