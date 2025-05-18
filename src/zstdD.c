#include <stddef.h>   // size_t
#include <stdint.h>   // uint8_t, uint16_t, int32_t, uint64_t
#include <string.h>   // memset, memcpy
#include <stdlib.h>   // malloc, free, exit
#include <stdio.h>    // printf


typedef uint8_t  u8;
typedef uint16_t u16;
typedef int32_t  i32;
typedef uint64_t u64;

#define SKIP_MAGIC_NUMBER_MIN (0x184D2A50U)    // min magic number of skip frame
#define SKIP_MAGIC_NUMBER_MAX (0x184D2A5FU)    // max magic number of skip frame
#define ZSTD_MAGIC_NUMBER     (0xFD2FB528U)    //     magic number of zstd frame
#define ZSTD_BLOCK_SIZE_MAX   (128 * 1024)
#define MAX_SEQ_SIZE          (0x18000)

/// This decoder calls exit(1) when it encounters an error, however a production library should propagate error codes
#define ERROR(msg)               { printf("Error: %s\n", (msg)); exit(1); }
#define ERROR_IF(cond, msg)      { if((cond)) ERROR(msg); }
#define ERROR_I_SIZE_IF(cond)    { ERROR_IF((cond), ("Input buffer smaller than it should be or input is corrupted")); }
#define ERROR_O_SIZE_IF(cond)    { ERROR_IF((cond), ("Output buffer overflow")); }
#define ERROR_CORRUPT_IF(cond)   { ERROR_IF((cond), ("Corruption detected while decompressing")); }
#define ERROR_NOT_ZSTD_IF(cond)  { ERROR_IF((cond), ("This data is not valid ZSTD frame")); }
#define ERROR_MALLOC_IF(cond)    { ERROR_IF((cond), ("Memory allocation error")); }

#define HUF_MAX_BITS     (13)
#define HUF_MAX_SYMBS    (256)
#define HUF_TABLE_LENGTH (1<<HUF_MAX_BITS)
#define FSE_MAX_BITS     (15)
#define FSE_MAX_SYMBS    (256)

#define MAX_LL_CODE      (35)
#define MAX_ML_CODE      (52)

typedef struct {
    u8  table      [(1U<<FSE_MAX_BITS)];
    u8  n_bits     [(1U<<FSE_MAX_BITS)];
    u16 state_base [(1U<<FSE_MAX_BITS)];
    i32 m_bits;    // max_bits
    u8  exist;
} FSE_table;

typedef struct {
    size_t window_size;                // The size of window that we need to be able to contiguously store for references
    u8     checksum_flag;              // 1-bit, Whether or not the content of this frame has a checksum
    
    u64 prev_of [3];                   // The last 3 offsets for the special "repeat offsets".

    u8  buf_lit [ZSTD_BLOCK_SIZE_MAX + 32];
    
    u8  huf_table  [HUF_TABLE_LENGTH]; // 同一个frame内跨block复用的huffman解码表   
    u8  huf_n_bits [HUF_TABLE_LENGTH];
    u8  huf_m_bits;
    u8  huf_table_exist;
    
    FSE_table table_ll;                // 同一个frame内跨block复用的fse解码表   
    FSE_table table_ml;
    FSE_table table_of;
} frame_context_t;



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Returns the bit position of the highest `1` bit in `value`. For example:
///   000 -> -1
///   001 -> 0
///   01x -> 1
///   1xx -> 2
///   ...
static i32 highest_set_bit (u64 value) {
    i32 i = -1;
    while (value) {
        value >>= 1;
        i++;
    }
    return i;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// 正向输入流类型，用于除了 huffman 和 fse 以外的数据读取 (meta-data)  
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    u8 *p;
    u8 *plimit;
    u8  c;
} istream_t;

static istream_t istream_new (u8 *ptr, size_t len) {
    istream_t st;
    st.p      = ptr;
    st.plimit = ptr + len;
    st.c = 0;
    return st;
}

static u8 istream_get_curr_byte (istream_t *p_st) {
    ERROR_I_SIZE_IF(p_st->p >= p_st->plimit);
    return p_st->p[0];
}

static u64 istream_readbytes (istream_t *p_st, u8 n_bytes) {
    u64 value = 0;
    u8  smt = 0;
    ERROR_CORRUPT_IF(p_st->c != 0);
    for (; n_bytes>0; n_bytes--) {
        value |= ((u64)istream_get_curr_byte(p_st)) << smt;
        p_st->p ++;
        smt += 8;
    }
    return value;
}

static u64 istream_readbits (istream_t *p_st, u8 n_bits) {
    u8 bitpos_start = p_st->c;
    u8 bitpos_end   = p_st->c + n_bits;
    u8 bytepos_end  = bitpos_end / 8;
    u64 valueh, valuel=0;
    ERROR_IF(n_bits==0, "why???");
    p_st->c = 0;
    valueh = istream_readbytes(p_st, bytepos_end);
    valueh >>= bitpos_start;
    p_st->c = bitpos_end % 8;
    if (p_st->c) {
        valuel = istream_get_curr_byte(p_st) & ((1 << p_st->c) - 1);
        if (bytepos_end) {
            valuel <<= (bytepos_end*8 - bitpos_start);
        } else {
            valuel >>= bitpos_start;
        }
    }
    return valueh | valuel;
}

static void istream_align (istream_t *p_st) {
    if (p_st->c != 0) {
        p_st->p ++;
        p_st->c = 0;
    }
}

static size_t istream_get_remain_len (istream_t *p_st) {
    ERROR_CORRUPT_IF(p_st->c != 0);
    return (p_st->plimit - p_st->p);
}

static u8 *istream_skip (istream_t* p_st, size_t len) {
    u8 *ptr = p_st->p;
    ERROR_CORRUPT_IF(p_st->c != 0);
    ERROR_I_SIZE_IF(len > (p_st->plimit - p_st->p));
    p_st->p += len;
    return ptr;
}

static istream_t istream_fork_substream (istream_t *p_st, size_t len) {
    return istream_new(istream_skip(p_st, len), len);
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// 反向输入流类型，用于 huffman 或 fse 解码  
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    u8  smt;
    u8  c;
    u8 *pbase;
    u8 *p;
    u64 data;
} backward_stream_t;

/// load 一次就会在 data 中缓存至少 57 bit  
/// 之后可以 read/move 多次，注意多次累计读取的 bit 数量不能超过57，否则 57 bit 会被消耗完，需要重新 load 再进行 read/move  
static void backward_stream_load (backward_stream_t *p_bst) {
    p_bst->p -= (p_bst->c >> 3);
    p_bst->c &= 0x7;
    p_bst->data = *((u64*)p_bst->p);
    p_bst->data <<= p_bst->c;
}

static u64 backward_stream_read (backward_stream_t *p_bst) {
    return p_bst->data >> p_bst->smt;
}

static void backward_stream_move (backward_stream_t *p_bst, u8 n_bits) {
    p_bst->data <<= n_bits;
    p_bst->c     += n_bits;
}

static u64 backward_stream_readmove (backward_stream_t *p_bst, u8 n_bits) {
    u64 res = n_bits ? (p_bst->data >> (64 - n_bits)) : 0;
    p_bst->data <<= n_bits;
    p_bst->c     += n_bits;
    return res;
}

static u8 backward_stream_load_and_judge_ended (backward_stream_t *p_bst) {
    backward_stream_load(p_bst);
    if        ((p_bst->p + 8) <   p_bst->pbase) {
        return 1;
    } else if ((p_bst->p + 8) ==  p_bst->pbase) {
        return (p_bst->c > 0);
    } else {
        return 0;
    }
}

static void backward_stream_check_ended (backward_stream_t *p_bst) {
    backward_stream_load(p_bst);
    ERROR_CORRUPT_IF((p_bst->p + 8) != p_bst->pbase);
    ERROR_CORRUPT_IF(p_bst->c != 0);
}

/// 用 istream_t 对象初始化一个 backward_stream_t 对象 ，用于解码FSE流和huffman流   
static backward_stream_t backward_stream_new (istream_t st, u8 n_bits_for_huf_read) {
    backward_stream_t bst;
    ERROR_CORRUPT_IF(st.c != 0);
    ERROR_CORRUPT_IF(st.p >= st.plimit);
    bst.smt   = sizeof(bst.data)*8 - n_bits_for_huf_read;
    bst.pbase = st.p;
    bst.p     = st.plimit - 8;
    bst.c     = 8 - highest_set_bit(bst.p[7]);
    backward_stream_load(&bst);
    return bst;
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ZSTD 解码相关函数（内部）  
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static i32 decode_fse_freqs (istream_t *p_st_src, i32 *p_freq, i32 m_bits) {
    i32 remaining, n_symb=0;
    remaining = 1 + (1 << m_bits);
    while (remaining > 1 && n_symb < FSE_MAX_SYMBS) {
        i32 bits = highest_set_bit(remaining);
        i32 val  = istream_readbits(p_st_src, bits);
        i32 thresh = (1 << (bits+1)) - 1 - remaining;
        if (val >= thresh) {
            if (istream_readbits(p_st_src, 1)) {
                val |= (1 << bits);
                val -= thresh;
            }
        }
        val --;
        remaining -= val<0 ? -val : val;
        p_freq[n_symb++] = val;
        if (val == 0) {
            u8 i, repeat;
            do {
                repeat = istream_readbits(p_st_src, 2);
                for (i=0; (i<repeat && n_symb<FSE_MAX_SYMBS); i++) {
                    p_freq[n_symb++] = 0;
                }
            } while (repeat == 3);
        }
    }
    ERROR_CORRUPT_IF (remaining != 1 || n_symb >= FSE_MAX_SYMBS);
    istream_align(p_st_src);

    return n_symb;
}


static void build_fse_table (FSE_table *p_ftab, i32 *p_freq, i32 n_symb) {
    i32 state_desc[FSE_MAX_SYMBS];
    i32 pos_limit = 1 << p_ftab->m_bits;
    i32 pos_high  = pos_limit;
    i32 pos = 0;
    i32 step = (pos_limit >> 1) + (pos_limit >> 3) + 3;
    i32 s, i;

    ERROR_CORRUPT_IF(p_ftab->m_bits > FSE_MAX_BITS);
    ERROR_CORRUPT_IF(n_symb > FSE_MAX_SYMBS);

    for (s=0; s<n_symb; s++) {
        if (p_freq[s] == -1) {  // -1是一种特殊的符号频率，代表该symbol频率很低(比1更低)，把他们放在顶部   
            pos_high --;
            p_ftab->table[pos_high] = s;
            state_desc[s] = 1;
        }
    }

    for (s=0; s<n_symb; s++) {
        if (p_freq[s] > 0) {
            state_desc[s] = p_freq[s];
            for (i=0; i<p_freq[s]; i++) {
                p_ftab->table[pos] = s;         // Give `p_freq[s]` states to symbol s  
                do {                            // "A position is skipped if already occupied, typically by a "less than 1" probability symbol."  
                    pos = (pos + step) & (pos_limit - 1);
                } while (pos >= pos_high);      // Note: no other collision checking is necessary as `step` is coprime to `size`, so the cycle will visit each position exactly once  
            }
        }
    }

    ERROR_CORRUPT_IF(pos != 0);
    
    for (i=0; i<pos_limit; i++) {         // fill baseline and num bits  
        u8 symbol = p_ftab->table[i];
        i32 next_state_desc = state_desc[symbol]++;
        p_ftab->n_bits[i] = (u8)(p_ftab->m_bits - highest_set_bit(next_state_desc));      // Fills in the table appropriately, next_state_desc increases by symbol over time, decreasing number of bits  
        p_ftab->state_base[i] = ((i32)next_state_desc << p_ftab->n_bits[i]) - pos_limit;  // Baseline increases until the bit threshold is passed, at which point it resets to 0  
    }
}


static void decode_and_build_fse_table (FSE_table *p_ftab, istream_t *p_st_src, i32 max_m_bits) {
    i32 n_fse_symb;
    i32 p_fse_freq [FSE_MAX_SYMBS] = {0};
    p_ftab->m_bits = 5 + istream_readbits(p_st_src, 4);
    ERROR_CORRUPT_IF(p_ftab->m_bits > max_m_bits);
    n_fse_symb = decode_fse_freqs(p_st_src, p_fse_freq, p_ftab->m_bits);
    build_fse_table(p_ftab, p_fse_freq, n_fse_symb);
}


static size_t decode_huf_weights_by_fse (FSE_table *p_ftab, istream_t *p_st_src, u8 *p_huf_weights) {
    backward_stream_t bst = backward_stream_new(*p_st_src, 0);
    i32 state1 = backward_stream_readmove(&bst, p_ftab->m_bits);
    i32 state2 = backward_stream_readmove(&bst, p_ftab->m_bits);
    size_t i = 0;
    for (;;) {
        p_huf_weights[i++] = p_ftab->table[state1];
        if (backward_stream_load_and_judge_ended(&bst)) return i;
        state1 = p_ftab->state_base[state1] + backward_stream_readmove(&bst, p_ftab->n_bits[state1]);
        p_huf_weights[i++] = p_ftab->table[state2];
        if (backward_stream_load_and_judge_ended(&bst)) return i;
        state2 = p_ftab->state_base[state2] + backward_stream_readmove(&bst, p_ftab->n_bits[state2]);
    }
}


static size_t decode_huf_weights (istream_t *p_st_src, u8 *p_huf_weights) {
    size_t hbyte = istream_readbytes(p_st_src, 1);
    if (hbyte >= 128) {
        u8 i, tmp=0;
        hbyte -= 127;
        for (i=0; i<hbyte; i++) {
            if (i % 2 == 0) {
                tmp = istream_readbytes(p_st_src, 1);
                p_huf_weights[i] = (tmp >> 4);
                tmp &= 0xF;
            } else {
                p_huf_weights[i] = tmp;
            }
        }
        return hbyte;
    } else {
        istream_t st_hufweight = istream_fork_substream(p_st_src, hbyte);
        FSE_table ftab;
        decode_and_build_fse_table(&ftab, &st_hufweight, 7);
        hbyte = decode_huf_weights_by_fse(&ftab, &st_hufweight, p_huf_weights);
        return hbyte;
    }
}


static void convert_huf_weights_to_bits (u8 *p, size_t n_symb) {
    i32 sum=0, left;
    u8  max_bits;
    size_t i;
    for (i=0; i<n_symb-1; i++) {
        ERROR_CORRUPT_IF(p[i] > HUF_MAX_BITS);
        sum += p[i] ? ((u64)1<<(p[i]-1)) : 0;
    }
    max_bits = 1 + highest_set_bit(sum);
    left = (1 << max_bits) - sum;
    ERROR_CORRUPT_IF(left & (left - 1));      // left 必须是2的指数   
    p[n_symb-1] = highest_set_bit(left) + 1;
    for (i=0; i<n_symb; i++) {
        if (p[i]) {
            p[i] = max_bits + 1 - p[i];
        }
    }
}


static void build_huf_table (frame_context_t *p_ctx, u8 *bits, i32 n_symb) {
    i32 i;
    u64 rank_idx   [HUF_MAX_BITS + 1];
    i32 rank_count [HUF_MAX_BITS + 1] = {0};
    p_ctx->huf_m_bits = 0;
    for (i=0; i<n_symb; i++) {
        ERROR_CORRUPT_IF(bits[i] > HUF_MAX_BITS);
        rank_count[bits[i]]++;
        if (p_ctx->huf_m_bits < bits[i]) {
            p_ctx->huf_m_bits = bits[i];
        }
    }
    for (i=0; i<HUF_TABLE_LENGTH; i++) {
        p_ctx->huf_table [i] = 0;
        p_ctx->huf_n_bits[i] = 0;
    }
    rank_idx[p_ctx->huf_m_bits] = 0;   // Initialize the starting codes for each rank (number of bits) 
    for (i=p_ctx->huf_m_bits; i>=1; i--) {
        rank_idx[i - 1] = rank_idx[i] + rank_count[i] * (1 << ((i32)p_ctx->huf_m_bits - i));
        memset(&p_ctx->huf_n_bits[rank_idx[i]], i, rank_idx[i - 1] - rank_idx[i]);  // The entire range takes the same number of bits so we can memset it 
    }
    ERROR_CORRUPT_IF(rank_idx[0] != (1 << p_ctx->huf_m_bits));
    for (i=0; i<n_symb; i++) {  // fill in the table
        if (bits[i] != 0) {
            i32 code = rank_idx[bits[i]];  // Allocate a code for this symbol and set its range in the table 
            i32 len = 1 << ((i32)p_ctx->huf_m_bits - bits[i]);  // Since the code doesn't care about the bottom `m_bits - bits[i]` bits of state, it gets a range that spans all possible values of the lower bits 
            memset(&p_ctx->huf_table[code], i, len);
            rank_idx[bits[i]] += len;
        }
    }
}


static void decode_and_build_huf_table (frame_context_t *p_ctx, istream_t *p_st_src) {
    u8 p_weights_or_bits [HUF_MAX_SYMBS] = {0};
    size_t n_symb = decode_huf_weights(p_st_src, p_weights_or_bits) + 1;  // 最后一个weight不编码，而是算出来的，所以这里要+1  
    ERROR_CORRUPT_IF(n_symb > HUF_MAX_SYMBS);
    convert_huf_weights_to_bits(p_weights_or_bits, n_symb);
    build_huf_table(p_ctx, p_weights_or_bits, n_symb);
}


static void huf_decode_1x1 (frame_context_t *p_ctx, istream_t *p_st_src, size_t n_lit, u8 *p_dst) {
    u8 i;
    backward_stream_t bst = backward_stream_new(*p_st_src, p_ctx->huf_m_bits);
    size_t n_lit_div = n_lit / 5;
    size_t n_lit_rem = n_lit - n_lit_div*5;
    for (; n_lit_div>0; n_lit_div--) {
        backward_stream_load(&bst);
        for (i=0; i<5; i++) {
            u64 entry = backward_stream_read(&bst);
            *(p_dst++) = p_ctx->huf_table[entry];
            backward_stream_move(&bst, p_ctx->huf_n_bits[entry]);
        }
    }
    backward_stream_load(&bst);
    for (; n_lit_rem>0; n_lit_rem--) {
        u64 entry = backward_stream_read(&bst);
        *(p_dst++) = p_ctx->huf_table[entry];
        backward_stream_move(&bst, p_ctx->huf_n_bits[entry]);
    }
    backward_stream_check_ended(&bst);
}


static void huf_decode_4x1 (frame_context_t *p_ctx, istream_t *p_st_src, size_t n_lit, u8 *p_dst) {
    size_t csize1 = istream_readbytes(p_st_src, 2);
    size_t csize2 = istream_readbytes(p_st_src, 2);
    size_t csize3 = istream_readbytes(p_st_src, 2);
    istream_t st1 = istream_fork_substream(p_st_src, csize1);
    istream_t st2 = istream_fork_substream(p_st_src, csize2);
    istream_t st3 = istream_fork_substream(p_st_src, csize3);
    istream_t st4 = *p_st_src;
    size_t n_lit123 = ((n_lit+3) / 4);
    size_t n_lit4   = n_lit - n_lit123 * 3;
    ERROR_CORRUPT_IF(n_lit < 6);
    ERROR_CORRUPT_IF(n_lit123 < n_lit4);
    huf_decode_1x1(p_ctx, &st1, n_lit123, p_dst);
    huf_decode_1x1(p_ctx, &st2, n_lit123, p_dst+n_lit123);
    huf_decode_1x1(p_ctx, &st3, n_lit123, p_dst+n_lit123*2);
    huf_decode_1x1(p_ctx, &st4, n_lit4  , p_dst+n_lit123*3);
}


static size_t decode_literals (frame_context_t *p_ctx, istream_t *p_st_src) {
    u8 lit_type   = istream_readbits(p_st_src, 2);
    u8 n_lit_type = istream_readbits(p_st_src, 2);
    size_t n_lit, huf_size;
    u8 huf_x1 = 0;
    if (lit_type < 2) {
        switch (n_lit_type) {
            case 0:  n_lit = (istream_readbits(p_st_src, 4) << 1);      break;
            case 2:  n_lit = (istream_readbits(p_st_src, 4) << 1) + 1;  break;
            case 1:  n_lit =  istream_readbits(p_st_src, 12);           break;
            default: n_lit =  istream_readbits(p_st_src, 20);           break;
        }
        ERROR_CORRUPT_IF(n_lit > ZSTD_BLOCK_SIZE_MAX);
        if (lit_type == 0) {
            memcpy(p_ctx->buf_lit, istream_skip(p_st_src, n_lit) , n_lit);
        } else {
            memset(p_ctx->buf_lit, istream_readbytes(p_st_src, 1), n_lit);
        }
    } else {
        istream_t st_huf;
        switch (n_lit_type) {
            case 0 : huf_x1 = 1;
            case 1 : n_lit    = istream_readbits(p_st_src, 10);
                     huf_size = istream_readbits(p_st_src, 10);  break;
            case 2 : n_lit    = istream_readbits(p_st_src, 14);
                     huf_size = istream_readbits(p_st_src, 14);  break;
            default: n_lit    = istream_readbits(p_st_src, 18);
                     huf_size = istream_readbits(p_st_src, 18);  break;
        }
        ERROR_CORRUPT_IF(n_lit > ZSTD_BLOCK_SIZE_MAX);
        st_huf = istream_fork_substream(p_st_src, huf_size);
        if (lit_type == 3) {                            // 复用前一个 block 的 huffman table  
            ERROR_CORRUPT_IF(!p_ctx->huf_table_exist);  // huffman table 必须已经存在  
        } else {                                        // 需要解码 huffman table  
            decode_and_build_huf_table(p_ctx, &st_huf);
            p_ctx->huf_table_exist = 1;
        }
        if (huf_x1) {
            huf_decode_1x1(p_ctx, &st_huf, n_lit, p_ctx->buf_lit);
        } else {
            huf_decode_4x1(p_ctx, &st_huf, n_lit, p_ctx->buf_lit);
        }
    }
    return n_lit;
}


static void decode_and_build_ll_or_of_or_ml_fse_table (FSE_table *p_ftab, istream_t *p_st_src, i32 type, i32 mode) {
    switch (mode) {
        case 0: { // Predefined_Mode
            static i32 LL_FREQ_DEFAULT[] = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
            static i32 OF_FREQ_DEFAULT[] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
            static i32 ML_FREQ_DEFAULT[] = {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
            switch (type) {
                case 0 :  p_ftab->m_bits=6;  build_fse_table(p_ftab, LL_FREQ_DEFAULT, sizeof(LL_FREQ_DEFAULT)/sizeof(LL_FREQ_DEFAULT[0]));  break;
                case 1 :  p_ftab->m_bits=5;  build_fse_table(p_ftab, OF_FREQ_DEFAULT, sizeof(OF_FREQ_DEFAULT)/sizeof(OF_FREQ_DEFAULT[0]));  break;
                default:  p_ftab->m_bits=6;  build_fse_table(p_ftab, ML_FREQ_DEFAULT, sizeof(ML_FREQ_DEFAULT)/sizeof(ML_FREQ_DEFAULT[0]));  break;
            }
            break;
        }
        case 1: { // RLE_Mode
            p_ftab->table[0] = istream_readbytes(p_st_src, 1);
            p_ftab->n_bits[0] = 0;
            p_ftab->state_base[0] = 0;
            p_ftab->m_bits = 0;
            break;
        }
        case 2: { // FSE_Compressed_Mode
            const static u8 lut_max_m_bits [] = {9, 8, 9};
            decode_and_build_fse_table(p_ftab, p_st_src, lut_max_m_bits[type]);
            break;
        }
        default:{ // Repeat_Mode
            ERROR_CORRUPT_IF(!p_ftab->exist);
            break;
        }
    }
    p_ftab->exist = 1;
}


static size_t decode_and_build_seq_fse_table (frame_context_t *p_ctx, istream_t *p_st_src) {
    size_t n_seq = istream_readbytes(p_st_src, 1);
    if (n_seq >=255) {
        n_seq   = istream_readbytes(p_st_src, 2) + 0x7F00;
    } else if (n_seq >= 128) {
        n_seq  -= 128;
        n_seq <<= 8;
        n_seq  += istream_readbytes(p_st_src, 1);
    }
    if (n_seq) {
        u8 mode_ml, mode_of, mode_ll;
                  istream_readbits(p_st_src, 2);  // 1-0 : Reserved
        mode_ml = istream_readbits(p_st_src, 2);  // 3-2 : Match_Lengths_Mode
        mode_of = istream_readbits(p_st_src, 2);  // 5-4 : Offsets_Mode
        mode_ll = istream_readbits(p_st_src, 2);  // 7-6 : Literals_Lengths_Mode
        decode_and_build_ll_or_of_or_ml_fse_table(&p_ctx->table_ll, p_st_src, 0, mode_ll);
        decode_and_build_ll_or_of_or_ml_fse_table(&p_ctx->table_of, p_st_src, 1, mode_of);
        decode_and_build_ll_or_of_or_ml_fse_table(&p_ctx->table_ml, p_st_src, 2, mode_ml);
    }
    return n_seq;
}


static u64 parse_offset (u64 *prev_of, u64 of, u64 ll) {
    u64 real_of = of - 3;
    if (of <= 3) {
        of -= ((ll == 0) ? 0 : 1);
        real_of = (of < 3) ? prev_of[of] : prev_of[0]-1;
    }
    switch (of) {
        default :
            prev_of[2] = prev_of[1];
        case 1 :
            prev_of[1] = prev_of[0];
            prev_of[0] = real_of;
        case 0 :
            break;
    }
    return real_of;
}


static void decode_sequences_by_fse_and_execute (frame_context_t *p_ctx, istream_t *p_st_src, size_t n_seq, size_t n_lit, u8 **pp_dst, u8 *p_dst_limit) {
    u8 *p_lit = p_ctx->buf_lit;
    
    if (n_seq) {
        backward_stream_t bst = backward_stream_new(*p_st_src, 0);
        i32 ll_state = backward_stream_readmove(&bst, p_ctx->table_ll.m_bits);
        i32 of_state = backward_stream_readmove(&bst, p_ctx->table_of.m_bits);
        i32 ml_state = backward_stream_readmove(&bst, p_ctx->table_ml.m_bits);
        size_t i = 0;

        for (;;) {
            const static u64 LL_BASELINES[] = {0,  1,  2,  3,  4,  5,  6,  7,    8,    9,     10,    11,12, 13, 14,  15,  16,  18,   20,   22,   24,   28,    32,    40,48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
            const static u64 ML_BASELINES[] = {3,  4,  5,  6,  7,  8,  9, 10,   11,    12,    13,   14, 15, 16,17, 18,  19,  20,  21,   22,   23,   24,   25,    26,    27,   28, 29, 30,31, 32,  33,  34,  35,   37,   39,   41,   43,    47,    51,   59, 67, 83,99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
            const static u8 LL_EXTRA_BITS[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  1,  1,1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
            const static u8 ML_EXTRA_BITS[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0, 0,0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  1,  1,  1, 1,2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
            
            u8 ll_code = p_ctx->table_ll.table[ll_state];
            u8 of_code = p_ctx->table_of.table[of_state];
            u8 ml_code = p_ctx->table_ml.table[ml_state];

            u64 of, ml, ll;
            
            ERROR_CORRUPT_IF(ll_code > MAX_LL_CODE || ml_code > MAX_ML_CODE);

            backward_stream_load(&bst);
            of = ((u64)1 << of_code)   + backward_stream_readmove(&bst, of_code);   // "Decoding starts by reading the Number_of_Bits required to decode Offset. It then does the same for Match_Length, and then for Literals_Length."
            ml = ML_BASELINES[ml_code] + backward_stream_readmove(&bst, ML_EXTRA_BITS[ml_code]);
            ll = LL_BASELINES[ll_code] + backward_stream_readmove(&bst, LL_EXTRA_BITS[ll_code]);

            memcpy(*pp_dst, p_lit, ll);
            (*pp_dst) += ll;
            p_lit += ll;
            n_lit -= ll;
            of = parse_offset(p_ctx->prev_of, of, ll);
            for (; ml>0; ml--) {
                **pp_dst = *(*pp_dst - of);
                (*pp_dst) ++;
            }

            if (++i >= n_seq) break;

            backward_stream_load(&bst);
            ll_state = p_ctx->table_ll.state_base[ll_state] + backward_stream_readmove(&bst, p_ctx->table_ll.n_bits[ll_state]);
            ml_state = p_ctx->table_ml.state_base[ml_state] + backward_stream_readmove(&bst, p_ctx->table_ml.n_bits[ml_state]);
            of_state = p_ctx->table_of.state_base[of_state] + backward_stream_readmove(&bst, p_ctx->table_of.n_bits[of_state]);
        }

        backward_stream_check_ended(&bst);
    }

    memcpy(*pp_dst, p_lit, n_lit);
    (*pp_dst) += n_lit;
}


static void decode_blocks_in_a_frame (frame_context_t *p_ctx, istream_t *p_st_src, u8 **pp_dst, u8 *p_dst_limit) {
    u8 block_last, block_type;
    size_t block_len;
    do {
        block_last = istream_readbits(p_st_src, 1);
        block_type = istream_readbits(p_st_src, 2);
        block_len  = istream_readbits(p_st_src, 21);  // the compressed length of this block
        switch (block_type) {
            case 0:    // Raw_Block
            case 1:    // RLE_Block
                ERROR_O_SIZE_IF(block_len > (p_dst_limit - *pp_dst));
                if (block_type == 0) {
                    memcpy(*pp_dst, istream_skip(p_st_src, block_len), block_len);
                } else {
                    memset(*pp_dst, istream_readbytes(p_st_src, 1)   , block_len);
                }
                (*pp_dst) += block_len;
                break;
            case 2: {  // Compressed_Block
                istream_t st_blk = istream_fork_substream(p_st_src, block_len);
                size_t n_lit = decode_literals(p_ctx, &st_blk);
                size_t n_seq = decode_and_build_seq_fse_table(p_ctx, &st_blk);
                decode_sequences_by_fse_and_execute(p_ctx, &st_blk, n_seq, n_lit, pp_dst, p_dst_limit);
                break;
            }
            default: ERROR_CORRUPT_IF(1);
        }
    } while (!block_last);
    if (p_ctx->checksum_flag) {
        istream_skip(p_st_src, 4);  // This program does not support checking the checksum, so skip it if it's present
    }
}


static void parse_frame_header (istream_t *p_st_src, u8 *p_checksum_flag, size_t *p_window_size, size_t *p_decoded_len) {
    u8 dictionary_id_flag, single_segment_flag, frame_content_size_flag;

    dictionary_id_flag      = istream_readbits(p_st_src, 2);   // 1-0  Dictionary_ID_flag"
    *p_checksum_flag        = istream_readbits(p_st_src, 1);   // 2    checksum_flag
    ERROR_CORRUPT_IF(istream_readbits(p_st_src, 1) != 0);      // 3    Reserved_bit
    istream_readbits(p_st_src, 1);                             // 4    Unused_bit
    single_segment_flag     = istream_readbits(p_st_src, 1);   // 5    Single_Segment_flag
    frame_content_size_flag = istream_readbits(p_st_src, 2);   // 7-6  Frame_Content_Size_flag

    ERROR_IF(dictionary_id_flag, "This zstd data is compressed using a dictionary, but this decoder do not support dictionary");
    
    if (!single_segment_flag) {                                // decode window_size if it exists
        u8 mantissa = istream_readbits(p_st_src, 3);           // mantissa: low  3-bit
        u8 exponent = istream_readbits(p_st_src, 5);           // exponent: high 5-bit
        size_t window_base = ((size_t)1) << (10 + exponent);
        size_t window_add  = (window_base / 8) * mantissa;
        *p_window_size     = window_base + window_add;
    }
    
    if (single_segment_flag || frame_content_size_flag) {      // decode frame content size (decoded_size) if it exists 
        const static i32 bytes_choices[] = {1, 2, 4, 8};
        i32 bytes = bytes_choices[frame_content_size_flag];
        *p_decoded_len  = istream_readbytes(p_st_src, bytes);
        *p_decoded_len += (bytes == 2) ? 256 : 0;              // "When Field_Size is 2, the offset of 256 is added." 
    } else {
        *p_decoded_len = 0;
    }

    if (single_segment_flag) {                                 // when Single_Segment_flag=1
        *p_window_size = *p_decoded_len;                       // the maximum back-reference distance is the content size itself, which can be any value from 1 to 2^64-1 bytes (16 EB)." 
    }
}


static void decode_frame (frame_context_t *p_ctx, istream_t *p_st_src, u8 **pp_dst, u8 *p_dst_limit) {
    u64 magic = istream_readbytes(p_st_src, 4);
    if (magic == ZSTD_MAGIC_NUMBER) {
        size_t decoded_len = 0;
        u8 *p_dst_base = *pp_dst;
        memset(p_ctx, 0, sizeof(*p_ctx));
        p_ctx->prev_of[0] = 1;
        p_ctx->prev_of[1] = 4;
        p_ctx->prev_of[2] = 8;
        parse_frame_header(p_st_src, &p_ctx->checksum_flag, &p_ctx->window_size, &decoded_len);
        if (decoded_len) {
            ERROR_O_SIZE_IF(decoded_len > (p_dst_limit - p_dst_base));
        }
        decode_blocks_in_a_frame(p_ctx, p_st_src, pp_dst, p_dst_limit);
        if (decoded_len) {
            ERROR_CORRUPT_IF(decoded_len != (*pp_dst - p_dst_base));
        }
    } else if (SKIP_MAGIC_NUMBER_MIN <= magic && magic <= SKIP_MAGIC_NUMBER_MAX) {
        size_t skip_frame_len = istream_readbytes(p_st_src, 4);
        istream_skip(p_st_src, skip_frame_len);
        // printf("  skip frame length = %lu\n", skip_frame_len);
    } else {
        ERROR_NOT_ZSTD_IF(1);
    }
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// ZSTD 解码函数（外部可调用） 
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void zstdD (u8 *p_src, size_t src_len, u8 *p_dst, size_t *p_dst_len) {
    u8 *p_dst_base  = p_dst;
    u8 *p_dst_limit = p_dst + (*p_dst_len);
    istream_t st_src = istream_new(p_src, src_len);
    frame_context_t *p_ctx = (frame_context_t*)malloc(sizeof(frame_context_t));
    ERROR_MALLOC_IF(p_ctx == NULL);
    while (istream_get_remain_len(&st_src) > 0) {
        decode_frame(p_ctx, &st_src, &p_dst, p_dst_limit);
    }
    free(p_ctx);
    *p_dst_len = (p_dst - p_dst_base);
}
