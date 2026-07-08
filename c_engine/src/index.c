/*
 * ============================================================================
 * INDEX.C  —  FM-INDEX SHIM LAYER
 * ============================================================================
 * FILE: src/index.c
 *
 * This file is a COMPLETE REPLACEMENT of the old hash-table inverted index.
 * All public functions keep their original signatures so that main.c compiles
 * without modification.  Internally every operation is delegated to the
 * FM-Index (fm_index.c / include/fm_index.h).
 *
 * Key mapping:
 *   create_index()      → fm_create()  wrapped in InvertedIndex shell
 *   index_word()        → no-op  (indexing now happens at file granularity)
 *   search_sentence()   → fm_backward_search() + position resolution
 *   search_keyword()    → search_sentence() with a single token
 *   save_index()        → fm_save()
 *   load_index()        → fm_load() wrapped in InvertedIndex shell
 *   free_index()        → fm_free() + shell free
 *   FileRegistry fns    → plain struct manipulation, pipe-delimited print
 * ============================================================================
 */

#include "../include/index.h"
#include "../include/fm_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* ============================================================================
 * FILE REGISTRY (used by main.c's "files" command)
 * ========================================================================== */

FileRegistry *create_file_registry(void) {
    FileRegistry *reg = calloc(1, sizeof(FileRegistry));
    return reg;
}

void register_file(FileRegistry *reg, int file_id, const char *filepath,
                   long size, int words, int sentences, int pages) {
    if (!reg || reg->count >= MAX_FILES) return;
    FileMetadata *m = &reg->files[reg->count++];
    m->file_id       = file_id;
    m->size_bytes    = size;
    m->word_count    = words;
    m->sentence_count = sentences;
    m->page_count    = pages;
    strncpy(m->filename, filepath ? filepath : "(unknown)", MAX_FILENAME_LEN - 1);
    m->filename[MAX_FILENAME_LEN - 1] = '\0';
}

/*
 * print_file_registry:
 *   Format expected by api.py:
 *     "<id> | <filepath> | <size_bytes> | <words> | <sentences> | <pages>"
 *   NOTE: we output raw bytes (not KB) so Python can display accurate sizes
 *   for files smaller than 1 KB.
 */
void print_file_registry(FileRegistry *reg) {
    if (!reg) return;
    for (int i = 0; i < reg->count; i++) {
        const FileMetadata *m = &reg->files[i];
        printf("%d | %s | %ld | %d | %d | %d\n",
               m->file_id, m->filename,
               m->size_bytes,        /* raw bytes */
               m->word_count, m->sentence_count, m->page_count);
    }
}

/* ============================================================================
 * INDEX LIFECYCLE
 * ========================================================================== */

/*
 * create_index: allocates the thin InvertedIndex wrapper and creates the
 * underlying FMIndex.  Sets size=0 / buckets=NULL so that the trie-rebuild
 * loop in main.c (`for (i=0; i<idx->size; i++)`) is harmlessly skipped.
 */
InvertedIndex *create_index(int size) {
    (void)size;   /* legacy parameter — ignored in FM-Index backend */

    InvertedIndex *idx = calloc(1, sizeof(InvertedIndex));
    if (!idx) return NULL;

    idx->fm      = fm_create();
    idx->buckets = NULL;
    idx->size    = 0;

    if (!idx->fm) { free(idx); return NULL; }

    /* Register with global singleton so trie.c can reach it */
    fm_set_global(idx->fm);
    return idx;
}

/*
 * index_word: deliberately a no-op.
 *
 * In the FM-Index design, indexing happens at the granularity of entire files
 * (fm_index_file).  main.c's index_file() now calls fm_index_file() directly
 * instead of looping over words.  This stub is kept so that any remaining
 * call sites in main.c still compile cleanly.
 */
void index_word(InvertedIndex *idx, const char *word, int file_id,
                int sentence_id, int page_number, long sentence_offset,
                int sentence_len, int position) {
    /* Intentionally empty — FM-Index handles indexing at file level */
    (void)idx; (void)word; (void)file_id; (void)sentence_id;
    (void)page_number; (void)sentence_offset; (void)sentence_len; (void)position;
}

void free_index(InvertedIndex *idx) {
    if (!idx) return;
    if (idx->fm) {
        fm_free(idx->fm);
        fm_set_global(NULL);
    }
    free(idx);
}

/* ============================================================================
 * SEARCH — BACKWARD SEARCH WRAPPER
 * ========================================================================== */

/*
 * Helper: normalise a query string to lowercase, returning a heap-allocated
 * copy.  Caller must free().
 */
static char *lc_dup(const char *s) {
    if (!s) return NULL;
    char *out = strdup(s);
    if (!out) return NULL;
    for (int i = 0; out[i]; i++)
        out[i] = (char)tolower((unsigned char)out[i]);
    return out;
}

/*
 * Helper: look up the PositionRecord whose corpus range contains `pos`.
 * Exposed as a static here; fm_index.c has an equivalent internal version.
 */
static const PositionRecord *find_record(const FMIndex *fm, long pos) {
    const PositionTable *pt = &fm->positions;
    int lo = 0, hi = pt->count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const PositionRecord *r = &pt->records[mid];
        if (pos < r->corpus_start)
            hi = mid - 1;
        else if (pos >= r->corpus_start + (long)r->corpus_len)
            lo = mid + 1;
        else
            return r;
    }
    return NULL;
}

/*
 * Helper: allocate a fresh Occurrence from a PositionRecord.
 */
static Occurrence *make_occurrence(const PositionRecord *pr) {
    Occurrence *occ = calloc(1, sizeof(Occurrence));
    if (!occ) return NULL;
    occ->file_id         = pr->file_id;
    occ->sentence_id     = pr->sentence_id;
    occ->page_number     = pr->page_number;
    occ->sentence_offset = pr->file_offset;
    occ->sentence_len    = pr->sentence_len;
    occ->frequency       = 1;
    occ->positions       = NULL;
    occ->capacity        = 0;
    occ->next            = NULL;
    return occ;
}

/*
 * core_search: internal helper that performs backward search for `lc_query`
 * (already lowercased) and returns a deduped Occurrence list sorted by
 * descending frequency.
 *
 * Deduplication: multiple SA rows resolving to the same (file_id, sentence_id,
 * page_number) triplet are merged — frequency is incremented.
 */
static Occurrence *core_search(const FMIndex *fm, const char *lc_query) {
    if (!fm || !lc_query || fm->bwt_len == 0) return NULL;

    int lo, hi;
    if (!fm_backward_search(fm, (const unsigned char *)lc_query,
                            (int)strlen(lc_query), &lo, &hi))
        return NULL;

    Occurrence *head = NULL, *tail = NULL;

    for (int row = lo; row <= hi; row++) {
        long pos = fm_resolve_position(fm, row);
        if (pos < 0 || pos >= (long)fm->corpus_len) continue;

        const PositionRecord *pr = find_record(fm, pos);
        if (!pr) continue;

        /* Dedup: find existing occurrence for same (file, sentence, page) */
        Occurrence *existing = head;
        bool merged = false;
        while (existing) {
            if (existing->file_id     == pr->file_id   &&
                existing->sentence_id == pr->sentence_id &&
                existing->page_number == pr->page_number) {
                existing->frequency++;
                merged = true;
                break;
            }
            existing = existing->next;
        }

        if (!merged) {
            Occurrence *occ = make_occurrence(pr);
            if (!occ) continue;
            if (!head) { head = tail = occ; }
            else        { tail->next = occ; tail = occ; }
        }
    }

    /* --- BM25 Scoring --- */
    int df = 0;
    for (Occurrence *cur = head; cur; cur = cur->next) df++;
    
    int N = fm->positions.count; /* Total number of sentences in corpus */
    if (N == 0) N = 1;
    
    /* Calculate IDF (Inverse Document Frequency) */
    float idf = logf( (float)(N - df + 0.5f) / (float)(df + 0.5f) + 1.0f );
    
    const float k1 = 1.2f;
    const float b  = 0.75f;
    /* We approximate avg sentence length as 100 bytes */
    const float avgdl = 100.0f; 

    for (Occurrence *cur = head; cur; cur = cur->next) {
        float tf = (float)cur->frequency;
        float dl = (float)cur->sentence_len;
        
        float num = tf * (k1 + 1.0f);
        float den = tf + k1 * (1.0f - b + b * (dl / avgdl));
        
        cur->score = idf * (num / den);
        
        /* Format explanation */
        snprintf(cur->explanation, sizeof(cur->explanation),
                 "BM25 Score: %.2f (IDF: %.2f, TF-Weight: %.2f)", 
                 cur->score, idf, (num/den));
    }

    return head;
}

/*
 * search_sentence: FM backward search for the full query string.
 *
 * The caller (main.c interactive loop) MUST free the returned list.
 * The fixed main.c does this — see the Phase 3 changes.
 */
Occurrence *search_sentence(InvertedIndex *idx, const char *query) {
    if (!idx || !idx->fm || !query) return NULL;

    char *lq = lc_dup(query);
    if (!lq) return NULL;

    Occurrence *results = core_search(idx->fm, lq);
    free(lq);
    return results;
}

/*
 * search_keyword: single-token lookup.  Strips any whitespace from the keyword
 * and delegates to the same FM backward search.
 */
Occurrence *search_keyword(InvertedIndex *idx, const char *keyword) {
    if (!idx || !keyword) return NULL;

    /* Extract the first non-whitespace token */
    char token[256] = {0};
    int  tlen = 0;
    for (int i = 0; keyword[i] && tlen < 255; i++) {
        char c = (char)tolower((unsigned char)keyword[i]);
        if (c == ' ' || c == '\t' || c == '\n') break;
        token[tlen++] = c;
    }
    token[tlen] = '\0';
    if (tlen == 0) return NULL;

    return core_search(idx->fm, token);
}

/* ============================================================================
 * PERSISTENCE
 * ========================================================================== */

void save_index(InvertedIndex *idx, const char *filename) {
    if (!idx || !idx->fm) return;
    fm_save(idx->fm, filename);
}

InvertedIndex *load_index(const char *filename) {
    FMIndex *fm = fm_load(filename);
    if (!fm) return NULL;

    InvertedIndex *idx = calloc(1, sizeof(InvertedIndex));
    if (!idx) { fm_free(fm); return NULL; }

    idx->fm      = fm;
    idx->buckets = NULL;
    idx->size    = 0;

    fm_set_global(fm);
    return idx;
}
