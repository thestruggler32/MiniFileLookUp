/*
 * ============================================================================
 * FM-INDEX HEADER
 * ============================================================================
 * FILE: include/fm_index.h
 *
 * Defines the complete FM-Index pipeline:
 *   1. Suffix Array (O(n log^2 n) qsort-based)
 *   2. Burrows-Wheeler Transform (BWT)
 *   3. Wavelet Matrix  -> O(log sigma) Rank(c, i) queries
 *   4. Sampled Suffix Array (rate K=32)  -> O(K) position resolution
 *   5. Backward Search -> O(p) query, where p = pattern length
 *
 * This header is the single dependency for index.c, trie.c, and main.c.
 * ============================================================================
 */

#ifndef FM_INDEX_H
#define FM_INDEX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- Tuning constants ---- */
#define FM_ALPHABET_SIZE  256   /* full byte alphabet                        */
#define FM_SAMPLING_RATE  32    /* sample SA every K rows; controls RAM/speed */
#define FM_LOG_SIGMA      8     /* log2(256): depth of the wavelet matrix     */
#define FM_OCC_BLOCK      512   /* popcount sample stride in bits             */
#define FM_FILE_SEP       '\x01'/* corpus separator between indexed files     */
#define FM_SENTINEL       '\x00'/* end-of-corpus sentinel (unique, smallest)  */
#define FM_MAX_FILES      256
#define FM_MAX_FILENAME   512
#define FM_MAX_SUGGEST    64    /* max autocomplete words returned            */

/* ============================================================================
 * WAVELET MATRIX
 *
 * A flat, cache-friendly replacement for the classical wavelet tree.
 * Supports Rank(c, i) = "how many times does byte c appear in bwt[0..i)?"
 * in O(log sigma) = O(8) steps, one per level.
 *
 * Level l stores a packed bitvector where bit[i] = (seq[i] >> (7-l)) & 1
 * after the sequence has been stably partitioned by levels 0..l-1.
 * ========================================================================== */

typedef struct {
    uint64_t *bits;          /* packed bitvector: ceil(n/64) words            */
    uint32_t *rank_samples;  /* rank_samples[k] = popcount(bits[0..k*BLOCK)) */
    int       n_words;       /* number of uint64_t words                      */
    int       n_samples;     /* number of rank_samples entries                */
    int       nz;            /* count of 0-bits at this level (left-child sz) */
} WaveletLevel;

typedef struct {
    WaveletLevel levels[FM_LOG_SIGMA];
    int          n;          /* sequence length                               */
} WaveletMatrix;

/* ============================================================================
 * SAMPLED SUFFIX ARRAY
 *
 * Stores SA[i] only when i % FM_SAMPLING_RATE == 0.
 * Saves (K-1)/K ≈ 97% of full-SA memory.
 * Position resolution uses LF-mapping (at most K steps per query).
 * ========================================================================== */

typedef struct {
    int *samples;    /* samples[i / K] = SA[i]  for i divisible by K         */
    int  n_samples;  /* = ceil(n / FM_SAMPLING_RATE)                          */
    int  n;          /* full SA length (= BWT length)                         */
} SampledSA;

/* ============================================================================
 * POSITION METADATA
 *
 * Each sentence/paragraph in every indexed file gets one record.
 * Records are kept sorted by corpus_start for O(log n) binary search.
 * ========================================================================== */

typedef struct {
    long corpus_start;  /* byte start of sentence in the lowercased corpus   */
    int  corpus_len;    /* byte length  in the corpus                        */
    int  file_id;
    int  sentence_id;
    int  page_number;
    long file_offset;   /* byte offset of sentence in the temp .txt file     */
    int  sentence_len;  /* byte length  in the temp .txt file                */
} PositionRecord;

typedef struct {
    PositionRecord *records;
    int             count;
    int             capacity;
} PositionTable;

/* ============================================================================
 * FILE REGISTRY (FM-Index side)
 * ========================================================================== */

typedef struct {
    int  file_id;
    char filename[FM_MAX_FILENAME];
    long size_bytes;
    int  word_count;
    int  sentence_count;
    int  page_count;
    long corpus_start;
    long corpus_end;
} FMFileRecord;

typedef struct {
    FMFileRecord files[FM_MAX_FILES];
    int          count;
} FMFileRegistry;

/* ============================================================================
 * FM-INDEX MASTER STRUCTURE
 * ========================================================================== */

typedef struct FMIndex {
    /* --- BWT --- */
    unsigned char *bwt;
    int            bwt_len;      /* corpus_len + 1 (includes sentinel row)   */
    int            primary_idx;  /* row i where SA[i] = 0                    */

    /* --- C-array: C[c] = count of chars < c in the entire BWT --- */
    int C[FM_ALPHABET_SIZE];

    /* --- Wavelet matrix for O(log sigma) rank --- */
    WaveletMatrix wm;

    /* --- Sampled SA for O(K) position resolution --- */
    SampledSA ssa;

    /* --- Lowercased, concatenated corpus of all indexed files --- */
    unsigned char *corpus;
    int            corpus_len;   /* NOT including trailing sentinel           */

    /* --- Sentence position metadata --- */
    PositionTable positions;

    /* --- File registry --- */
    FMFileRegistry registry;
} FMIndex;

/* ============================================================================
 * PUBLIC API
 * ========================================================================== */

/* Lifecycle */
FMIndex *fm_create(void);
void     fm_free(FMIndex *fm);

/* File indexing — appends file content to corpus and rebuilds all structures */
int fm_index_file(FMIndex *fm, int file_id, const char *filepath);

/* Core operations */
int  fm_rank(const FMIndex *fm, int c, int i);   /* count c in bwt[0..i)    */
int  fm_lf  (const FMIndex *fm, int i);          /* LF-mapping at row i     */
bool fm_backward_search(const FMIndex *fm,
                        const unsigned char *pattern, int plen,
                        int *lo_out, int *hi_out);
long fm_resolve_position(const FMIndex *fm, int row); /* corpus byte offset  */

/* Autocomplete: fills out_words (caller frees each entry). Returns count.   */
int fm_prefix_search(const FMIndex *fm, const char *prefix,
                     char **out_words, int max_words);

/* Persistence */
int      fm_save(const FMIndex *fm, const char *filename);
FMIndex *fm_load(const char *filename);

/* Global singleton — lets trie.c access the FM-Index without changing sigs  */
void     fm_set_global(FMIndex *fm);
FMIndex *fm_get_global(void);

#endif /* FM_INDEX_H */
