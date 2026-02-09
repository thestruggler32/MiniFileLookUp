/*
 * ============================================================================
 * INVERTED INDEX IMPLEMENTATION
 * ============================================================================
 *
 * FILE: index.c
 * DESCRIPTION: Core inverted index data structure for fast keyword search
 *
 * WHAT IS AN INVERTED INDEX?
 * --------------------------
 * An inverted index is a data structure that maps words to their locations
 * in documents. It's called "inverted" because instead of mapping documents
 * to words (like a normal index), it maps words to documents.
 *
 * Example:
 * --------
 * Document 1: "hello world"
 * Document 2: "hello there"
 *
 * Inverted Index:
 * "hello" -> [Doc1, Doc2]
 * "world" -> [Doc1]
 * "there" -> [Doc2]
 *
 * IMPLEMENTATION DETAILS:
 * ----------------------
 * This implementation uses a HASH TABLE for O(1) average case lookup:
 *
 * 1. HASH FUNCTION (DJB2):
 *    - Converts word string to integer hash value
 *    - Formula: hash = hash * 33 + char
 *    - Fast and provides good distribution
 *
 * 2. HASH TABLE STRUCTURE:
 *    - Array of buckets (default size: 1024)
 *    - Each bucket is a linked list of IndexEntry nodes
 *    - Handles collisions via chaining
 *
 * 3. INDEX ENTRY:
 *    - word: The indexed keyword (normalized to lowercase)
 *    - occurrences: Linked list of Occurrence nodes
 *    - next: Pointer to next entry in bucket (for collision handling)
 *
 * 4. OCCURRENCE NODE:
 *    - file_id: Which document contains this word
 *    - sentence_id: Which sentence/paragraph in the document
 *    - page_number: Which page (for PDF/DOCX support)
 *    - position: Word position within sentence (for phrase search)
 *    - sentence_offset: Byte offset in file (for quick retrieval)
 *    - sentence_len: Length of sentence in bytes
 *    - frequency: How many times word appears in this sentence
 *    - next: Pointer to next occurrence
 *
 * KEY OPERATIONS:
 * --------------
 * - create_index(size): Initialize hash table with given size
 * - index_word(...): Add word occurrence to index
 * - search_keyword(word): Find all occurrences of a word (O(1) avg)
 * - search_sentence(phrase): Find multi-word phrases (O(n*m))
 * - free_index(): Clean up all allocated memory
 *
 * PERFORMANCE:
 * -----------
 * - Keyword search: O(1) average case, O(n) worst case (all collisions)
 * - Phrase search: O(n*m) where n = docs, m = query words
 * - Memory: O(V + O) where V = vocabulary size, O = total occurrences
 *
 * USAGE EXAMPLE:
 * -------------
 * InvertedIndex *idx = create_index(1024);
 * index_word(idx, "hello", file_id=1, sentence_id=1, page=1, ...);
 * Occurrence *results = search_keyword(idx, "hello");
 * // results now contains all occurrences of "hello"
 *
 * ============================================================================
 */

#include "../include/index.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// DJB2 Hash Function
static unsigned long hash(const char *str) {
  unsigned long hash = 5381;
  int c;
  while ((c = *str++))
    hash = ((hash << 5) + hash) + c; // hash * 33 + c
  return hash;
}

InvertedIndex *create_index(int size) {
  if (size <= 0)
    return NULL;
  InvertedIndex *idx = (InvertedIndex *)malloc(sizeof(InvertedIndex));
  if (!idx)
    return NULL;

  idx->size = size;
  idx->buckets = (IndexEntry **)calloc(size, sizeof(IndexEntry *));
  if (!idx->buckets) {
    free(idx);
    return NULL;
  }
  return idx;
}

// FIX 1: Added 'int page_number' to match header and fix "undeclared variable"
// error FIX 2: Added sentence_offset and sentence_len
void index_word(InvertedIndex *idx, const char *word, int file_id,
                int sentence_id, int page_number, long sentence_offset,
                int sentence_len, int position) {
  if (!idx || !word)
    return;

  // Copy and normalize
  char *key = strdup(word);
  if (!key)
    return;
  for (int i = 0; key[i]; i++) {
    key[i] = tolower(key[i]);
  }

  unsigned long h_val = hash(key);
  int bucket_idx = h_val % idx->size;

  // Search for existing entry in the bucket
  IndexEntry *entry = idx->buckets[bucket_idx];
  while (entry) {
    if (strcmp(entry->word, key) == 0) {
      break;
    }
    entry = entry->next;
  }

  if (!entry) {
    // Create new IndexEntry if not found
    entry = (IndexEntry *)malloc(sizeof(IndexEntry));
    if (!entry) {
      free(key);
      return;
    }
    entry->word = key; // takes ownership of normalized string
    entry->occurrences = NULL;
    entry->next = idx->buckets[bucket_idx];
    idx->buckets[bucket_idx] = entry;
  } else {
    free(key); // We found the entry, so we don't need the copy of the word
  }

  // Find occurrence for this file/sentence/page combo
  Occurrence *occ = entry->occurrences;
  while (occ) {
    if (occ->file_id == file_id && occ->sentence_id == sentence_id &&
        occ->page_number == page_number) {
      // Existing occurrence, update frequency and positions
      occ->frequency++;
      if (occ->frequency > occ->capacity) {
        occ->capacity *= 2;
        occ->positions =
            (int *)realloc(occ->positions, occ->capacity * sizeof(int));
      }
      occ->positions[occ->frequency - 1] = position;
      return;
    }
    occ = occ->next;
  }

  // New occurrence node
  Occurrence *new_occ = (Occurrence *)malloc(sizeof(Occurrence));
  new_occ->file_id = file_id;
  new_occ->sentence_id = sentence_id;
  new_occ->page_number = page_number;
  new_occ->sentence_offset = sentence_offset; // Store offset
  new_occ->sentence_len = sentence_len;       // Store length
  new_occ->frequency = 1;
  new_occ->capacity = 4;
  new_occ->positions = (int *)malloc(new_occ->capacity * sizeof(int));
  new_occ->positions[0] = position;
  new_occ->next = entry->occurrences;
  entry->occurrences = new_occ;
}

// ============================================================================
// RANKING LOGIC (MERGE SORT LINKED LIST)
// ============================================================================

// Helper to split list for merge sort
void split_list(Occurrence *source, Occurrence **front, Occurrence **back) {
  Occurrence *fast;
  Occurrence *slow;
  slow = source;
  fast = source->next;
  while (fast != NULL) {
    fast = fast->next;
    if (fast != NULL) {
      slow = slow->next;
      fast = fast->next;
    }
  }
  *front = source;
  *back = slow->next;
  slow->next = NULL;
}

// Helper to merge sorted lists (Sorted by Frequency DESC)
Occurrence *merge_lists(Occurrence *a, Occurrence *b) {
  Occurrence *result = NULL;
  if (a == NULL)
    return b;
  if (b == NULL)
    return a;

  if (a->frequency >= b->frequency) {
    result = a;
    result->next = merge_lists(a->next, b);
  } else {
    result = b;
    result->next = merge_lists(a, b->next);
  }
  return result;
}

// Main merge sort function
void sort_occurrences(Occurrence **head_ref) {
  Occurrence *head = *head_ref;
  Occurrence *a;
  Occurrence *b;
  if ((head == NULL) || (head->next == NULL)) {
    return;
  }
  split_list(head, &a, &b);
  sort_occurrences(&a);
  sort_occurrences(&b);
  *head_ref = merge_lists(a, b);
}

Occurrence *search_keyword(InvertedIndex *idx, const char *keyword) {
  // FIX 2: Hash function takes 1 arg. Modulo must be applied to the result.
  unsigned int h = hash(keyword) % idx->size;

  IndexEntry *entry = idx->buckets[h];
  while (entry) {
    if (strcmp(entry->word, keyword) == 0) {
      // Sort results by frequency before returning if list > 1
      if (entry->occurrences && entry->occurrences->next) {
        sort_occurrences(&entry->occurrences);
      }
      return entry->occurrences;
    }
    entry = entry->next;
  }
  return NULL;
}

// -----------------------------------------------------------------------------
// Sentence Search Logic
// -----------------------------------------------------------------------------

Occurrence *search_sentence(InvertedIndex *idx, const char *query) {
  // 1. Tokenize query
  char *q_copy = strdup(query);
  char *tokens[64];
  int token_count = 0;
  // main.c uses: " \t\n\r,;:\"()[]{}!?.+=<>|&^%*-~'"
  // We add .?! to ensure they are stripped from the query token
  char *token = strtok(q_copy, " \t\n\r,;:\"()[]{}!?.+=<>|&^%*-~'");
  while (token && token_count < 64) {
    tokens[token_count++] = token;
    token = strtok(NULL, " \t\n\r,;:\"()[]{}!?.+=<>|&^%*-~'");
  }

  if (token_count == 0) {
    free(q_copy);
    return NULL;
  }

  // 2. Get matches for the first word
  Occurrence *candidates = search_keyword(idx, tokens[0]);
  if (!candidates) {
    free(q_copy);
    return NULL;
  }

  // 3. Filter candidates based on subsequent words
  Occurrence *head = NULL;
  Occurrence *tail = NULL;

  Occurrence *curr = candidates;
  while (curr) {
    // For each position of the first word:
    for (int i = 0; i < curr->frequency; i++) {
      int start_pos = curr->positions[i];
      int match = 1; // Assume match until proven otherwise

      // Check subsequent tokens
      for (int j = 1; j < token_count; j++) {
        Occurrence *next_word_occ = search_keyword(idx, tokens[j]);
        int found_next = 0;
        // Find occurrence of next word in SAME file/sentence/page
        while (next_word_occ) {
          if (next_word_occ->file_id == curr->file_id &&
              next_word_occ->sentence_id == curr->sentence_id &&
              next_word_occ->page_number == curr->page_number) {
            // Check if it has position == start_pos + j
            for (int k = 0; k < next_word_occ->frequency; k++) {
              if (next_word_occ->positions[k] == start_pos + j) {
                found_next = 1;
                break;
              }
            }
          }
          if (found_next)
            break;
          next_word_occ = next_word_occ->next;
        }
        if (!found_next) {
          match = 0;
          break;
        }
      }
      if (match) {
        // Found a full sentence match! Add to results.
        Occurrence *new_node = (Occurrence *)malloc(sizeof(Occurrence));
        new_node->file_id = curr->file_id;
        new_node->sentence_id = curr->sentence_id;
        new_node->page_number = curr->page_number;
        new_node->sentence_offset = curr->sentence_offset; // Copy offset
        new_node->sentence_len = curr->sentence_len;       // Copy length
        new_node->frequency = 1;    // It's a phrase match count
        new_node->positions = NULL; // Not needed for result display
        new_node->capacity = 0;
        new_node->next = NULL;
        if (!head) {
          head = new_node;
          tail = new_node;
        } else {
          tail->next = new_node;
          tail = new_node;
        }
        break;
      }
    }
    curr = curr->next;
  }

  free(q_copy);
  return head;
}

void free_index(InvertedIndex *idx) {
  if (!idx)
    return;
  for (int i = 0; i < idx->size; i++) {
    IndexEntry *entry = idx->buckets[i];
    while (entry) {
      IndexEntry *next_entry = entry->next;

      // Free word copy
      free(entry->word);

      // Free occurrences
      Occurrence *occ = entry->occurrences;
      while (occ) {
        Occurrence *next_occ = occ->next;
        if (occ->positions) {
          free(occ->positions);
        }
        free(occ);
        occ = next_occ;
      }

      free(entry);
      entry = next_entry;
    }
  }
  free(idx->buckets);
  free(idx);
}

// ============================================================================
// PERSISTENCE IMPLEMENTATION
// ============================================================================

// Binary File Header Structure (implied)
// [4 bytes] Magic Number: "MINI" (0x494E494D)
// [4 bytes] Version: 1
// [4 bytes] Index Size (number of buckets)

#define MAGIC_NUMBER 0x494E494D
#define INDEX_VERSION 1

void save_index(InvertedIndex *idx, const char *filename) {
  if (!idx || !filename)
    return;

  FILE *f = fopen(filename, "wb");
  if (!f) {
    perror("Failed to open index file for writing");
    return;
  }

  // 1. Write Header
  int magic = MAGIC_NUMBER;
  int version = INDEX_VERSION;
  fwrite(&magic, sizeof(int), 1, f);
  fwrite(&version, sizeof(int), 1, f);
  fwrite(&idx->size, sizeof(int), 1, f);

  // 2. Write Buckets
  for (int i = 0; i < idx->size; i++) {
    IndexEntry *entry = idx->buckets[i];
    if (!entry)
      continue; // Skip empty buckets

    // Count entries in this bucket
    int entry_count = 0;
    IndexEntry *temp = entry;
    while (temp) {
      entry_count++;
      temp = temp->next;
    }

    // Write Bucket Info: [Index] [Count]
    fwrite(&i, sizeof(int), 1, f);
    fwrite(&entry_count, sizeof(int), 1, f);

    // Write Entries
    entry = idx->buckets[i];
    while (entry) {
      // Write Word: [Length] [Chars]
      int word_len = strlen(entry->word);
      fwrite(&word_len, sizeof(int), 1, f);
      fwrite(entry->word, sizeof(char), word_len, f);

      // Count Occurrences
      int occ_count = 0;
      Occurrence *occ = entry->occurrences;
      while (occ) {
        occ_count++;
        occ = occ->next;
      }
      fwrite(&occ_count, sizeof(int), 1, f);

      // Write Occurrences
      occ = entry->occurrences;
      while (occ) {
        fwrite(&occ->file_id, sizeof(int), 1, f);
        fwrite(&occ->sentence_id, sizeof(int), 1, f);
        fwrite(&occ->page_number, sizeof(int), 1, f);
        fwrite(&occ->sentence_offset, sizeof(long), 1, f);
        fwrite(&occ->sentence_len, sizeof(int), 1, f);
        fwrite(&occ->frequency, sizeof(int), 1, f);
        // Note: We write first position but positions array is not fully
        // serialized in this v1
        fwrite(&occ->position, sizeof(int), 1, f);
        occ = occ->next;
      }

      entry = entry->next;
    }
  }

  // Write End Marker for Buckets (Index = -1)
  int marker = -1;
  fwrite(&marker, sizeof(int), 1, f);

  fclose(f);
  printf("Index saved to '%s'\n", filename);
}

InvertedIndex *load_index(const char *filename) {
  FILE *f = fopen(filename, "rb");
  if (!f)
    return NULL;

  // 1. Read Header
  int magic, version, size;
  if (fread(&magic, sizeof(int), 1, f) != 1 || magic != MAGIC_NUMBER) {
    // Not a valid index file
    fclose(f);
    return NULL;
  }
  if (fread(&version, sizeof(int), 1, f) != 1 || version != INDEX_VERSION) {
    fprintf(stderr, "Index version mismatch. Expected %d, got %d\n",
            INDEX_VERSION, version);
    fclose(f);
    return NULL;
  }
  if (fread(&size, sizeof(int), 1, f) != 1) {
    fclose(f);
    return NULL;
  }

  // Create Index
  InvertedIndex *idx = create_index(size);
  if (!idx) {
    fclose(f);
    return NULL;
  }

  // 2. Read Buckets
  while (1) {
    int bucket_idx;
    if (fread(&bucket_idx, sizeof(int), 1, f) != 1)
      break;

    if (bucket_idx == -1)
      break; // End marker

    int entry_count;
    fread(&entry_count, sizeof(int), 1, f);

    // Read Entries
    IndexEntry **bucket_ptr = &idx->buckets[bucket_idx];

    for (int j = 0; j < entry_count; j++) {
      // Read Word
      int word_len;
      fread(&word_len, sizeof(int), 1, f);
      char *word = (char *)malloc(word_len + 1);
      fread(word, sizeof(char), word_len, f);
      word[word_len] = '\0';

      // Create Entry
      IndexEntry *new_entry = (IndexEntry *)malloc(sizeof(IndexEntry));
      new_entry->word = word;
      new_entry->occurrences = NULL;
      new_entry->next = NULL;

      // Read Occurrences
      int occ_count;
      fread(&occ_count, sizeof(int), 1, f);

      Occurrence **occ_ptr = &new_entry->occurrences;
      for (int k = 0; k < occ_count; k++) {
        Occurrence *new_occ = (Occurrence *)malloc(sizeof(Occurrence));
        fread(&new_occ->file_id, sizeof(int), 1, f);
        fread(&new_occ->sentence_id, sizeof(int), 1, f);
        fread(&new_occ->page_number, sizeof(int), 1, f);
        fread(&new_occ->sentence_offset, sizeof(long), 1, f);
        fread(&new_occ->sentence_len, sizeof(int), 1, f);
        fread(&new_occ->frequency, sizeof(int), 1, f);
        int position;
        fread(&position, sizeof(int), 1, f);
        new_occ->position = position; // Restore first position

        new_occ->next = NULL;

        // Partially restore positions array (v1 limitation: only first position
        // restored) This allows basic search but might limit precise phrase
        // searching to first instance
        new_occ->capacity = 1;
        new_occ->positions = (int *)malloc(sizeof(int));
        new_occ->positions[0] = position;

        *occ_ptr = new_occ;
        occ_ptr = &new_occ->next;
      }

      // Append to Bucket
      *bucket_ptr = new_entry;
      bucket_ptr = &new_entry->next;
    }
  }

  fclose(f);
  printf("Index loaded from '%s' (%d buckets)\n", filename, size);
  return idx;
}
