#ifndef INDEX_H
#define INDEX_H

// Linked list node representing a single location of a word
typedef struct Occurrence {
  int file_id;
  int sentence_id;
  int frequency;  // Count of this word in this specific sentence
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
                int sentence_id, int position);

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

#endif
