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
// error
void index_word(InvertedIndex *idx, const char *word, int file_id,
                int sentence_id, int page_number, int position) {
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
  new_occ->frequency = 1;
  new_occ->capacity = 4;
  new_occ->positions = (int *)malloc(new_occ->capacity * sizeof(int));
  new_occ->positions[0] = position;
  new_occ->next = entry->occurrences;
  entry->occurrences = new_occ;
}

Occurrence *search_keyword(InvertedIndex *idx, const char *keyword) {
  // FIX 2: Hash function takes 1 arg. Modulo must be applied to the result.
  unsigned int h = hash(keyword) % idx->size;

  IndexEntry *entry = idx->buckets[h];
  while (entry) {
    if (strcmp(entry->word, keyword) == 0)
      return entry->occurrences;
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
  char *token = strtok(q_copy, " \t\n");
  while (token && token_count < 64) {
    tokens[token_count++] = token;
    token = strtok(NULL, " \t\n");
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
      Occurrence *occ = entry->occurrences;
      while (occ) {
        Occurrence *next_occ = occ->next;
        free(occ->positions);
        free(occ);
        occ = next_occ;
      }
      free(entry->word);
      free(entry);
      entry = next_entry;
    }
  }
  free(idx->buckets);
  free(idx);
}

// -----------------------------------------------------------------------------
// File Registry
// -----------------------------------------------------------------------------

FileRegistry *create_file_registry() {
  FileRegistry *reg = (FileRegistry *)malloc(sizeof(FileRegistry));
  if (reg) {
    reg->count = 0;
  }
  return reg;
}

void register_file(FileRegistry *reg, int file_id, const char *filepath,
                   long size, int words, int sentences, int pages) {
  if (!reg || reg->count >= MAX_FILES)
    return;

  FileMetadata *meta = &reg->files[reg->count++];
  meta->file_id = file_id;
  strncpy(meta->filename, filepath, MAX_FILENAME_LEN - 1);
  meta->filename[MAX_FILENAME_LEN - 1] = '\0';
  meta->size_bytes = size;
  meta->word_count = words;
  meta->sentence_count = sentences;
  meta->page_count = pages;
}

void print_file_registry(FileRegistry *reg) {
  if (!reg)
    return;

  printf("\n%-8s | %-30s | %-10s | %-8s | %-10s | %-6s\n", "ID", "Filename",
         "Size(KB)", "Words", "Sentences", "Pages");
  printf("---------|--------------------------------|------------|----------|--"
         "----------|-------\n");

  for (int i = 0; i < reg->count; i++) {
    FileMetadata *m = &reg->files[i];
    printf("%-8d | %-30s | %-10ld | %-8d | %-10d | %-6d\n", m->file_id,
           m->filename, m->size_bytes / 1024, m->word_count, m->sentence_count,
           m->page_count);
  }
  printf("\n");
}
