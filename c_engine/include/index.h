#ifndef INDEX_H
#define INDEX_H

/* Forward declaration — full definition is in fm_index.h */
struct FMIndex;

/* ---- Occurrence: one match location, returned by search functions ---- */
typedef struct Occurrence {
  int   file_id;
  int   sentence_id;
  int   page_number;
  long  sentence_offset; /* byte offset of sentence start in temp .txt file  */
  int   sentence_len;    /* byte length of sentence in temp .txt file         */
  int   frequency;       /* number of query hits in this sentence             */
  int  *positions;       /* per-word positions (NULL in FM-Index results)     */
  int   capacity;
  struct Occurrence *next;
} Occurrence;

/* ---- IndexEntry / InvertedIndex: kept for main.c ABI compatibility ----
 *
 * Under the FM-Index backend:
 *   idx->fm      = live FMIndex pointer (all real work done here)
 *   idx->buckets = NULL   (legacy hash table is gone)
 *   idx->size    = 0      (makes the trie-rebuild loop in main.c a no-op)
 */
typedef struct IndexEntry {
  char       *word;
  Occurrence *occurrences;
  struct IndexEntry *next;
} IndexEntry;

typedef struct InvertedIndex {
  struct FMIndex  *fm;      /* FM-Index backend — replaces hash table        */
  IndexEntry     **buckets; /* always NULL under FM-Index                    */
  int              size;    /* always 0   under FM-Index                    */
} InvertedIndex;

/* ---- File metadata registry (used by the interactive 'files' command) ---- */
#define MAX_FILENAME_LEN 256
#define MAX_FILES        100

typedef struct FileMetadata {
  int  file_id;
  char filename[MAX_FILENAME_LEN];
  long size_bytes;
  int  word_count;
  int  sentence_count;
  int  page_count;
} FileMetadata;

typedef struct FileRegistry {
  FileMetadata files[MAX_FILES];
  int          count;
} FileRegistry;

FileRegistry *create_file_registry(void);
void register_file(FileRegistry *reg, int file_id, const char *filepath,
                   long size, int words, int sentences, int pages);
void print_file_registry(FileRegistry *reg);

/* ---- Public index API (implementations in index.c delegate to FMIndex) ---- */
InvertedIndex *create_index(int size);

/* no-op under FM-Index: actual indexing happens in fm_index_file() */
void index_word(InvertedIndex *idx, const char *word, int file_id,
                int sentence_id, int page_number, long sentence_offset,
                int sentence_len, int position);

/* FM backward search → returns freshly allocated Occurrence list (caller frees) */
Occurrence *search_sentence(InvertedIndex *idx, const char *query);

/* Single-word backward search */
Occurrence *search_keyword(InvertedIndex *idx, const char *keyword);

/* Free the index and the underlying FMIndex */
void free_index(InvertedIndex *idx);

/* Persist / restore the FM-Index */
void           save_index(InvertedIndex *idx, const char *filename);
InvertedIndex *load_index(const char *filename);

#endif /* INDEX_H */
