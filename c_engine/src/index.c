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

void index_word(InvertedIndex *idx, const char *word, int file_id,
                int sentence_id, int position) {
  if (!idx || !word)
    return;

  // Normalize word to lowercase (simple version)
  // In a real generic engine, we might duplicate the string to ensure ownership
  // and normalization. checking strict constraints "C only receives word
  // strings". We will strdup the word for the key if we create a new entry. We
  // should normalize it first.

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
    free(key); // We found it, so we don't need the copy
  }

  // Add/Update Occurrence
  // Optimization: Check the head of the list first (most likely for sequential
  // sentence processing)
  if (entry->occurrences && entry->occurrences->file_id == file_id &&
      entry->occurrences->sentence_id == sentence_id) {

    Occurrence *occ = entry->occurrences;
    // Resize positions array if needed
    if (occ->frequency >= occ->capacity) {
      int new_cap = occ->capacity * 2;
      int *new_pos = (int *)realloc(occ->positions, new_cap * sizeof(int));
      if (new_pos) {
        occ->positions = new_pos;
        occ->capacity = new_cap;
      } else {
        // Realloc failed, drop this position or handle error?
        // For now, just return to avoid crash, though data is incomplete.
        return;
      }
    }
    occ->positions[occ->frequency] = position;
    occ->frequency++;
  } else {
    // Prepend new occurrence
    Occurrence *occ = (Occurrence *)malloc(sizeof(Occurrence));
    if (occ) {
      occ->file_id = file_id;
      occ->sentence_id = sentence_id;
      occ->frequency = 1;
      occ->capacity = 4; // Initial capacity
      occ->positions = (int *)malloc(occ->capacity * sizeof(int));
      if (occ->positions) {
        occ->positions[0] = position;
      } else {
        // fallback if malloc fails
        occ->capacity = 0;
      }
      occ->next = entry->occurrences;
      entry->occurrences = occ;
    }
  }
}

Occurrence *search_keyword(InvertedIndex *idx, const char *keyword) {
  if (!idx || !keyword)
    return NULL;

  // Normalize keyword
  char *key = strdup(keyword);
  if (!key)
    return NULL;
  for (int i = 0; key[i]; i++)
    key[i] = tolower(key[i]);

  unsigned long h_val = hash(key);
  int bucket_idx = h_val % idx->size;

  IndexEntry *entry = idx->buckets[bucket_idx];
  while (entry) {
    if (strcmp(entry->word, key) == 0) {
      free(key);
      return entry->occurrences;
    }
    entry = entry->next;
  }

  free(key);
  return NULL;
}

Occurrence *search_sentence(InvertedIndex *idx, const char *query) {
  if (!idx || !query)
    return NULL;

  // 1. Parse query into words
  char *q_copy = strdup(query);
  if (!q_copy)
    return NULL;

  // Use a fixed size array for query words for simplicity, limit 128 words
  // In production, use dynamic list
  char *words[128];
  int count = 0;

  char *token = strtok(q_copy, " \t\n\r,;:\"()[]{}");
  while (token && count < 128) {
    // Normalize
    for (int i = 0; token[i]; i++)
      token[i] = tolower(token[i]);
    words[count++] = token;
    token = strtok(NULL, " \t\n\r,;:\"()[]{}");
  }

  if (count == 0) {
    free(q_copy);
    return NULL;
  }

  // 2. Fetch Occurrences
  // We need to find the intersection of all words
  // Optimization: Start with the word with fewest occurrences?
  // For now, just start with the first word
  Occurrence *candidates = search_keyword(idx, words[0]);
  if (!candidates) {
    free(q_copy);
    return NULL;
  }

  // We will build a list of matches.
  // However, simply keeping pointers to the first word's Occurrences is not
  // enough because we need to verify positions. AND: One sentence can have
  // multiple occurrences of the first word. We should iterate through
  // candidates (occurrences of first word) and check if they are part of a
  // valid sequence.

  Occurrence *results_head = NULL;
  Occurrence *results_tail = NULL;

  // Iterate over each occurrence of the first word
  for (Occurrence *curr = candidates; curr != NULL; curr = curr->next) {
    int fid = curr->file_id;
    int sid = curr->sentence_id;

    // Check if this (fid, sid) is valid for all other words and has consecutive
    // positions

    // For the first word, we have a list of positions: curr->positions
    // We need to find a generic way to check sequence:
    // Exists p0 in curr->positions such that
    //    (fid, sid, p0+1) is in word[1]
    //    (fid, sid, p0+2) is in word[2]
    //    ...

    int match_found = 0;

    for (int p_idx = 0; p_idx < curr->frequency; p_idx++) {
      int start_pos = curr->positions[p_idx];
      int valid_seq = 1;

      // Check subsequent words
      for (int w = 1; w < count; w++) {
        Occurrence *next_word_occs = search_keyword(idx, words[w]);
        // Find (fid, sid) in next_word_occs
        // Optimization: The list is sorted by insertion (LIFO for files,
        // sequential for sentences usually). Actually search_keyword returns
        // the head of the list for that word. We need to traverse it to find
        // (fid, sid).

        int found_next = 0;
        while (next_word_occs) {
          if (next_word_occs->file_id == fid &&
              next_word_occs->sentence_id == sid) {
            // Check position
            int needed_pos = start_pos + w;
            for (int k = 0; k < next_word_occs->frequency; k++) {
              if (next_word_occs->positions[k] == needed_pos) {
                found_next = 1;
                break;
              }
            }
          }
          if (found_next)
            break;
          next_word_occs = next_word_occs->next;
        }

        if (!found_next) {
          valid_seq = 0;
          break;
        }
      }

      if (valid_seq) {
        match_found++;
      }
    }

    if (match_found > 0) {
      // Add to results
      Occurrence *res = (Occurrence *)malloc(sizeof(Occurrence));
      res->file_id = fid;
      res->sentence_id = sid;
      res->frequency = match_found;  // Actual count of phrase occurrences
      res->positions = NULL; // Not needed for result display
      res->capacity = 0;
      res->next = NULL;

      if (!results_head) {
        results_head = res;
        results_tail = res;
      } else {
        results_tail->next = res;
        results_tail = res;
      }
    }
  }

  free(q_copy);
  return results_head;
}

void free_index(InvertedIndex *idx) {
  if (!idx)
    return;

  for (int i = 0; i < idx->size; i++) {
    IndexEntry *entry = idx->buckets[i];
    while (entry) {
      IndexEntry *next_entry = entry->next;

      // Free occurrences
      Occurrence *occ = entry->occurrences;
      while (occ) {
        Occurrence *next_occ = occ->next;
        if (occ->positions)
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
