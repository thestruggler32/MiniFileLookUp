/*
 * ============================================================================
 * FM-INDEX CORE IMPLEMENTATION
 * ============================================================================
 * FILE: src/fm_index.c
 *
 * Implements the complete FM-Index pipeline:
 *   - Wavelet Matrix (O(log sigma) Rank)
 *   - Suffix Array  (qsort-based, O(n log^2 n))
 *   - BWT, C-array, Sampled SA
 *   - Backward Search (O(p) per query)
 *   - Position resolution via LF-mapping + sampled SA
 *   - Binary persistence (magic "FMIX", version 2)
 *   - Autocomplete via prefix backward search
 * ============================================================================
 */

#include "../include/fm_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ============================================================================
 * SECTION 1 — BITVECTOR HELPERS
 * ========================================================================== */

/*
 * bv_rank1(lv, i):
 *   Returns count of 1-bits in bitvector positions [0, i)  (exclusive).
 *   rank_samples[k] = popcount([0, k * FM_OCC_BLOCK))  — filled at build time.
 */
static int bv_rank1(const WaveletLevel *lv, int i) {
    if (i <= 0) return 0;

    /* last_bit: highest bit index we count (0-indexed, inclusive) */
    int last_bit   = i - 1;
    int block      = last_bit / FM_OCC_BLOCK;           /* sample block index */
    int base       = (int)lv->rank_samples[block];      /* cumulative before block */

    /* Count bits inside the block, from block_start to last_bit */
    int bit_start  = block * FM_OCC_BLOCK;
    int word_start = bit_start / 64;
    int word_end   = last_bit  / 64;

    /* Full 64-bit words fully inside the range */
    for (int w = word_start; w < word_end; w++) {
        base += __builtin_popcountll(lv->bits[w]);
    }

    /* Partial last word: bits [0 .. last_bit % 64] */
    int bits_in_last = (last_bit % 64) + 1;
    uint64_t mask = (bits_in_last == 64) ? UINT64_MAX
                                         : (((uint64_t)1 << bits_in_last) - 1);
    base += __builtin_popcountll(lv->bits[word_end] & mask);

    return base;
}

/* ============================================================================
 * SECTION 2 — WAVELET MATRIX BUILD & RANK
 * ========================================================================== */

static void wm_free_levels(WaveletMatrix *wm) {
    for (int l = 0; l < FM_LOG_SIGMA; l++) {
        free(wm->levels[l].bits);
        free(wm->levels[l].rank_samples);
        wm->levels[l].bits         = NULL;
        wm->levels[l].rank_samples = NULL;
    }
    wm->n = 0;
}

/*
 * wm_build: constructs FM_LOG_SIGMA bitvectors from the byte sequence `seq`.
 *
 * At each level l (MSB first):
 *   1. Record bit  (seq[i] >> (7-l)) & 1  into the bitvector.
 *   2. Stably partition seq: 0-elements first, then 1-elements.
 *
 * Returns 0 on success, -1 on allocation failure.
 */
static int wm_build(WaveletMatrix *wm, const unsigned char *seq, int n) {
    wm->n = n;
    memset(wm->levels, 0, sizeof(wm->levels));

    if (n <= 0) return 0;

    unsigned char *cur = malloc(n);
    unsigned char *nxt = malloc(n);
    if (!cur || !nxt) { free(cur); free(nxt); return -1; }
    memcpy(cur, seq, n);

    for (int l = 0; l < FM_LOG_SIGMA; l++) {
        WaveletLevel *lv = &wm->levels[l];
        int shift = FM_LOG_SIGMA - 1 - l;   /* extract this bit (MSB = level 0) */

        /* --- Allocate bitvector --- */
        lv->n_words = (n + 63) / 64;
        lv->bits    = calloc(lv->n_words, sizeof(uint64_t));
        if (!lv->bits) { free(cur); free(nxt); wm_free_levels(wm); return -1; }

        /* --- Fill bitvector --- */
        int ones = 0;
        for (int i = 0; i < n; i++) {
            if ((cur[i] >> shift) & 1) {
                lv->bits[i / 64] |= (uint64_t)1 << (i % 64);
                ones++;
            }
        }
        lv->nz = n - ones;

        /* --- Build rank_samples ---
         *  rank_samples[k] = popcount of bits in [0, k * FM_OCC_BLOCK)
         *  We iterate block by block, setting the sample BEFORE counting. */
        int n_blocks   = (n + FM_OCC_BLOCK - 1) / FM_OCC_BLOCK;
        lv->n_samples  = n_blocks;
        lv->rank_samples = calloc((size_t)n_blocks + 1, sizeof(uint32_t));
        if (!lv->rank_samples) {
            free(cur); free(nxt); wm_free_levels(wm); return -1;
        }

        uint32_t running = 0;
        for (int k = 0; k < n_blocks; k++) {
            lv->rank_samples[k] = running;
            int bs = k * FM_OCC_BLOCK;
            int be = bs + FM_OCC_BLOCK;
            if (be > n) be = n;
            /* count bits [bs, be) */
            int ws = bs / 64, we = (be - 1) / 64;
            for (int w = ws; w < we; w++)
                running += (uint32_t)__builtin_popcountll(lv->bits[w]);
            int tail = (be - 1) % 64 + 1;
            uint64_t m = (tail == 64) ? UINT64_MAX : (((uint64_t)1 << tail) - 1);
            running += (uint32_t)__builtin_popcountll(lv->bits[we] & m);
        }
        lv->rank_samples[n_blocks] = running; /* sentinel / total */

        /* --- Stable partition for next level --- */
        int li = 0, ri = lv->nz;
        for (int i = 0; i < n; i++)
            if (!((cur[i] >> shift) & 1)) nxt[li++] = cur[i];
        for (int i = 0; i < n; i++)
            if  ((cur[i] >> shift) & 1)  nxt[ri++] = cur[i];
        memcpy(cur, nxt, n);
    }

    free(cur);
    free(nxt);
    return 0;
}

/* ============================================================================
 * SECTION 12 — TELEMETRY
 * ========================================================================== */

void fm_stat_memory(const FMIndex *fm, long *bwt_sz, long *wm_sz, long *ssa_sz, long *corpus_sz) {
    if (!fm) return;
    
    *bwt_sz = fm->bwt_len * sizeof(unsigned char);
    *corpus_sz = fm->corpus_len * sizeof(unsigned char);
    
    *wm_sz = 0;
    for (int i = 0; i < FM_LOG_SIGMA; i++) {
        *wm_sz += fm->wm.levels[i].n_words * sizeof(uint64_t);
        *wm_sz += fm->wm.levels[i].n_samples * sizeof(uint32_t);
    }
    
    *ssa_sz = fm->ssa.n_samples * sizeof(int);
}

/*
 * wm_rank(wm, c, i):
 *   Count of byte c in seq[0..i)  (exclusive upper bound).
 *
 *   Algorithm — two-pointer tracking (lo, i) through all levels:
 *     lo tracks the start of c's bucket; i tracks its end.
 *     The difference (i - lo) is the rank at the end of traversal.
 */
static int wm_rank(const WaveletMatrix *wm, int c, int i) {
    if (i <= 0 || wm->n == 0) return 0;
    if (i > wm->n) i = wm->n;

    int lo = 0;
    for (int l = 0; l < FM_LOG_SIGMA; l++) {
        const WaveletLevel *lv = &wm->levels[l];
        int bit   = (c >> (FM_LOG_SIGMA - 1 - l)) & 1;
        int r1_lo = bv_rank1(lv, lo);
        int r1_i  = bv_rank1(lv, i);
        if (bit == 0) {
            lo = lo - r1_lo;   /* count of 0s before lo  */
            i  = i  - r1_i;   /* count of 0s before i   */
        } else {
            lo = lv->nz + r1_lo;
            i  = lv->nz + r1_i;
        }
    }
    return i - lo;
}

/* ============================================================================
 * SECTION 3 — SUFFIX ARRAY
 * ========================================================================== */

/*
 * Thread-unsafe but safe for single-threaded use.
 * The text MUST end with '\x00' (FM_SENTINEL) and contain no other '\x00'.
 * strcmp on suffixes works correctly because the sentinel terminates all of
 * them and is the smallest character (value 0), giving correct SA order.
 */
static const unsigned char *g_sa_text = NULL;

static int sa_cmp(const void *a, const void *b) {
    int ia = *(const int *)a;
    int ib = *(const int *)b;
    return strcmp((const char *)g_sa_text + ia,
                  (const char *)g_sa_text + ib);
}

/* Fills sa[0..n-1] with sorted suffix start indices. Returns 0 on success. */
static int build_suffix_array(const unsigned char *text, int n, int *sa) {
    g_sa_text = text;
    for (int i = 0; i < n; i++) sa[i] = i;
    qsort(sa, (size_t)n, sizeof(int), sa_cmp);
    g_sa_text = NULL;
    return 0;
}

/* ============================================================================
 * SECTION 4 — BWT CONSTRUCTION
 * ========================================================================== */

/*
 * build_bwt: bwt[i] = text[(sa[i] - 1 + n) % n]
 * Sets *primary_idx to the row where sa[i] == 0.
 */
static void build_bwt(const unsigned char *text, const int *sa, int n,
                       unsigned char *bwt, int *primary_idx) {
    *primary_idx = -1;
    for (int i = 0; i < n; i++) {
        if (sa[i] == 0) {
            bwt[i]       = text[n - 1];  /* wrap: last char = '\x00' sentinel */
            *primary_idx = i;
        } else {
            bwt[i] = text[sa[i] - 1];
        }
    }
}

/* ============================================================================
 * SECTION 5 — C-ARRAY
 * ========================================================================== */

/* C[c] = total characters strictly less than c in the BWT */
static void build_c_array(const unsigned char *bwt, int n, int *C) {
    int freq[FM_ALPHABET_SIZE] = {0};
    for (int i = 0; i < n; i++) freq[(unsigned char)bwt[i]]++;
    C[0] = 0;
    for (int c = 1; c < FM_ALPHABET_SIZE; c++)
        C[c] = C[c - 1] + freq[c - 1];
}

/* ============================================================================
 * SECTION 6 — SAMPLED SUFFIX ARRAY
 * ========================================================================== */

static int build_sampled_sa(const int *sa, int n, SampledSA *ssa) {
    ssa->n         = n;
    ssa->n_samples = (n + FM_SAMPLING_RATE - 1) / FM_SAMPLING_RATE;
    ssa->samples   = malloc((size_t)ssa->n_samples * sizeof(int));
    if (!ssa->samples) return -1;
    for (int i = 0; i < n; i += FM_SAMPLING_RATE)
        ssa->samples[i / FM_SAMPLING_RATE] = sa[i];
    return 0;
}

static void free_sampled_sa(SampledSA *ssa) {
    free(ssa->samples);
    ssa->samples   = NULL;
    ssa->n_samples = 0;
    ssa->n         = 0;
}

/* ============================================================================
 * SECTION 7 — PUBLIC CORE OPERATIONS
 * ========================================================================== */

int fm_rank(const FMIndex *fm, int c, int i) {
    if (!fm || !fm->bwt || i <= 0) return 0;
    return wm_rank(&fm->wm, (unsigned char)c, i);
}

int fm_lf(const FMIndex *fm, int i) {
    unsigned char c = fm->bwt[i];
    /* LF(i) = C[c] + Occ_exclusive(c, i)  where Occ_exclusive(c,i)=rank(c,i) */
    return fm->C[(int)c] + fm_rank(fm, c, i);
}

/*
 * fm_backward_search:
 *   Finds the SA range [lo, hi] of all suffixes starting with `pattern`.
 *   Iterates right-to-left:
 *     lo' = C[c] + rank(c, lo)
 *     hi' = C[c] + rank(c, hi+1) - 1
 *   Returns true iff lo <= hi (at least one match).
 */
bool fm_backward_search(const FMIndex *fm, const unsigned char *pattern,
                        int plen, int *lo_out, int *hi_out) {
    *lo_out = 1; *hi_out = 0;
    if (!fm || !pattern || plen <= 0 || fm->bwt_len == 0) return false;

    int lo = 0, hi = fm->bwt_len - 1;

    for (int i = plen - 1; i >= 0 && lo <= hi; i--) {
        int c = (unsigned char)pattern[i];
        lo = fm->C[c] + fm_rank(fm, c, lo);
        hi = fm->C[c] + fm_rank(fm, c, hi + 1) - 1;
    }

    *lo_out = lo;
    *hi_out = hi;
    return (lo <= hi);
}

/*
 * fm_resolve_position:
 *   Returns the absolute corpus byte offset (0-indexed) for SA row `row`.
 *
 *   Property: SA[LF(i)] = SA[i] - 1  (mod n)
 *   So after k LF steps reaching sampled row s:
 *     SA[row] = (SA[s] + k)  mod  n
 */
long fm_resolve_position(const FMIndex *fm, int row) {
    if (!fm || !fm->ssa.samples || fm->bwt_len == 0) return -1;

    int steps = 0;
    int r     = row;

    while (r % FM_SAMPLING_RATE != 0) {
        unsigned char c = fm->bwt[r];
        r = fm->C[(int)c] + fm_rank(fm, c, r);
        if (++steps > fm->bwt_len) return -1;   /* safety guard */
    }

    long sa_val = (long)fm->ssa.samples[r / FM_SAMPLING_RATE];
    return (sa_val + (long)steps) % (long)fm->bwt_len;
}

/* ============================================================================
 * SECTION 8 — POSITION METADATA LOOKUP (binary search)
 * ========================================================================== */

/* (Position lookup is implemented inline in index.c's find_record) */


/* ============================================================================
 * SECTION 9 — FULL INDEX REBUILD (called after each new file is appended)
 * ========================================================================== */

static int fm_rebuild(FMIndex *fm) {
    if (!fm->corpus || fm->corpus_len == 0) return 0;

    int n = fm->corpus_len + 1;  /* include '\x00' sentinel */

    /* --- Suffix Array --- */
    int *sa = malloc((size_t)n * sizeof(int));
    if (!sa) return -1;
    if (build_suffix_array(fm->corpus, n, sa) != 0) { free(sa); return -1; }

    /* --- BWT --- */
    free(fm->bwt);
    fm->bwt = malloc((size_t)n);
    if (!fm->bwt) { free(sa); return -1; }
    fm->bwt_len = n;
    build_bwt(fm->corpus, sa, n, fm->bwt, &fm->primary_idx);

    /* --- C-array --- */
    build_c_array(fm->bwt, n, fm->C);

    /* --- Wavelet Matrix --- */
    wm_free_levels(&fm->wm);
    if (wm_build(&fm->wm, fm->bwt, n) != 0) { free(sa); return -1; }

    /* --- Sampled SA --- */
    free_sampled_sa(&fm->ssa);
    if (build_sampled_sa(sa, n, &fm->ssa) != 0) { free(sa); return -1; }

    free(sa);   /* full SA no longer needed after sampling */
    return 0;
}

/* ============================================================================
 * SECTION 10 — FILE INDEXING
 * ========================================================================== */

/*
 * ensure_pt_capacity: grow position table if needed.
 */
static int ensure_pt_capacity(PositionTable *pt) {
    if (pt->count < pt->capacity) return 0;
    int nc = pt->capacity ? pt->capacity * 2 : 64;
    PositionRecord *nr = realloc(pt->records, (size_t)nc * sizeof(PositionRecord));
    if (!nr) return -1;
    pt->records  = nr;
    pt->capacity = nc;
    return 0;
}

/*
 * append_to_corpus: grow the corpus buffer and append lowercased bytes.
 * Returns 0 on success, -1 on OOM.
 */
/* corpus extension is handled inline in fm_index_file */


/*
 * fm_index_file:
 *   1. Read `filepath` into memory.
 *   2. Parse it line-by-line, handling [PAGE:N] markers.
 *   3. Build lowercased corpus and position records.
 *   4. Rebuild the FM-Index from scratch.
 */
int fm_index_file(FMIndex *fm, int file_id, const char *filepath) {
    if (!fm || !filepath || filepath[0] == '\0') return -1;

    /* --- Read file --- */
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "FM-Index: cannot open '%s': %s\n", filepath, strerror(errno));
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long file_len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_len <= 0) { fclose(f); return 0; }

    char *raw = malloc((size_t)file_len + 1);
    if (!raw) { fclose(f); return -1; }
    size_t rd = fread(raw, 1, (size_t)file_len, f);
    fclose(f);
    raw[rd] = '\0';

    /* Strip null bytes and FM_FILE_SEP to keep corpus clean */
    int clean_len = 0;
    for (size_t i = 0; i < rd; i++) {
        unsigned char ch = (unsigned char)raw[i];
        if (ch != '\x00' && ch != (unsigned char)FM_FILE_SEP)
            raw[clean_len++] = (char)ch;
    }
    raw[clean_len] = '\0';

    /* --- Parse lines, record positions, build corpus --- */
    long corpus_base = fm->corpus_len;
    int  sentence_id = 1;
    int  current_page = 1;
    int  word_count   = 0;
    int  total_words  = 0;
    int  total_sentences = 0;

    const char *p         = raw;
    const char *raw_end   = raw + clean_len;
    const char *sent_start = p;

    /* We'll build a separate lowercased text block for the corpus,
     * tracking where each sentence maps in both the corpus and the raw file. */

    /* Temporary lowercased buffer (same size as raw, 1:1 positions for ASCII) */
    char *lc_buf = malloc((size_t)clean_len + 2);
    if (!lc_buf) { free(raw); return -1; }
    for (int i = 0; i <= clean_len; i++)
        lc_buf[i] = (char)tolower((unsigned char)raw[i]);

    /* Corpus offset at start of this file */
    long corp_write = (long)fm->corpus_len;

    /* Extend corpus buffer (upper bound: clean_len + file_sep + sentinel) */
    {
        unsigned char *nc = realloc(fm->corpus,
                                    (size_t)(fm->corpus_len + clean_len + 2));
        if (!nc) { free(raw); free(lc_buf); return -1; }
        fm->corpus = nc;
    }

    const char *lc_sent_start = lc_buf + (sent_start - raw);

    while (p <= raw_end) {
        bool at_end = (p == raw_end);

        /* Handle [PAGE:N] markers inline */
        if (!at_end && *p == '[' &&
                (raw_end - p) >= 8 &&
                strncmp(p, "[PAGE:", 6) == 0) {
            const char *rb = (const char *)memchr(p + 6, ']',
                                                  (size_t)(raw_end - p - 6 > 15
                                                           ? 15 : raw_end - p - 6));
            if (rb) {
                current_page = atoi(p + 6);
                /* Skip the marker — advance both pointers */
                int skip = (int)(rb + 1 - p);
                p          += skip;
                sent_start  = p;
                lc_sent_start = lc_buf + (sent_start - raw);
                continue;
            }
        }

        bool is_newline = (!at_end && *p == '\n');

        if (is_newline || at_end) {
            int line_len = (int)(p - sent_start);

            if (line_len > 0) {
                /* Count words */
                bool in_word = false;
                word_count = 0;
                for (int i = 0; i < line_len; i++) {
                    if (isalnum((unsigned char)sent_start[i])) {
                        if (!in_word) { word_count++; in_word = true; }
                    } else {
                        in_word = false;
                    }
                }
                total_words += word_count;

                /* Copy lowercased sentence to corpus */
                long sent_corp_start = corp_write;
                memcpy(fm->corpus + corp_write, lc_sent_start, (size_t)line_len);
                corp_write += line_len;
                /* Add newline separator in corpus */
                fm->corpus[corp_write++] = '\n';

                /* Add position record */
                if (ensure_pt_capacity(&fm->positions) != 0)
                    break;

                PositionRecord *rec = &fm->positions.records[fm->positions.count++];
                rec->corpus_start = sent_corp_start;
                rec->corpus_len   = line_len + 1;   /* include the '\n' */
                rec->file_id      = file_id;
                rec->sentence_id  = sentence_id++;
                rec->page_number  = current_page;
                rec->file_offset  = (long)(sent_start - raw);
                rec->sentence_len = line_len;

                total_sentences++;
            }

            sent_start    = p + 1;
            lc_sent_start = lc_buf + (sent_start - raw);
        }

        p++;
    }

    /* Append file separator and update corpus_len */
    fm->corpus[corp_write] = (unsigned char)FM_FILE_SEP;
    corp_write++;
    fm->corpus[corp_write] = (unsigned char)FM_SENTINEL;  /* always keep sentinel */
    fm->corpus_len = (int)corp_write;

    free(lc_buf);

    /* --- Update file registry --- */
    if (fm->registry.count < FM_MAX_FILES) {
        FMFileRecord *fr = &fm->registry.files[fm->registry.count++];
        fr->file_id       = file_id;
        fr->size_bytes    = (long)clean_len;
        fr->word_count    = total_words;
        fr->sentence_count = total_sentences;
        fr->page_count    = current_page;
        fr->corpus_start  = corpus_base;
        fr->corpus_end    = (long)fm->corpus_len;
        strncpy(fr->filename, filepath, FM_MAX_FILENAME - 1);
        fr->filename[FM_MAX_FILENAME - 1] = '\0';
    }

    free(raw);

    /* --- Rebuild all FM-Index structures --- */
    printf("Indexing File ID %d: %s (%d bytes)\n", file_id, filepath, clean_len);
    fflush(stdout);

    if (fm_rebuild(fm) != 0) {
        fprintf(stderr, "FM-Index: rebuild failed for '%s'\n", filepath);
        return -1;
    }

    printf("FM-Index OK: corpus=%d chars, BWT=%d rows\n",
           fm->corpus_len, fm->bwt_len);
    fflush(stdout);

    return 0;
}

/* ============================================================================
 * SECTION 11 — AUTOCOMPLETE
 * ========================================================================== */

/*
 * extract_word_at: copies the word beginning at corpus offset `pos`
 * into `out` (null-terminated).  A word ends at any whitespace, file
 * separator, or sentinel.  Returns the number of chars copied.
 */
static int extract_word_at(const FMIndex *fm, long pos, char *out, int max) {
    int len = 0;
    while (pos + len < (long)fm->corpus_len && len < max - 1) {
        unsigned char ch = fm->corpus[pos + len];
        if (!isalnum((unsigned char)ch))
            break;
        out[len++] = (char)tolower((int)ch);
    }
    out[len] = '\0';
    return len;
}

/*
 * is_word_boundary: returns true if corpus[pos-1] is a separator/start.
 */
static bool is_word_boundary(const FMIndex *fm, long pos) {
    if (pos <= 0) return true;  /* start of corpus is always a word boundary */
    unsigned char prev = fm->corpus[pos - 1];
    return !isalnum(prev);
}

int fm_prefix_search(const FMIndex *fm, const char *prefix,
                     char **out_words, int max_words) {
    if (!fm || !prefix || !out_words || max_words <= 0) return 0;
    int plen = (int)strlen(prefix);
    if (plen == 0 || fm->bwt_len == 0) return 0;

    /* Lowercase the prefix before searching */
    char lc_prefix[256];
    int pl = plen < 255 ? plen : 255;
    for (int i = 0; i < pl; i++)
        lc_prefix[i] = (char)tolower((unsigned char)prefix[i]);
    lc_prefix[pl] = '\0';

    int lo, hi;
    if (!fm_backward_search(fm, (const unsigned char *)lc_prefix, pl, &lo, &hi))
        return 0;

    int count = 0;
    char wbuf[512];

    for (int row = lo; row <= hi && count < max_words; row++) {
        long pos = fm_resolve_position(fm, row);
        if (pos < 0 || pos >= (long)fm->corpus_len) continue;
        if (!is_word_boundary(fm, pos)) continue;

        int wlen = extract_word_at(fm, pos, wbuf, (int)sizeof(wbuf));
        if (wlen < pl) continue;

        /* Verify prefix match (already lowercased in corpus) */
        if (strncmp(wbuf, lc_prefix, (size_t)pl) != 0) continue;

        /* Deduplicate */
        bool dup = false;
        for (int i = 0; i < count; i++) {
            if (strcmp(out_words[i], wbuf) == 0) { dup = true; break; }
        }
        if (dup) continue;

        out_words[count] = strdup(wbuf);
        if (!out_words[count]) break;
        count++;
    }

    return count;
}

/* ============================================================================
 * SECTION 12 — LIFECYCLE
 * ========================================================================== */

FMIndex *fm_create(void) {
    FMIndex *fm = calloc(1, sizeof(FMIndex));
    if (!fm) return NULL;
    fm->positions.capacity = 128;
    fm->positions.records  = malloc(128 * sizeof(PositionRecord));
    if (!fm->positions.records) { free(fm); return NULL; }
    fm->positions.count    = 0;
    return fm;
}

void fm_free(FMIndex *fm) {
    if (!fm) return;
    free(fm->bwt);
    free(fm->corpus);
    free(fm->positions.records);
    free_sampled_sa(&fm->ssa);
    wm_free_levels(&fm->wm);
    free(fm);
}

/* ============================================================================
 * SECTION 13 — PERSISTENCE
 * ========================================================================== */

#define FM_MAGIC   0x58494D46u  /* "FMIX" little-endian */
#define FM_VERSION 2

int fm_save(const FMIndex *fm, const char *filename) {
    if (!fm || !filename) return -1;

    FILE *f = fopen(filename, "wb");
    if (!f) { perror("fm_save"); return -1; }

#define WI(val) { int _v = (int)(val); fwrite(&_v, sizeof(int), 1, f); }
#define WU(val) { unsigned int _v = (unsigned int)(val); fwrite(&_v, sizeof(unsigned int), 1, f); }

    /* Header */
    WU(FM_MAGIC); WI(FM_VERSION);
    WI(fm->corpus_len); WI(fm->bwt_len); WI(fm->primary_idx);

    /* Corpus (include trailing sentinel) */
    if (fm->corpus && fm->corpus_len > 0)
        fwrite(fm->corpus, 1, (size_t)fm->corpus_len + 1, f);

    /* BWT */
    if (fm->bwt && fm->bwt_len > 0)
        fwrite(fm->bwt, 1, (size_t)fm->bwt_len, f);

    /* C-array */
    fwrite(fm->C, sizeof(int), FM_ALPHABET_SIZE, f);

    /* Sampled SA */
    WI(fm->ssa.n_samples);
    if (fm->ssa.n_samples > 0)
        fwrite(fm->ssa.samples, sizeof(int), (size_t)fm->ssa.n_samples, f);

    /* Wavelet matrix */
    for (int l = 0; l < FM_LOG_SIGMA; l++) {
        const WaveletLevel *lv = &fm->wm.levels[l];
        WI(lv->n_words); WI(lv->n_samples); WI(lv->nz);
        if (lv->n_words > 0)
            fwrite(lv->bits, sizeof(uint64_t), (size_t)lv->n_words, f);
        if (lv->n_samples > 0)
            fwrite(lv->rank_samples, sizeof(uint32_t),
                   (size_t)lv->n_samples + 1, f);
    }

    /* Position table */
    WI(fm->positions.count);
    if (fm->positions.count > 0)
        fwrite(fm->positions.records, sizeof(PositionRecord),
               (size_t)fm->positions.count, f);

    /* File registry */
    WI(fm->registry.count);
    if (fm->registry.count > 0)
        fwrite(fm->registry.files, sizeof(FMFileRecord),
               (size_t)fm->registry.count, f);

#undef WI
#undef WU

    fclose(f);
    printf("Index saved to '%s'\n", filename);
    return 0;
}

FMIndex *fm_load(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    unsigned int magic; int version;
    if (fread(&magic,   sizeof(unsigned int), 1, f) != 1 || magic   != FM_MAGIC)   goto fail;
    if (fread(&version, sizeof(int),          1, f) != 1 || version != FM_VERSION)  goto fail;

    FMIndex *fm = fm_create();
    if (!fm) goto fail;

    int corpus_len, bwt_len, primary_idx;
    if (fread(&corpus_len,   sizeof(int), 1, f) != 1) goto fail2;
    if (fread(&bwt_len,      sizeof(int), 1, f) != 1) goto fail2;
    if (fread(&primary_idx,  sizeof(int), 1, f) != 1) goto fail2;

    fm->corpus_len  = corpus_len;
    fm->bwt_len     = bwt_len;
    fm->primary_idx = primary_idx;

    /* Corpus */
    if (corpus_len > 0) {
        fm->corpus = malloc((size_t)corpus_len + 1);
        if (!fm->corpus) goto fail2;
        if (fread(fm->corpus, 1, (size_t)corpus_len + 1, f) != (size_t)corpus_len + 1)
            goto fail2;
    }

    /* BWT */
    if (bwt_len > 0) {
        fm->bwt = malloc((size_t)bwt_len);
        if (!fm->bwt) goto fail2;
        if (fread(fm->bwt, 1, (size_t)bwt_len, f) != (size_t)bwt_len) goto fail2;
    }

    /* C-array */
    fread(fm->C, sizeof(int), FM_ALPHABET_SIZE, f);

    /* Sampled SA */
    {
        int ns;
        fread(&ns, sizeof(int), 1, f);
        fm->ssa.n_samples = ns;
        fm->ssa.n         = bwt_len;
        if (ns > 0) {
            fm->ssa.samples = malloc((size_t)ns * sizeof(int));
            if (!fm->ssa.samples) goto fail2;
            fread(fm->ssa.samples, sizeof(int), (size_t)ns, f);
        }
    }

    /* Wavelet matrix */
    fm->wm.n = bwt_len;
    for (int l = 0; l < FM_LOG_SIGMA; l++) {
        WaveletLevel *lv = &fm->wm.levels[l];
        int nw, ns, nz;
        fread(&nw, sizeof(int), 1, f);
        fread(&ns, sizeof(int), 1, f);
        fread(&nz, sizeof(int), 1, f);
        lv->n_words = nw; lv->n_samples = ns; lv->nz = nz;
        if (nw > 0) {
            lv->bits = malloc((size_t)nw * sizeof(uint64_t));
            if (!lv->bits) goto fail2;
            fread(lv->bits, sizeof(uint64_t), (size_t)nw, f);
        }
        if (ns > 0) {
            lv->rank_samples = malloc(((size_t)ns + 1) * sizeof(uint32_t));
            if (!lv->rank_samples) goto fail2;
            fread(lv->rank_samples, sizeof(uint32_t), (size_t)ns + 1, f);
        }
    }

    /* Position table */
    {
        int pc;
        fread(&pc, sizeof(int), 1, f);
        if (pc > 0) {
            free(fm->positions.records);
            fm->positions.records = malloc((size_t)pc * sizeof(PositionRecord));
            if (!fm->positions.records) goto fail2;
            fread(fm->positions.records, sizeof(PositionRecord), (size_t)pc, f);
            fm->positions.count    = pc;
            fm->positions.capacity = pc;
        }
    }

    /* File registry */
    {
        int rc;
        fread(&rc, sizeof(int), 1, f);
        if (rc > 0 && rc <= FM_MAX_FILES) {
            fread(fm->registry.files, sizeof(FMFileRecord), (size_t)rc, f);
            fm->registry.count = rc;
        }
    }

    fclose(f);
    printf("Index loaded from '%s' (%d BWT rows)\n", filename, bwt_len);
    return fm;

fail2: fm_free(fm);
fail:  fclose(f); return NULL;
}

/* ============================================================================
 * SECTION 14 — GLOBAL SINGLETON
 * ========================================================================== */

static FMIndex *g_fm = NULL;

void     fm_set_global(FMIndex *fm) { g_fm = fm; }
FMIndex *fm_get_global(void)        { return g_fm; }
