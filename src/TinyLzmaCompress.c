// TinyLZMA
// Source from https://github.com/WangXuan95/TinyLzma


#include <stdlib.h>                     // this code only use malloc and free

#include "TinyLzmaCompress.h"



// the code only use these basic types :
//    int      : as return code
//    uint8_t  : as compressed and uncompressed data, as LZMA state
//    uint16_t : as probabilities of range coder
//    uint32_t : as generic integers
//    size_t   : as data length



#define RET_IF_ERROR(expression)  {     \
    int res = (expression);             \
    if (res != R_OK)                    \
        return res;                     \
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// common useful functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t bitsReverse (uint32_t bits, uint32_t bit_count) {
    uint32_t revbits = 0;
    for (; bit_count>0; bit_count--) {
        revbits <<= 1;
        revbits |= (bits & 1);
        bits >>= 1;
    }
    return revbits;
}


static uint32_t countBit (uint32_t val) {         // count bits after the highest bit '1'
    uint32_t count = 0;
    for (; val!=0; val>>=1)
       count ++;
    return count;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Range Encoder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define   RANGE_CODE_NORMALIZE_THRESHOLD           (1 << 24)
#define   RANGE_CODE_MOVE_BITS                     5
#define   RANGE_CODE_N_BIT_MODEL_TOTAL_BITS        11
#define   RANGE_CODE_BIT_MODEL_TOTAL               (1 << RANGE_CODE_N_BIT_MODEL_TOTAL_BITS)
#define   RANGE_CODE_HALF_PROBABILITY              (RANGE_CODE_BIT_MODEL_TOTAL >> 1)

#define   RANGE_CODE_CACHE_SIZE_MAX                (~((size_t)0))


typedef struct {
    uint8_t  overflow;
    uint8_t  cache;
    uint8_t  low_msb;            // the 32th bit (high 1 bit) of "low"
    uint32_t low_lsb;            // the 31~0th bit (low 32 bits) of "low". Note that ((low_msb<<32) | low_lsb) forms a 33-bit unsigned integer. The goal is to avoid using 64-bit integer type.
    uint32_t range;
    size_t   cache_size;
    uint8_t *p_dst;
    uint8_t *p_dst_limit;
} RangeEncoder_t;


static RangeEncoder_t newRangeEncoder (uint8_t *p_dst, size_t dst_len) {
    RangeEncoder_t coder;
    coder.cache       = 0;
    coder.low_msb     = 0;
    coder.low_lsb     = 0;
    coder.range       = 0xFFFFFFFF;
    coder.cache_size  = 1;
    coder.p_dst       = p_dst;
    coder.p_dst_limit = p_dst + dst_len;
    coder.overflow    = 0;
    return coder;
}


static void rangeEncodeOutByte (RangeEncoder_t *e, uint8_t byte) {
    if (e->p_dst != e->p_dst_limit)
        *(e->p_dst++) = byte;
    else
        e->overflow = 1;
}


static void rangeEncodeNormalize (RangeEncoder_t *e) {
    if (e->range < RANGE_CODE_NORMALIZE_THRESHOLD) {
        if (e->low_msb) {                                  // if "low" is greater than or equal to (1<<32)
            rangeEncodeOutByte(e, e->cache+1);
            for (; e->cache_size>1; e->cache_size--)
                rangeEncodeOutByte(e, 0x00);
            e->cache = (uint8_t)((e->low_lsb) >> 24);
            e->cache_size = 0;
            
        } else if (e->low_lsb < 0xFF000000) {              // if "low" is less than ((1<<32)-(1<<24))
            rangeEncodeOutByte(e, e->cache);
            for (; e->cache_size>1; e->cache_size--)
                rangeEncodeOutByte(e, 0xFF);
            e->cache = (uint8_t)((e->low_lsb) >> 24);
            e->cache_size = 0;
        }
        
        if (e->cache_size < RANGE_CODE_CACHE_SIZE_MAX)
            e->cache_size ++;
        
        e->low_msb = 0;
        e->low_lsb <<= 8;
        e->range <<= 8;
    }
}


static void rangeEncodeTerminate (RangeEncoder_t *e) {
    e->range = 0;
    rangeEncodeNormalize(e);
    rangeEncodeNormalize(e);
    rangeEncodeNormalize(e);
    rangeEncodeNormalize(e);
    rangeEncodeNormalize(e);
    rangeEncodeNormalize(e);
}


static void rangeEncodeIntByFixedProb (RangeEncoder_t *e, uint32_t val, uint32_t bit_count) {
    for (; bit_count>0; bit_count--) {
        uint8_t bit = 1 & (val >> (bit_count-1));
        rangeEncodeNormalize(e);
        e->range >>= 1;
        if (bit) {
            if ((e->low_lsb + e->range) < e->low_lsb)     // if low_lsb + range overflow from 32-bit unsigned integer
                e->low_msb = 1;
            e->low_lsb += e->range;
        }
    }
}


static void rangeEncodeBit (RangeEncoder_t *e, uint16_t *p_prob, uint8_t bit) {
    uint32_t prob = *p_prob;
    uint32_t bound;
    
    rangeEncodeNormalize(e);
    
    bound = (e->range >> RANGE_CODE_N_BIT_MODEL_TOTAL_BITS) * prob;
    
    if (!bit) {                                           // encode bit 0
        e->range = bound;
        *p_prob = (uint16_t)(prob + ((RANGE_CODE_BIT_MODEL_TOTAL - prob) >> RANGE_CODE_MOVE_BITS));
    } else {                                              // encode bit 1
        e->range -= bound;
        if ((e->low_lsb + bound) < e->low_lsb)            // if low_lsb + bound overflow from 32-bit unsigned integer
            e->low_msb = 1;
        e->low_lsb += bound;
        *p_prob = (uint16_t)(prob - (prob >> RANGE_CODE_MOVE_BITS));
    }
}


static void rangeEncodeInt (RangeEncoder_t *e, uint16_t *p_prob, uint32_t val, uint32_t bit_count) {
    uint32_t treepos = 1;
    for (; bit_count>0; bit_count--) {
        uint8_t bit = (uint8_t)(1 & (val >> (bit_count-1)));
        rangeEncodeBit(e, p_prob+(treepos-1), bit);
        treepos <<= 1;
        if (bit)
            treepos |= 1;
    }
}


static void rangeEncodeMB (RangeEncoder_t *e, uint16_t *p_prob, uint32_t byte, uint32_t match_byte) {
    uint32_t i, treepos = 1, off0 = 0x100, off1;
    for (i=0; i<8; i++) {
        uint8_t bit = (uint8_t)(1 & (byte >> 7));
        byte <<= 1;
        match_byte <<= 1;
        off1 = off0;
        off0 &= match_byte;
        rangeEncodeBit(e, p_prob+(off0+off1+treepos-1), bit);
        treepos <<= 1;
        if (bit)
            treepos |= 1;
        else
            off0 ^= off1;
    }
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LZ {length, distance} searching algorithm
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define    LZ_LEN_MAX                          273
//#define    LZ_DIST_MAX_PLUS1                   0xFFFFFFFF
#define    LZ_DIST_MAX_PLUS1                   0x40000000

#define    HASH_LEVEL                          16
#define    HASH_N                              21
#define    HASH_SIZE                           (1<<HASH_N)
#define    HASH_MASK                           ((1<<HASH_N)-1)

#define    INVALID_HASH_ITEM                   (~((size_t)0))               // use maximum value of size_t as invalid hash entry

#define    INIT_HASH_TABLE(hash_table) {            \
    uint32_t i, j;                                  \
    for (i=0; i<HASH_SIZE; i++)                     \
        for (j=0; j<HASH_LEVEL; j++)                \
            hash_table[i][j] = INVALID_HASH_ITEM;   \
}


static uint32_t getHash (const uint8_t *p_src, size_t src_len, size_t pos) {
    if (pos >= src_len || pos+1 == src_len || pos+2 == src_len)
        return 0 ;
    else
#if HASH_N < 24
        return ((p_src[pos+2]<<16) + (p_src[pos+1]<<8) + p_src[pos]) & HASH_MASK;
#else
        return ((p_src[pos+2]<<16) + (p_src[pos+1]<<8) + p_src[pos]);
#endif
}


static void updateHashTable (const uint8_t *p_src, size_t src_len, size_t pos, size_t hash_table [][HASH_LEVEL]) {
    const uint32_t hash = getHash(p_src, src_len, pos);
    uint32_t i, oldest_i = 0;
    size_t   oldest_pos = INVALID_HASH_ITEM;
    
    if (pos >= src_len)
        return;
    
    for (i=0; i<HASH_LEVEL; i++) {
        if (hash_table[hash][i] == INVALID_HASH_ITEM) {      // find a invalid (empty) hash item
            hash_table[hash][i] = pos;                       // fill it
            return;                                          // return immediently
        }
        if (oldest_pos > hash_table[hash][i]) {              // search the oldest hash item
            oldest_pos = hash_table[hash][i];
            oldest_i   = i;
        }
    }
    
    hash_table[hash][oldest_i] = pos;
}


static uint32_t lenDistScore (uint32_t len, uint32_t dist, uint32_t rep0, uint32_t rep1, uint32_t rep2, uint32_t rep3) {
    #define D 12
    static const uint32_t TABLE_THRESHOLDS [] = {D*D*D*D*D*5, D*D*D*D*4, D*D*D*3, D*D*2, D};
    uint32_t score;
    
    if (dist == rep0 || dist == rep1 || dist == rep2 || dist == rep3) {
        score = 5;
    } else {
        for (score=4; score>0; score--)
            if (dist <= TABLE_THRESHOLDS[score])
                break;
    }
    
    if      (len <  2)
        return 8 + 5;
    else if (len == 2)
        return 8 + score + 1;
    else
        return 8 + score + len;
}


static void lzSearchMatch (const uint8_t *p_src, size_t src_len, size_t pos, size_t hash_table [][HASH_LEVEL], uint32_t *p_len, uint32_t *p_dist) {
    const uint32_t len_max = ((src_len-pos) < LZ_LEN_MAX) ? (src_len-pos) : LZ_LEN_MAX;
    const uint32_t hash = getHash(p_src, src_len, pos);
    uint32_t i, j, score1, score2;
    
    *p_len  = 0;
    *p_dist = 0;
    
    score1 = lenDistScore(0, 0xFFFFFFFF, 0, 0, 0, 0);
    
    for (i=0; i<HASH_LEVEL+2; i++) {
        size_t ppos = (i<HASH_LEVEL) ? hash_table[hash][i] : (pos-1-(i-HASH_LEVEL));
        if (ppos != INVALID_HASH_ITEM && ppos < pos && (pos - ppos) < LZ_DIST_MAX_PLUS1) {
            for (j=0; j<len_max; j++)
                if (p_src[pos+j] != p_src[ppos+j])
                    break;
            score2 = lenDistScore(j, (pos-ppos), 0, 0, 0, 0);
            if (j >= 2 && score1 < score2) {
                score1  = score2;
                *p_len  = j;
                *p_dist = pos - ppos;
            }
        }
    }
}


static void lzSearchRep (const uint8_t *p_src, size_t src_len, size_t pos, uint32_t rep0, uint32_t rep1, uint32_t rep2, uint32_t rep3, uint32_t len_limit, uint32_t *p_len, uint32_t *p_dist) {
    uint32_t len_max = ((src_len-pos) < LZ_LEN_MAX) ? (src_len-pos) : LZ_LEN_MAX;
    uint32_t reps [4];
    uint32_t i, j;
    
    if (len_max > len_limit)
        len_max = len_limit;
    
    reps[0] = rep0;   reps[1] = rep1;   reps[2] = rep2;   reps[3] = rep3;
    
    *p_len  = 0;
    *p_dist = 0;
    
    for (i=0; i<4; i++) {
        if (reps[i] <= pos) {
            size_t ppos = pos - reps[i];
            for (j=0; j<len_max; j++)
                if (p_src[pos+j] != p_src[ppos+j])
                    break;
            if (j >= 2 && j > *p_len) {
                *p_len  = j;
                *p_dist = reps[i];
            }
        }
    }
}


static void lzSearch (const uint8_t *p_src, size_t src_len, size_t pos, uint32_t rep0, uint32_t rep1, uint32_t rep2, uint32_t rep3, size_t hash_table [][HASH_LEVEL], uint32_t *p_len, uint32_t *p_dist) {
    uint32_t rlen, rdist;
    uint32_t mlen, mdist;
    
    lzSearchRep(p_src, src_len, pos, rep0, rep1, rep2, rep3, 0xFFFFFFFF, &rlen, &rdist);
    lzSearchMatch(p_src, src_len, pos, hash_table, &mlen, &mdist);
    
    if ( lenDistScore(rlen, rdist, rep0, rep1, rep2, rep3) >= lenDistScore(mlen, mdist, rep0, rep1, rep2, rep3) ) {
        *p_len  = rlen;
        *p_dist = rdist;
    } else {
        *p_len  = mlen;
        *p_dist = mdist;
    }
}


static uint8_t isShortRep (const uint8_t *p_src, size_t src_len, size_t pos, uint32_t rep0) {
    return (pos >= rep0 && (p_src[pos] == p_src[pos-rep0])) ? 1 : 0;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LZMA Encoder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum {          // packet_type
    PKT_LIT,
    PKT_MATCH,
    PKT_SHORTREP,
    PKT_REP0,           // LONGREP0
    PKT_REP1,           // LONGREP1
    PKT_REP2,           // LONGREP2
    PKT_REP3            // LONGREP3
} PACKET_t;


static uint8_t stateTransition (uint8_t state, PACKET_t type) {
    switch (state) {
        case  0 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  1 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  2 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  3 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  4 : return (type==PKT_LIT) ?  1 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  5 : return (type==PKT_LIT) ?  2 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  6 : return (type==PKT_LIT) ?  3 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  7 : return (type==PKT_LIT) ?  4 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case  8 : return (type==PKT_LIT) ?  5 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case  9 : return (type==PKT_LIT) ?  6 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case 10 : return (type==PKT_LIT) ?  4 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case 11 : return (type==PKT_LIT) ?  5 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        default : return 0xFF;                                                                              // 0xFF is invalid state which will never appear
    }
}



#define   N_STATES                                  12
#define   N_LIT_STATES                              7

#define   LC                                        4                  // valid range : 0~8
#define   N_PREV_BYTE_LC_MSBS                       (1 << LC)
#define   LC_SHIFT                                  (8 - LC)
#define   LC_MASK                                   ((1 << LC) - 1)

#define   LP                                        0                  // valid range : 0~4
#define   N_LIT_POS_STATES                          (1 << LP)
#define   LP_MASK                                   ((1 << LP) - 1)

#define   PB                                        3                  // valid range : 0~4
#define   N_POS_STATES                              (1 << PB)
#define   PB_MASK                                   ((1 << PB) - 1)

#define   LCLPPB_BYTE                               ((uint8_t)( (PB * 5 + LP) * 9 + LC ))


#define   INIT_PROBS(probs)                         {                  \
    uint16_t *p = (uint16_t*)(probs);                                  \
    uint16_t *q = p + (sizeof(probs) / sizeof(uint16_t));              \
    for (; p<q; p++)                                                   \
        *p = RANGE_CODE_HALF_PROBABILITY;                              \
}                                                                       // all probabilities are init to 50% (half probability)


static int lzmaEncode (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, uint8_t with_end_mark) {
    uint8_t  state = 0;                           // valid value : 0~12
    size_t   pos   = 0;                           // position of uncompressed data (p_dst)
    uint32_t rep0  = 1;
    uint32_t rep1  = 1;
    uint32_t rep2  = 1;
    uint32_t rep3  = 1;
    uint32_t n_bypass=0, len_bypass=0, dist_bypass=0;
    
    RangeEncoder_t coder = newRangeEncoder(p_dst, *p_dst_len);
    
    // probability arrays ---------------------------------------
    uint16_t probs_is_match     [N_STATES] [N_POS_STATES] ;
    uint16_t probs_is_rep       [N_STATES] ;
    uint16_t probs_is_rep0      [N_STATES] ;
    uint16_t probs_is_rep0_long [N_STATES] [N_POS_STATES] ;
    uint16_t probs_is_rep1      [N_STATES] ;
    uint16_t probs_is_rep2      [N_STATES] ;
    uint16_t probs_literal      [N_LIT_POS_STATES] [N_PREV_BYTE_LC_MSBS] [3*(1<<8)];
    uint16_t probs_dist_slot    [4]  [(1<<6)-1];
    uint16_t probs_dist_special [10] [(1<<5)-1];
    uint16_t probs_dist_align   [(1<<4)-1];
    uint16_t probs_len_choice   [2];
    uint16_t probs_len_choice2  [2];
    uint16_t probs_len_low      [2] [N_POS_STATES] [(1<<3)-1];
    uint16_t probs_len_mid      [2] [N_POS_STATES] [(1<<3)-1];
    uint16_t probs_len_high     [2] [(1<<8)-1];
    
    // size_t hash_table [HASH_SIZE][HASH_LEVEL];                                            // if HASH_LEVEL and HASH_SIZE is small, you can use this instead of malloc
    
    size_t (*hash_table) [HASH_LEVEL];
    
    hash_table = (size_t (*) [HASH_LEVEL]) malloc (sizeof(size_t) * HASH_SIZE * HASH_LEVEL); // if HASH_LEVEL and HASH_SIZE is large, we must use malloc instead of local variables to prevent stack-overflow
    
    if (hash_table == 0)
        return R_ERR_MEMORY_RUNOUT;
    
    INIT_HASH_TABLE(hash_table);
    
    INIT_PROBS(probs_is_match);
    INIT_PROBS(probs_is_rep);
    INIT_PROBS(probs_is_rep0);
    INIT_PROBS(probs_is_rep0_long);
    INIT_PROBS(probs_is_rep1);
    INIT_PROBS(probs_is_rep2);
    INIT_PROBS(probs_literal);
    INIT_PROBS(probs_dist_slot);
    INIT_PROBS(probs_dist_special);
    INIT_PROBS(probs_dist_align);
    INIT_PROBS(probs_len_choice);
    INIT_PROBS(probs_len_choice2);
    INIT_PROBS(probs_len_low);
    INIT_PROBS(probs_len_mid);
    INIT_PROBS(probs_len_high);
    
    while (!coder.overflow) {
        const uint32_t lit_pos_state = LP_MASK & (uint32_t)pos;
        const uint32_t pos_state     = PB_MASK & (uint32_t)pos;
        uint32_t curr_byte=0, match_byte=0, prev_byte_lc_msbs=0;
        uint32_t dist=0, len=0;
        PACKET_t type;
        
        if (pos < src_len)
            curr_byte = p_src[pos];
        
        if (pos > 0) {
            match_byte        =  p_src[pos-rep0];
            prev_byte_lc_msbs = (p_src[pos-1] >> LC_SHIFT) & LC_MASK;
        }
        
        if (pos >= src_len) {                                                    // input end (no more data to be encoded)
            if (!with_end_mark)                                                  // if user dont want to encode end marker
                break;                                                           // finish immediently
            with_end_mark = 0;                                                   // clear with_end_mark. we will finish at the next loop
            type = PKT_MATCH;                                                    // the end marker is regarded as a MATCH packet
            len  = 2;                                                            // this MATCH packet's len = 2
            dist = 0;                                                            // this MATCH packet's dist = 0, in next steps, we will encode dist-1 (0xFFFFFFFF), aka end marker
        
        } else {                                                                 // there are still data need to be encoded
            if (n_bypass > 0) {
                len  = 0;
                dist = 0;
                n_bypass --;
            } else if (len_bypass > 0) {
                len  = len_bypass;
                dist = dist_bypass;
                len_bypass  = 0;
                dist_bypass = 0;
            } else {
                lzSearch(p_src, src_len, pos, rep0, rep1, rep2, rep3, hash_table, &len, &dist);
                
                if ((src_len-pos)>8 && len>=2) {
                    const uint32_t score0 = lenDistScore(len, dist, rep0, rep1, rep2, rep3);
                    uint32_t len1=0, dist1=0, score1=0;
                    uint32_t len2=0, dist2=0, score2=0;
                    
                    lzSearch(p_src, src_len, pos+1, rep0, rep1, rep2, rep3, hash_table, &len1, &dist1);
                    score1 = lenDistScore(len1, dist1, rep0, rep1, rep2, rep3);
                    
                    if (len >= 3) {
                        lzSearch(p_src, src_len, pos+2, rep0, rep1, rep2, rep3, hash_table, &len2, &dist2);
                        score2 = lenDistScore(len2, dist2, rep0, rep1, rep2, rep3) - 1;
                    }
                    
                    if (score2 > score0 && score2 > score1) {
                        len  = 0;
                        dist = 0;
                        lzSearchRep(p_src, src_len, pos, rep0, rep1, rep2, rep3, 2, &len, &dist);
                        len_bypass  = len2;
                        dist_bypass = dist2;
                        n_bypass = (len<2) ? 1 : 0;
                    } else if (score1 > score0) {
                        len  = 0;
                        dist = 0;
                        len_bypass  = len1;
                        dist_bypass = dist1;
                        n_bypass = 0;
                    }
                }
            }
            
            if        (len <  2) {
                type = isShortRep(p_src, src_len, pos, rep0) ? PKT_SHORTREP : PKT_LIT;
            } else if (dist == rep0) {
                type = PKT_REP0;
            } else if (dist == rep1) {
                type = PKT_REP1;
                rep1 = rep0;
                rep0 = dist;
            } else if (dist == rep2) {
                type = PKT_REP2;
                rep2 = rep1;
                rep1 = rep0;
                rep0 = dist;
            } else if (dist == rep3) {
                type = PKT_REP3;
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                rep0 = dist;
            } else {
                type = PKT_MATCH;
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                rep0 = dist;
            }
            
            {
                const size_t pos2 = pos + ((type==PKT_LIT || type==PKT_SHORTREP) ? 1 : len);
                for (; pos<pos2; pos++)
                    updateHashTable(p_src, src_len, pos, hash_table);
            }
        }
        
        switch (type) {
            case PKT_LIT :
                rangeEncodeBit(&coder, &probs_is_match    [state][pos_state], 0);
                break;
            case PKT_MATCH :
                rangeEncodeBit(&coder, &probs_is_match    [state][pos_state], 1);
                rangeEncodeBit(&coder, &probs_is_rep      [state]           , 0);
                break;
            case PKT_SHORTREP :
                rangeEncodeBit(&coder, &probs_is_match    [state][pos_state], 1);
                rangeEncodeBit(&coder, &probs_is_rep      [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep0     [state]           , 0);
                rangeEncodeBit(&coder, &probs_is_rep0_long[state][pos_state], 0);
                break;
            case PKT_REP0     :
                rangeEncodeBit(&coder, &probs_is_match    [state][pos_state], 1);
                rangeEncodeBit(&coder, &probs_is_rep      [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep0     [state]           , 0);
                rangeEncodeBit(&coder, &probs_is_rep0_long[state][pos_state], 1);
                break;
            case PKT_REP1     :
                rangeEncodeBit(&coder, &probs_is_match    [state][pos_state], 1);
                rangeEncodeBit(&coder, &probs_is_rep      [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep0     [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep1     [state]           , 0);
                break;
            case PKT_REP2     :
                rangeEncodeBit(&coder, &probs_is_match    [state][pos_state], 1);
                rangeEncodeBit(&coder, &probs_is_rep      [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep0     [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep1     [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep2     [state]           , 0);
                break;
            default :  // PKT_REP3
                rangeEncodeBit(&coder, &probs_is_match    [state][pos_state], 1);
                rangeEncodeBit(&coder, &probs_is_rep      [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep0     [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep1     [state]           , 1);
                rangeEncodeBit(&coder, &probs_is_rep2     [state]           , 1);
                break;
        }
        
        if (type == PKT_LIT) {
            if (state < N_LIT_STATES)
                rangeEncodeInt(&coder, probs_literal[lit_pos_state][prev_byte_lc_msbs], curr_byte, 8);
            else
                rangeEncodeMB (&coder, probs_literal[lit_pos_state][prev_byte_lc_msbs], curr_byte, match_byte);
        }
        
        if (type == PKT_MATCH || type == PKT_REP0 || type == PKT_REP1 || type == PKT_REP2 || type == PKT_REP3) {
            const uint8_t isrep = (type != PKT_MATCH);
            if        (len < 10) {                                                          // len = 2~9
                rangeEncodeBit(&coder, &probs_len_choice [isrep], 0);
                rangeEncodeInt(&coder,  probs_len_low    [isrep][pos_state], len-2 , 3);
            } else if (len < 18) {                                                          // len = 10~17
                rangeEncodeBit(&coder, &probs_len_choice [isrep], 1);
                rangeEncodeBit(&coder, &probs_len_choice2[isrep], 0);
                rangeEncodeInt(&coder,  probs_len_mid    [isrep][pos_state], len-10, 3);
            } else {                                                                        // len = 18~273
                rangeEncodeBit(&coder, &probs_len_choice [isrep], 1);
                rangeEncodeBit(&coder, &probs_len_choice2[isrep], 1);
                rangeEncodeInt(&coder,  probs_len_high   [isrep],            len-18, 8);
            }
        }
        
        if (type == PKT_MATCH) {
            const uint32_t len_min5_minus2 = (len>5) ? 3 : (len-2);
            uint32_t dist_slot, bcnt, bits;
            
            dist --;
            
            if (dist < 4) {
                dist_slot = dist;
            } else {
                dist_slot = countBit(dist) - 1;
                dist_slot = (dist_slot<<1) | ((dist>>(dist_slot-1)) & 1);
            }
            
            rangeEncodeInt(&coder, probs_dist_slot[len_min5_minus2], dist_slot, 6);
            
            bcnt = (dist_slot >> 1) - 1;
            
            if (dist_slot >= 14) {                                                          // dist slot = 14~63
                bcnt-= 4;
                bits = (dist>>4) & ((1<<bcnt)-1);
                rangeEncodeIntByFixedProb(&coder, bits, bcnt);
                
                bits = dist & ((1<<4)-1);
                bits = bitsReverse(bits, 4);
                rangeEncodeInt(&coder, probs_dist_align, bits, 4);
            } else if (dist_slot >= 4) {                                                    // dist slot = 4~13
                bits = dist & ((1<<bcnt)-1);
                bits = bitsReverse(bits, bcnt);
                rangeEncodeInt(&coder, probs_dist_special[dist_slot-4], bits, bcnt);
            }
        }
        
        state = stateTransition(state, type);
    }
    
    free(hash_table);
    
    rangeEncodeTerminate(&coder);
    
    if (coder.overflow)
        return R_ERR_OUTPUT_OVERFLOW;
    
    *p_dst_len = coder.p_dst - p_dst;
    
    return R_OK;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LZMA compress function, output data is packed in ".lzma" format
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define   LZMA_DIC_MIN                             4096
#define   LZMA_DIC_LEN                             ((LZ_DIST_MAX_PLUS1>LZMA_DIC_MIN) ? LZ_DIST_MAX_PLUS1 : LZMA_DIC_MIN)

#define   LZMA_HEADER_LEN                          13

static int writeLzmaHeader (uint8_t *p_dst, size_t *p_dst_len, size_t uncompressed_len, uint8_t uncompressed_len_known) {
    uint32_t i;
    
    if (*p_dst_len < LZMA_HEADER_LEN)
        return R_ERR_OUTPUT_OVERFLOW;
    
    *p_dst_len = LZMA_HEADER_LEN;
    
    *(p_dst++) = LCLPPB_BYTE;
    
    for (i=0; i<4; i++)
        *(p_dst++) = (uint8_t)(LZMA_DIC_LEN >> (i*8));
    
    for (i=0; i<8; i++) {
        if (uncompressed_len_known) {
            *(p_dst++) = (uint8_t)uncompressed_len;
            uncompressed_len >>= 8;
        } else {
            *(p_dst++) = 0xFF;
        }
    }
    
    return R_OK;
}


int tinyLzmaCompress (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len) {
    size_t hdr_len, cmprs_len;
    
    hdr_len = *p_dst_len;                                                      // set available space for header length
    
    RET_IF_ERROR( writeLzmaHeader(p_dst, &hdr_len, src_len, 1) );              // 
    
    cmprs_len = *p_dst_len - hdr_len;                                          // set available space for compressed data length
    
    RET_IF_ERROR( lzmaEncode(p_src, src_len, p_dst+hdr_len, &cmprs_len, 1) );  // do compression
    
    *p_dst_len = hdr_len + cmprs_len;                                          // the final output data length = LZMA file header len + compressed data len
    
    return R_OK;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LZMA compress function, output data is packed in ".zip" format, with a single file in this zip package
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static size_t getStringLength (const char *string) {
    size_t i;
    for (i=0; *string; string++, i++);
    return i;
}


#define   ZIP_LZMA_PROPERTY_LEN             9
#define   ZIP_HEADER_LEN_EXCLUDE_FILENAME   30
#define   ZIP_FOOTER_LEN_EXCLUDE_FILENAME   (46 + 22)

#define   FILE_NAME_IN_ZIP_MAX_LEN          ((size_t)0xFF00)
#define   ZIP_UNCOMPRESSED_MAX_LEN          ((size_t)0xFFFF0000)
#define   ZIP_COMPRESSED_MAX_LEN            ((size_t)0xFFFF0000)


static int writeZipHeader (uint8_t *p_dst, size_t *p_dst_len, uint32_t crc, size_t compressed_len, size_t uncompressed_len, const char *file_name) {
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
    *(p_dst++) = 0x0E;                               // 8~9 Compression method (0x0E = LZMA)
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


static int writeZipLzmaProperty (uint8_t *p_dst, size_t *p_dst_len) {
    if (*p_dst_len < ZIP_LZMA_PROPERTY_LEN)                 // no enough space for writing ZIP's LZMA property
        return R_ERR_OUTPUT_OVERFLOW;
    
    *p_dst_len = ZIP_LZMA_PROPERTY_LEN;
    
    *(p_dst++) = 0x10;
    *(p_dst++) = 0x02;
    *(p_dst++) = 0x05;
    *(p_dst++) = 0x00;
    *(p_dst++) = LCLPPB_BYTE;
    *(p_dst++) = (uint8_t)(LZMA_DIC_LEN >> 0);
    *(p_dst++) = (uint8_t)(LZMA_DIC_LEN >> 8);
    *(p_dst++) = (uint8_t)(LZMA_DIC_LEN >>16);
    *(p_dst++) = (uint8_t)(LZMA_DIC_LEN >>24);
    
    return R_OK;
}


static int writeZipFooter (uint8_t *p_dst, size_t *p_dst_len, uint32_t crc, size_t compressed_len, size_t uncompressed_len, const char *file_name, size_t offset) {
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
    *(p_dst++) = 0x0E;                               // 10~11 Compression method : 14 = LZMA
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


int tinyLzmaCompressToZipContainer (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, const char *file_name_in_zip) {
    size_t zip_hdr_len, lzma_prop_len, cmprs_len, zip_ftr_len;    // there are 4 parts of the final output data : ZIP header, ZIP LZMA property, LZMA compressed data, and ZIP footer
    uint32_t crc;
    
    zip_hdr_len = *p_dst_len;                                                                                               // set available space for ZIP header
    
    RET_IF_ERROR( writeZipHeader(p_dst, &zip_hdr_len, 0, 0, src_len, file_name_in_zip) );                                   // note that some fields are unknown and filled using "0", we should rewrite it later
    
    lzma_prop_len = *p_dst_len - zip_hdr_len;                                                                               // set available space for ZIP LZMA property
    
    RET_IF_ERROR( writeZipLzmaProperty(p_dst+zip_hdr_len, &lzma_prop_len) );
    
    cmprs_len = *p_dst_len - zip_hdr_len - lzma_prop_len;                                                                   // set available space for LZMA compressed data
    
    RET_IF_ERROR( lzmaEncode(p_src, src_len, p_dst+zip_hdr_len+lzma_prop_len, &cmprs_len, 1) );
    
    if (cmprs_len > ZIP_COMPRESSED_MAX_LEN)
        return R_ERR_UNSUPPORTED;
    
    cmprs_len += lzma_prop_len;                                                                                             // ZIP's LZMA property is actually a part of compressed data
    
    crc = calcCrc32(p_src, src_len);
    
    zip_ftr_len = *p_dst_len - zip_hdr_len - cmprs_len;                                                                     // set available space for ZIP footer
    
    RET_IF_ERROR( writeZipFooter(p_dst+zip_hdr_len+cmprs_len, &zip_ftr_len, crc, cmprs_len, src_len, file_name_in_zip, zip_hdr_len+cmprs_len) );
    
    RET_IF_ERROR( writeZipHeader(p_dst,                       &zip_hdr_len, crc, cmprs_len, src_len, file_name_in_zip) );   // rewrite ZIP header, since some fields are not writed previously.
    
    *p_dst_len = zip_hdr_len + cmprs_len + zip_ftr_len;                                                                     // the total output length = ZIP header length + compressed data length (include ZIP LZMA property) + ZIP footer length
    
    return R_OK;
}

