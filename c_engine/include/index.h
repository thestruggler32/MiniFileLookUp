#ifndef INDEX_H
#define INDEX_H

// Linked list node representing a single location of a word
typedef struct Occurrence {
  int file_id;
  int sentence_id;
  int page_number;
  long sentence_offset; // NEW: Byte offset of the sentence start
  int sentence_len;     // NEW: Length of the sentence in bytes
  int frequency;
  int *positions; // Array of positions (0-indexed)
  int capacity;   // Capacity of positions array
  struct Occurrence *next;
} Occurrence;

// Hash table entry (bucket node)
typedef struct IndexEntry {
  char *word;              // The keyword
  Occurrence *occurrences; // Linked list of occurrences
  struct IndexEntry *next; // Next entry in the same bucket (collision chain)
} IndexEntry;

// The main Inverted Index structure
typedef struct InvertedIndex {
  IndexEntry **buckets; // Array of pointers to IndexEntry
  int size;             // Number of buckets
} InvertedIndex;

// File Metadata Tracking
#define MAX_FILENAME_LEN 256
#define MAX_FILES 100

typedef struct FileMetadata {
  int file_id;
  char filename[MAX_FILENAME_LEN];
  long size_bytes;
  int word_count;
  int sentence_count;
  int page_count;
} FileMetadata;

typedef struct FileRegistry {
  FileMetadata files[MAX_FILES];
  int count;
} FileRegistry;

FileRegistry *create_file_registry();
void register_file(FileRegistry *reg, int file_id, const char *filepath,
                   long size, int words, int sentences, int pages);
void print_file_registry(FileRegistry *reg);

/**
 * Creates a new Inverted Index with a specified number of buckets.
 */
InvertedIndex *create_index(int size);

/**
 * Indexes a word occurrence.
 * If the word exists, it updates the frequency for the same sentence
 * and records the position.
 * or adds a new occurrence node if it's a new sentence/file.
 */
void index_word(InvertedIndex *idx, const char *word, int file_id,
                int sentence_id, int page_number, long sentence_offset,
                int sentence_len, int position);

/**
 * Searches for a whole sentence/phrase.
 * Returns a list of Occurrences (allocated freshly) representing the matches.
 * The caller must free this list (but currently we reuse Occurrence struct so
 * simple free is enough if we don't deep copy positions for results merely for
 * display). For simplicity, we'll return a newly allocated linked list of
 * matches.
 */
Occurrence *search_sentence(InvertedIndex *idx, const char *query);

/**
 * Searches for a keyword in the index.
 * Returns the head of the Occurrence list, or NULL if not found.
 */
Occurrence *search_keyword(InvertedIndex *idx, const char *keyword);

/**
 * Frees all memory associated with the index.
 */
void free_index(InvertedIndex *idx);

/**
 * Saves the index to a binary file.
 */
void save_index(InvertedIndex *idx, const char *filename);

/**
 * Loads the index from a binary file.
 * Returns NULL if file doesn't exist or is invalid.
 */
InvertedIndex *load_index(const char *filename);

#endif
