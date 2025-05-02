#include <stddef.h>  // size_t
#include <stdint.h>  // uint8_t, uint32_t


#define     DISABLE_DYNAMIC_HUFFMAN    0                                         // set this to 1 to disable dynamic huffman


#define R_OK                            0
#define R_DST_OVERFLOW                  1
#define R_SRC_OVERFLOW                  2

#define RET_WHEN_ERR(err_code)          { if (err_code)  return err_code; }
#define RET_ERR_IF(err_code,condition)  { if (condition) return err_code; }


#ifndef     NULL
#define     NULL                       0
#endif

#ifndef     UINT32_MAX
#define     UINT32_MAX                 0xFFFFFFFFU
#endif

#define     NUM_LITERAL                256                                       // literal      (symbol = 0-255  )
#define     SYMBOL_END                 NUM_LITERAL                               // end_of_block (symbol = 256    )
#define     NUM_LZ77_LEN               29                                        // LZ77_len     (symbol = 257-285)
#define     NUM_SYMBOL                 ( (NUM_LITERAL) + 1 + (NUM_LZ77_LEN) )
#define     NUM_LZ77_DIST              30

#define     MIN_LZ77_LEN               3
#define     MAX_LZ77_LEN               258
#define     MAX_LZ77_DISTANCE          32768

#define     HASH_SIZE                  ((1<<14)-7)
#define     HASH_LEVEL                 4

#define     MAX_HUFFMAN_BITS_LEN       15

#define     SYMBOL_TREE_MERGE_INC      20
#define     DIST_TREE_MERGE_INC        7

#if         DISABLE_DYNAMIC_HUFFMAN
    #define MAX_BLOCK_LEN              16777216
#else
    #define MAX_BLOCK_LEN              32768
#endif



struct StreamWriter_t {
    uint8_t *p_buf;
    uint8_t *p_limit;
    uint8_t  byte;
    uint8_t  mask;
};


static struct StreamWriter_t newStreamWriter (uint8_t *p_buf, uint32_t len) {
    struct StreamWriter_t bs = {p_buf, (p_buf+len), 0x00, 0x01};
    return bs;
}


static int appendBits (struct StreamWriter_t *p_bs, uint32_t bits, uint32_t cnt) {
    //assert(cnt <= 32);
    for (; cnt>0; cnt--) {
        if (bits & 1)
            p_bs->byte |= p_bs->mask;
        bits >>= 1;
        p_bs->mask <<= 1;
        if (p_bs->mask == 0x00) {
            RET_ERR_IF(R_DST_OVERFLOW, (p_bs->p_buf >= p_bs->p_limit));
            *(p_bs->p_buf) = p_bs->byte;
            p_bs->p_buf ++;
            p_bs->byte = 0x00;
            p_bs->mask = 0x01;
        }
    }
    return R_OK;
}


static int alignBitsToBytes (struct StreamWriter_t *p_bs) {
    if (p_bs->mask > 0x01) {
        RET_ERR_IF(R_DST_OVERFLOW, (p_bs->p_buf >= p_bs->p_limit));
        *(p_bs->p_buf) = p_bs->byte;
        p_bs->p_buf ++;
        p_bs->byte = 0x00;
        p_bs->mask = 0x01;
    }
    return R_OK;
}


static int writeValue (uint8_t **pp_dst, uint8_t *p_dst_limit, uint32_t value, uint8_t n_bytes) {
    RET_ERR_IF(R_DST_OVERFLOW, (n_bytes > p_dst_limit - *pp_dst));
    for (; n_bytes>0; n_bytes--) {
        *((*pp_dst)++) = value & 0xFF;
        value >>= 8;
    }
    return R_OK;
}


static uint32_t bitsReverse (uint32_t bits, uint32_t len) {
    uint32_t revbits = 0;
    //assert(len <= 32);
    for (; len>0; len--) {
        revbits <<= 1;
        revbits |= (bits & 1);
        bits >>= 1;
    }
    return revbits;
}


static uint32_t calcCrc32 (uint8_t *p_src, uint32_t len) {
    static const uint32_t TABLE_CRC32 [] = { 0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c };
    uint32_t crc = 0xFFFFFFFF;
    uint8_t *p_end = p_src + len;
    for (; p_src<p_end; p_src++) {
        crc ^= *p_src;
        crc = TABLE_CRC32[crc & 0x0f] ^ (crc >> 4);
        crc = TABLE_CRC32[crc & 0x0f] ^ (crc >> 4);
    }
    return ~crc;
}



static void buildHuffmanLen (uint32_t num, uint32_t count [], uint32_t huffman_len [], uint32_t tree_merge_inc) {
    uint32_t i, group1_no, group2_no;
    
    uint32_t huffman_group [NUM_SYMBOL];
    
    //assert(0<=num && num<=NUM_SYMBOL);
    
    for (i=0; i<num; i++) {
        huffman_len  [i] = 0;
        huffman_group[i] = i + 1;                               // initial: all nodes are not in same sub-tree
    }
    
    for (;;) {
        uint32_t m2c = UINT32_MAX;                              // the minimum 2nd value
        uint32_t m2i = UINT32_MAX;                              // the minimum 2nd value's index
        uint32_t m1c = UINT32_MAX;                              // the minimum 1st value 
        uint32_t m1i = UINT32_MAX;                              // the minimum 1st value's index
        
        // find the minimum 2 values in count[] --------------------
        for (i=0; i<num; i++) {
            if (count[i] > 0) {                                 // skip the values that never appear (count=0)
                if        (count[i] < m1c) {
                    m2c = m1c;
                    m2i = m1i;
                    m1c = count[i];
                    m1i = i;
                } else if (count[i] < m2c) {
                    m2c = count[i];
                    m2i = i;
                }
            }
        }
        
        if (m2i == UINT32_MAX) {                                // if there's only one minimum value found, which means all nodes a merged in one sub-tree
            if (m1i != UINT32_MAX && huffman_len[m1i] == 0)     // a special case : there is only one symbol appears, we should assign a one-node huffman tree for it, set its huffman_len to 1
                huffman_len[m1i] = 1;
            break;
        }
        
        //assert (m1i != UINT32_MAX);
        
        // merge the two sub-trees to one sub-tree --------------------
        count[m1i] += tree_merge_inc;                           // NOTE : to make the merged sub-tree's counter be larger, Avoid trees that are too deep
        count[m1i] += count[m2i];                               // merge the 2nd sub-tree's count to the 1st sub-tree
        //count[m1i] *= 1.3;
        count[m2i] = 0;                                         // clear the 2nd sub-tree's count to zero, since it is merged to the 1st sub-tree
        group1_no = huffman_group[m1i];
        group2_no = huffman_group[m2i];
        
        for (i=0; i<num; i++) {
            if (huffman_group[i] == group1_no || huffman_group[i] == group2_no) {
                huffman_len[i] ++;                              // huffman code bits length (tree depth) +1
                huffman_group[i] = group1_no;                   // set the 2nd sub-tree's number to as same as the 1st sub-tree's
            }
        }
    }
}


static void buildHuffmanBits (uint32_t num, uint32_t huffman_len [], uint32_t huffman_bits []) {
    uint32_t bl_count  [1+MAX_HUFFMAN_BITS_LEN];
    uint32_t next_bits [1+MAX_HUFFMAN_BITS_LEN];
    
    uint32_t i;
    
    for (i=0; i<=MAX_HUFFMAN_BITS_LEN; i++) {
        bl_count [i] = 0;
        next_bits[i] = 0;
    }
    
    for (i=0; i<num; i++) {
        //assert(huffman_len[i] <= MAX_HUFFMAN_BITS_LEN);  // exceed the max huffman bits length (tree depth)
        bl_count[huffman_len[i]] ++;
    }
    
    for (i=2; i<=MAX_HUFFMAN_BITS_LEN; i++)
        next_bits[i] = (next_bits[i-1] + bl_count[i-1]) << 1;
    
    for (i=0; i<num; i++)  {
        uint32_t len = huffman_len[i];
        if (len > 0)
            huffman_bits[i] = bitsReverse(next_bits[len]++, len);
        else
            huffman_bits[i] = 0;
    }
}



static uint32_t hash (uint8_t *p) {
    uint32_t v = (p[0]<<16) | (p[1]<<8) | p[2];
    return v % HASH_SIZE;
}


static uint32_t getLZ77SymbolAndExtraBits (uint32_t lz77_dist, uint32_t lz77_len, uint32_t *p_len_extra_bits, uint32_t *p_len_extra_len, uint32_t *p_dist_symbol, uint32_t *p_dist_extra_bits, uint32_t *p_dist_extra_len) {
    static const uint32_t TABLE_DIST_EXTRA [NUM_LZ77_DIST] = {0,0,0,0,1,1,2, 2, 3, 3, 4, 4, 5, 5,  6,  6,  7,  7,  8,  8,   9,   9,  10,  10,  11,  11,  12,   12,   13,   13};
    static const uint32_t TABLE_DIST_START [NUM_LZ77_DIST] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    
    static const uint32_t TABLE_LEN_EXTRA [NUM_LZ77_LEN] = {0,0,0,0,0,0,0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4,  4,  5,  5,  5,  5,  0};
    static const uint32_t TABLE_LEN_START [NUM_LZ77_LEN] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    
    uint32_t i;
    
    //assert(1 <= lz77_dist && lz77_dist <= MAX_LZ77_DISTANCE);
    //assert(MIN_LZ77_LEN <= lz77_len && lz77_len <= MAX_LZ77_LEN);
    
    for (i=NUM_LZ77_DIST-1; ; i--)
        if (TABLE_DIST_START[i] <= lz77_dist)
            break;
    
    *p_dist_symbol     = i;
    *p_dist_extra_bits = lz77_dist - TABLE_DIST_START[i];
    *p_dist_extra_len  = TABLE_DIST_EXTRA[i];

    for (i=NUM_LZ77_LEN-1; ; i--)
        if (TABLE_LEN_START[i] <= lz77_len)
            break;
    
    *p_len_extra_bits = lz77_len - TABLE_LEN_START[i];
    *p_len_extra_len  = TABLE_LEN_EXTRA[i];
    
    return i + 257;         // return LZ77_len symbol (257-285)
}


static uint32_t getLZ77NextSymbol (uint8_t **pp_src, uint8_t *p_end, uint8_t* hash_table[][HASH_SIZE], uint32_t *p_len_extra_bits, uint32_t *p_len_extra_len, uint32_t *p_dist_symbol, uint32_t *p_dist_extra_bits, uint32_t *p_dist_extra_len) {
    if ((*pp_src) >= p_end) {
        return SYMBOL_END;
    } else {
        uint32_t j, i;
        uint32_t lz77_len_best = 1;
        uint8_t *p_match_best  = NULL;

        if ((*pp_src) + MIN_LZ77_LEN < p_end) {
            uint32_t h = hash(*pp_src);
            
            for (i=0; i<HASH_LEVEL; i++) {
                uint8_t *p_curr  = (*pp_src);
                uint8_t *p_match = hash_table[i][h];
                if (p_match && (p_curr-p_match) <= MAX_LZ77_DISTANCE) {
                    uint32_t lz77_len = 0;
                    while (lz77_len < MAX_LZ77_LEN && p_curr < p_end && (*p_curr == *p_match)) {
                        lz77_len ++;
                        p_curr  ++;
                        p_match ++;
                    }
                    if (lz77_len > MIN_LZ77_LEN || (lz77_len >= MIN_LZ77_LEN && (p_curr-p_match) < 256)) {
                        if (lz77_len_best < lz77_len) {
                            lz77_len_best = lz77_len;
                            p_match_best  = p_match;
                        }
                    }
                }
            }
        }

        for (j=0; j<lz77_len_best; j++) {
            if ((*pp_src) + MIN_LZ77_LEN < p_end) {
                uint32_t h = hash(*pp_src);
                for (i=HASH_LEVEL-1; i>0; i--) {
                    hash_table[i][h] = hash_table[i-1][h];
                }
                hash_table[0][h] = (*pp_src);
            }
            (*pp_src) ++;
        }

        if (p_match_best) {
            return getLZ77SymbolAndExtraBits(((*pp_src)-p_match_best), lz77_len_best, p_len_extra_bits, p_len_extra_len, p_dist_symbol, p_dist_extra_bits, p_dist_extra_len);
        } else {
            return (*pp_src)[-1];
        }
    }
}



static void clearHashTable (uint8_t* hash_table[][HASH_SIZE]) {
    uint32_t i, j;
    for (j=0; j<HASH_LEVEL; j++) {
        for (i=0; i<HASH_SIZE; i++) {
            hash_table[j][i] = NULL;
        }
    }
}



static int deflateBlockDynamicHuffman (uint8_t* hash_table[][HASH_SIZE], struct StreamWriter_t *p_bs, uint8_t *p_src, uint32_t len, uint32_t is_final_block) {
    uint8_t *p_src1 = p_src;
    uint8_t *p_end = p_src + len;
    
    uint32_t symbol_cnt [NUM_SYMBOL] , symbol_huffman_len [NUM_SYMBOL] , symbol_huffman_bits [NUM_SYMBOL];
    uint32_t dist_cnt[NUM_LZ77_DIST] , dist_huffman_len[NUM_LZ77_DIST] , dist_huffman_bits[NUM_LZ77_DIST];
    
    uint32_t symbol=0, len_extra_bits=0, len_extra_len=0, dist_symbol=0, dist_extra_bits=0, dist_extra_len=0, i;
    
    for (i=0; i<NUM_SYMBOL; i++)
        symbol_cnt[i] = 0;
    
    for (i=0; i<NUM_LZ77_DIST; i++)
        dist_cnt[i] = 0;
    
    clearHashTable(hash_table);
    
    // scan block data, get LZ77 symbols, count them to build huffman tree ------------------------------------------------------
    do {
        symbol = getLZ77NextSymbol(&p_src, p_end, hash_table, &len_extra_bits, &len_extra_len, &dist_symbol, &dist_extra_bits, &dist_extra_len);
        
        symbol_cnt[symbol] ++;
        if (symbol >  SYMBOL_END)
            dist_cnt[dist_symbol] ++;
    } while (symbol != SYMBOL_END);
    
    
    // build huffman tree ------------------------------------------------------
    buildHuffmanLen (NUM_LZ77_DIST, dist_cnt, dist_huffman_len, DIST_TREE_MERGE_INC);
    buildHuffmanBits(NUM_LZ77_DIST, dist_huffman_len, dist_huffman_bits);
    
    buildHuffmanLen (NUM_SYMBOL, symbol_cnt, symbol_huffman_len, SYMBOL_TREE_MERGE_INC);
    buildHuffmanBits(NUM_SYMBOL, symbol_huffman_len, symbol_huffman_bits);
    
    
    // write block header ------------------------------------------------------
    RET_WHEN_ERR(appendBits(p_bs, (!!is_final_block), 1));  // final block ?
    RET_WHEN_ERR(appendBits(p_bs, 2, 2));                   // dynamic huffman tree
    
    {
        uint32_t hlit, hdist;
        
        for (hlit=NUM_LZ77_LEN; hlit>0; hlit--)
            if (symbol_huffman_len[NUM_LITERAL+1+hlit-1] != 0)
                break;
        
        for (hdist=NUM_LZ77_DIST-1; hdist>0; hdist--)
            if (dist_huffman_len[hdist] != 0)
                break;
        
        RET_WHEN_ERR(appendBits(p_bs, hlit , 5));           // hlit
        RET_WHEN_ERR(appendBits(p_bs, hdist, 5));           // hdist
        RET_WHEN_ERR(appendBits(p_bs, 19-4 , 4));           // hclen
        
        for (i=0; i<3; i++)
            RET_WHEN_ERR(appendBits(p_bs, 0, 3));
        for (i=0; i<16; i++)
            RET_WHEN_ERR(appendBits(p_bs, 4, 3));
        
        for (i=0; i<NUM_LITERAL+1+hlit; i++)
            RET_WHEN_ERR(appendBits(p_bs, bitsReverse(symbol_huffman_len[i],4), 4));
        
        for (i=0; i<hdist+1; i++)
            RET_WHEN_ERR(appendBits(p_bs, bitsReverse(  dist_huffman_len[i],4), 4));
    }
    
    clearHashTable(hash_table);
    
    // rescan block data, encode as LZ77 and huffman ------------------------------------------------------
    do {
        symbol = getLZ77NextSymbol(&p_src1, p_end, hash_table, &len_extra_bits, &len_extra_len, &dist_symbol, &dist_extra_bits, &dist_extra_len);
        
        //assert(symbol_huffman_len[symbol] > 0);
        RET_WHEN_ERR(appendBits(p_bs, symbol_huffman_bits[symbol], symbol_huffman_len[symbol]));           // write symbol (literal, SYMBOL_END, or LZ77_len_symbol)
        
        if (symbol >  SYMBOL_END) {
            //assert(dist_huffman_len[dist_symbol] > 0);
            RET_WHEN_ERR(appendBits(p_bs, len_extra_bits, len_extra_len));                                 // write extra bits of LZ77_len
            RET_WHEN_ERR(appendBits(p_bs, dist_huffman_bits[dist_symbol], dist_huffman_len[dist_symbol])); // write symbol     of LZ77_distance
            RET_WHEN_ERR(appendBits(p_bs, dist_extra_bits, dist_extra_len));                               // write extra bits of LZ77_distance
        }
    } while (symbol != SYMBOL_END);

    return R_OK;
}



static int deflateBlockFixedHuffman (uint8_t* hash_table[][HASH_SIZE], struct StreamWriter_t *p_bs, uint8_t *p_src, uint32_t len, uint32_t is_final_block) {
    static const uint32_t symbol_huffman_bits [NUM_SYMBOL] = {0x00c, 0x08c, 0x04c, 0x0cc, 0x02c, 0x0ac, 0x06c, 0x0ec, 0x01c, 0x09c, 0x05c, 0x0dc, 0x03c, 0x0bc, 0x07c, 0x0fc, 0x002, 0x082, 0x042, 0x0c2, 0x022, 0x0a2, 0x062, 0x0e2, 0x012, 0x092, 0x052, 0x0d2, 0x032, 0x0b2, 0x072, 0x0f2, 0x00a, 0x08a, 0x04a, 0x0ca, 0x02a, 0x0aa, 0x06a, 0x0ea, 0x01a, 0x09a, 0x05a, 0x0da, 0x03a, 0x0ba, 0x07a, 0x0fa, 0x006, 0x086, 0x046, 0x0c6, 0x026, 0x0a6, 0x066, 0x0e6, 0x016, 0x096, 0x056, 0x0d6, 0x036, 0x0b6, 0x076, 0x0f6, 0x00e, 0x08e, 0x04e, 0x0ce, 0x02e, 0x0ae, 0x06e, 0x0ee, 0x01e, 0x09e, 0x05e, 0x0de, 0x03e, 0x0be, 0x07e, 0x0fe, 0x001, 0x081, 0x041, 0x0c1, 0x021, 0x0a1, 0x061, 0x0e1, 0x011, 0x091, 0x051, 0x0d1, 0x031, 0x0b1, 0x071, 0x0f1, 0x009, 0x089, 0x049, 0x0c9, 0x029, 0x0a9, 0x069, 0x0e9, 0x019, 0x099, 0x059, 0x0d9, 0x039, 0x0b9, 0x079, 0x0f9, 0x005, 0x085, 0x045, 0x0c5, 0x025, 0x0a5, 0x065, 0x0e5, 0x015, 0x095, 0x055, 0x0d5, 0x035, 0x0b5, 0x075, 0x0f5, 0x00d, 0x08d, 0x04d, 0x0cd, 0x02d, 0x0ad, 0x06d, 0x0ed, 0x01d, 0x09d, 0x05d, 0x0dd, 0x03d, 0x0bd, 0x07d, 0x0fd, 0x013, 0x113, 0x093, 0x193, 0x053, 0x153, 0x0d3, 0x1d3, 0x033, 0x133, 0x0b3, 0x1b3, 0x073, 0x173, 0x0f3, 0x1f3, 0x00b, 0x10b, 0x08b, 0x18b, 0x04b, 0x14b, 0x0cb, 0x1cb, 0x02b, 0x12b, 0x0ab, 0x1ab, 0x06b, 0x16b, 0x0eb, 0x1eb, 0x01b, 0x11b, 0x09b, 0x19b, 0x05b, 0x15b, 0x0db, 0x1db, 0x03b, 0x13b, 0x0bb, 0x1bb, 0x07b, 0x17b, 0x0fb, 0x1fb, 0x007, 0x107, 0x087, 0x187, 0x047, 0x147, 0x0c7, 0x1c7, 0x027, 0x127, 0x0a7, 0x1a7, 0x067, 0x167, 0x0e7, 0x1e7, 0x017, 0x117, 0x097, 0x197, 0x057, 0x157, 0x0d7, 0x1d7, 0x037, 0x137, 0x0b7, 0x1b7, 0x077, 0x177, 0x0f7, 0x1f7, 0x00f, 0x10f, 0x08f, 0x18f, 0x04f, 0x14f, 0x0cf, 0x1cf, 0x02f, 0x12f, 0x0af, 0x1af, 0x06f, 0x16f, 0x0ef, 0x1ef, 0x01f, 0x11f, 0x09f, 0x19f, 0x05f, 0x15f, 0x0df, 0x1df, 0x03f, 0x13f, 0x0bf, 0x1bf, 0x07f, 0x17f, 0x0ff, 0x1ff, 0x000, 0x040, 0x020, 0x060, 0x010, 0x050, 0x030, 0x070, 0x008, 0x048, 0x028, 0x068, 0x018, 0x058, 0x038, 0x078, 0x004, 0x044, 0x024, 0x064, 0x014, 0x054, 0x034, 0x074, 0x003, 0x083, 0x043, 0x0c3, 0x023, 0x0a3};
    static const uint32_t symbol_huffman_len  [NUM_SYMBOL] = {    8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     8,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     9,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     7,     8,     8,     8,     8,     8,     8};
    static const uint32_t dist_huffman_bits[NUM_LZ77_DIST] = {0x00, 0x10, 0x08, 0x18, 0x04, 0x14, 0x0c, 0x1c, 0x02, 0x12, 0x0a, 0x1a, 0x06, 0x16, 0x0e, 0x1e, 0x01, 0x11, 0x09, 0x19, 0x05, 0x15, 0x0d, 0x1d, 0x03, 0x13, 0x0b, 0x1b, 0x07, 0x17};
    
    uint8_t *p_end = p_src + len;
    
    uint32_t symbol=0, len_extra_bits=0, len_extra_len=0, dist_symbol=0, dist_extra_bits=0, dist_extra_len=0;
    
    clearHashTable(hash_table);
    
    // write block header ------------------------------------------------------
    RET_WHEN_ERR(appendBits(p_bs, (!!is_final_block), 1));  // final block ?
    RET_WHEN_ERR(appendBits(p_bs, 1, 2));                   // fixed huffman tree
    
    // scan block data, encode as LZ77 and huffman ------------------------------------------------------
    do {
        symbol = getLZ77NextSymbol(&p_src, p_end, hash_table, &len_extra_bits, &len_extra_len, &dist_symbol, &dist_extra_bits, &dist_extra_len);
        
        RET_WHEN_ERR(appendBits(p_bs, symbol_huffman_bits[symbol], symbol_huffman_len[symbol]));  // write symbol (literal, SYMBOL_END, or LZ77_len_symbol)
        
        if (symbol >  SYMBOL_END) {
            RET_WHEN_ERR(appendBits(p_bs, len_extra_bits, len_extra_len));                        // write extra bits of LZ77_len
            RET_WHEN_ERR(appendBits(p_bs, dist_huffman_bits[dist_symbol], 5));                    // write symbol     of LZ77_distance, NOTE that the distance symbol length of fixed huffman tree is always 5 bits
            RET_WHEN_ERR(appendBits(p_bs, dist_extra_bits, dist_extra_len));                      // write extra bits of LZ77_distance
        }
    } while (symbol != SYMBOL_END);

    return R_OK;
}



int deflateEncode (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len) {
    struct StreamWriter_t bs = newStreamWriter(p_dst, (*p_dst_len));
    uint8_t* hash_table [HASH_LEVEL] [HASH_SIZE];
    //uint8_t* (*hash_table) [HASH_SIZE] = (uint8_t*(*)[HASH_SIZE]) malloc (HASH_LEVEL * HASH_SIZE * sizeof(uint8_t*));
    //RET_ERR_IF(R_MALLOC_ERROR,  hash_table == NULL);

    RET_ERR_IF(R_SRC_OVERFLOW,     src_len  > 0xFFFF0000U);
    RET_ERR_IF(R_DST_OVERFLOW, (*p_dst_len) > 0xFFFF0000U);

    if (src_len == 0) {
        RET_WHEN_ERR(deflateBlockFixedHuffman(hash_table, &bs, NULL, 0, 1));                                     // special case : data length = 0, fill a empty block
    } else {
        uint32_t i;
        for (i=0; i<src_len; i+=MAX_BLOCK_LEN) {                                                                 // for all blocks
            struct StreamWriter_t bs_backup = bs;                                                                // backup
            uint32_t is_final_block = (i+MAX_BLOCK_LEN >= src_len);
            uint32_t block_len = is_final_block ? src_len-i : MAX_BLOCK_LEN;
            uint32_t fixed_len, dynamic_len;
            
            RET_WHEN_ERR(deflateBlockFixedHuffman(hash_table, &bs, p_src+i, block_len, is_final_block));         // try fixed huffman
            fixed_len = (bs.p_buf - p_dst);
            
            #if ( !DISABLE_DYNAMIC_HUFFMAN )
                bs = bs_backup;
                RET_WHEN_ERR(deflateBlockDynamicHuffman(hash_table, &bs, p_src+i, block_len, is_final_block));   // try dynamic huffman
                dynamic_len = (bs.p_buf - p_dst);
                
                if (fixed_len <= dynamic_len) {                                                                  // if the fixed huffman gets a shorter result
                    bs = bs_backup;
                    RET_WHEN_ERR(deflateBlockFixedHuffman(hash_table, &bs, p_src+i, block_len, is_final_block)); // redo fixed huffman
                    //printf("block %08x (length=%10d) appies   fixed huffman\n", i, block_len);
                }
            #endif
        }
    }

    RET_WHEN_ERR(alignBitsToBytes(&bs));

    *p_dst_len = (bs.p_buf - p_dst);

    return R_OK;
}



int gzipC (uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len) {
    uint8_t  *p_dst_tmp   = p_dst;
    uint8_t **pp_dst      = &p_dst_tmp;
    uint8_t  *p_dst_limit = p_dst + *p_dst_len;
    size_t    deflate_len;
    
    RET_ERR_IF(R_SRC_OVERFLOW, p_src > (p_src + src_len));
    RET_ERR_IF(R_DST_OVERFLOW, p_dst > p_dst_limit);
    
    RET_WHEN_ERR(writeValue(pp_dst, p_dst_limit, 0x00088B1FU, 4));
    RET_WHEN_ERR(writeValue(pp_dst, p_dst_limit, 0x00000000U, 4));
    RET_WHEN_ERR(writeValue(pp_dst, p_dst_limit,     0x0304U, 2));

    deflate_len = p_dst_limit - (*pp_dst);
    RET_WHEN_ERR(deflateEncode(p_src, src_len, *pp_dst, &deflate_len));
    (*pp_dst) += deflate_len;
    
    RET_WHEN_ERR(writeValue(pp_dst, p_dst_limit, calcCrc32(p_src, src_len), 4));
    RET_WHEN_ERR(writeValue(pp_dst, p_dst_limit,                  src_len , 4));
    
    *p_dst_len = (*pp_dst) - p_dst;

    return R_OK;
}
