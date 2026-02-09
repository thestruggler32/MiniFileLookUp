/*
 * ============================================================================
 * MINI SEARCH ENGINE - CORE C ENGINE
 * ============================================================================
 *
 * FILE: main.c
 * DESCRIPTION: Main driver for the high-performance C-based search engine
 *
 * ARCHITECTURE OVERVIEW:
 * ---------------------
 * This is the CORE search engine implementation written in C for maximum
 * performance. The engine provides:
 *
 * 1. INVERTED INDEX: Fast keyword-to-document mapping using hash tables
 * 2. TRIE-BASED AUTOCOMPLETE: Prefix-based word suggestions
 * 3. PHRASE SEARCH: Multi-word query support with positional indexing
 * 4. PAGE-AWARE INDEXING: Tracks page numbers for PDF/DOCX files
 * 5. INTERACTIVE SHELL: Command-line interface for direct C engine access
 * 6. PERSISTENCE: Binary serialization of index to disk (index.dat)
 *
 * PERFORMANCE CHARACTERISTICS:
 * ---------------------------
 * - O(1) average case keyword lookup via hash table
 * - O(k) autocomplete where k = number of matching words
 * - O(n*m) phrase search where n = doc count, m = query length
 * - Scalable: Processes files of any size (limited only by RAM)
 *
 * INTEGRATION:
 * -----------
 * The Python API (api.py) acts as a THIN WRAPPER around this C engine,
 * providing:
 * - HTTP REST interface for web frontend
 * - File upload handling and text extraction
 * - Fallback search logic for complex queries
 *
 * The C engine handles ALL core search operations. Python is only used
 * for I/O, text extraction, and serving the web interface.
 *
 * COMPILATION:
 * -----------
 * gcc -o engine.exe src/main.c src/index.c src/trie.c -I./include
 *
 * USAGE:
 * ------
 * ./engine.exe interactive          # Start interactive shell
 * ./engine.exe index file1.txt      # Index files
 * ./engine.exe search "keyword"     # Search for keyword
 * ./engine.exe autocomplete "pre"   # Get autocomplete suggestions
 *
 * ============================================================================
 */

#include "../include/index.h"
#include "../include/trie.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDEX_FILENAME "index.dat"

// -----------------------------------------------------------------------------
// Helper: File Reading & Processing
// -----------------------------------------------------------------------------

// Basic tokenizer for sentence splitting
void process_text_buffer(InvertedIndex *idx, TrieNode *trie, int file_id,
                         char *text, int *total_words, int *total_sentences,
                         int *total_pages) {
  if (!text)
    return;

  int sentence_id = 1;
  int current_page = 1; // Default to page 1
  int word_count = 0;

  char *cursor = text;
  char *sentence_start = text;

  // Iterate through the text to find sentence delimiters
  while (*cursor != '\0') {

    // Check for Page Marker: [PAGE:N]
    // Heuristic: Check if sequence starts with [PAGE:
    if (*cursor == '[' && strncmp(cursor, "[PAGE:", 6) == 0) {
      // Parse page number
      char *end_marker = strchr(cursor, ']');
      if (end_marker) {
        current_page = atoi(cursor + 6);
        if (current_page > *total_pages)
          *total_pages = current_page;

        // Move cursor past marker
        cursor = end_marker + 1;
        // Reset sentence start to after marker?
        if (sentence_start < cursor) {
          sentence_start = cursor;
        }
        continue;
      }
    }

    // FIX: Switch to LINE/PARAGRAPH based splitting instead of STRICT SENTENCE
    // splitting. This allows "main_memory.v" and multi-sentence queries to
    // work. We treat '\n' as the only hard breaker. Punctuation is just a word
    // delimiter.
    if (*cursor == '\n') {
      // Found a line/paragraph end
      char delimiter = *cursor;
      *cursor = '\0';

      // Process terms in this paragraph
      char *sentence_copy = strdup(sentence_start);
      if (sentence_copy) {
        // Calculate offset and length
        long offset = sentence_start - text;
        int len = cursor - sentence_start;

        int position = 0;
        char *word = strtok(sentence_copy, " \t\n\r,;:\"()[]{}!?.+=<>|&^%*-~'");
        // Note: brackets [] added to delimiters to clean up any marker residue
        // if parsing failed
        while (word) {
          // Skip if it looks like part of a marker (unlikely due to above
          // logic, but safety)
          if (strncmp(word, "PAGE:", 5) != 0) {
            index_word(idx, word, file_id, sentence_id, current_page, offset,
                       len, position);
            insert_word(trie, word);
            position++;
            word_count++;
          }
          word = strtok(NULL, " \t\n\r,;:\"()[]{}!?.+=<>|&^%*-~'");
        }
        free(sentence_copy);
      }

      *cursor = delimiter;
      sentence_id++;
      sentence_start = cursor + 1;
    }
    cursor++;
  }

  // Handle remaining text
  if (cursor > sentence_start) {
    char *sentence_copy = strdup(sentence_start);
    if (sentence_copy) {
      int position = 0;
      char *word = strtok(sentence_copy, " \t\n\r,;:\"()[]{}!?.+=<>|&^%*-~'");
      while (word) {
        if (strncmp(word, "PAGE:", 5) != 0) {
          // For last sentence
          long offset = sentence_start - text;
          int len = cursor - sentence_start;
          index_word(idx, word, file_id, sentence_id, current_page, offset, len,
                     position);
          insert_word(trie, word);
          position++;
          word_count++;
        }
        word = strtok(NULL, " \t\n\r,;:\"()[]{}");
      }
      free(sentence_copy);
    }
  }

  *total_words = word_count;
  *total_sentences = sentence_id - 1;
}

// Global Registry
FileRegistry *file_registry = NULL;

// Reads a file into a buffer and indexes it
void index_file(InvertedIndex *idx, TrieNode *trie, int file_id,
                const char *filepath) {
  if (!filepath || strlen(filepath) == 0)
    return;

  FILE *f = fopen(filepath, "rb");
  if (!f) {
    fprintf(stderr, "Error: Could not open file '%s'. Reason: %s\n", filepath,
            strerror(errno));
    return;
  }

  fseek(f, 0, SEEK_END);
  long length = ftell(f);
  fseek(f, 0, SEEK_SET);

  // REMOVED 1MB LIMIT CHECK.
  // We now allocate exactly what we need.
  // Note: malloc might fail if file is larger than available RAM.
  // Ideally we would check for a sane upper bound (e.g. 2GB)
  // but for "Scalability" mandate, we remove the arbitrary 1MB cap.

  char *buffer = (char *)malloc(length + 1);
  if (!buffer) {
    fprintf(stderr,
            "Error: Failed to allocate memory for file '%s' (%ld bytes)\n",
            filepath, length);
    fclose(f);
    return;
  }

  size_t read_bytes = fread(buffer, 1, length, f);
  if (read_bytes != length) {
    fprintf(stderr, "Warning: Read fewer bytes than expected from '%s'\n",
            filepath);
  }
  buffer[length] = '\0'; // Null-terminate
  fclose(f);

  // Skip UTF-16 check for brevity (assumed handled by extractor.py or plain
  // ASCII)

  printf("Indexing File ID %d: %s (%ld bytes)\n", file_id, filepath, length);
  fflush(stdout);

  int total_words = 0;
  int total_sentences = 0;
  int total_pages = 1;

  process_text_buffer(idx, trie, file_id, buffer, &total_words,
                      &total_sentences, &total_pages);

  // Register file metadata
  if (file_registry) {
    register_file(file_registry, file_id, filepath, length, total_words,
                  total_sentences, total_pages);
  }

  free(buffer);
}

// -----------------------------------------------------------------------------
// CLI Output Helpers
// -----------------------------------------------------------------------------

void print_search_results(Occurrence *occ) {
  if (!occ) {
    printf("No results found.\n");
    return;
  }
  printf("%-10s %-12s %-10s %-12s %-10s %-10s\n", "File ID", "Page", "Sentence",
         "Offset", "Length", "Frequency");
  printf("---------------------------------------------------------------------"
         "-----------\n");
  while (occ) {
    printf("%-10d %-12d %-10d %-12ld %-10d %-10d\n", occ->file_id,
           occ->page_number, occ->sentence_id, occ->sentence_offset,
           occ->sentence_len, occ->frequency);
    occ = occ->next;
  }
}

void print_help(const char *prog_name) {
  printf("Mini Search Engine CLI\n");
  printf("Usage:\n");
  printf("  %s interactive                 : Start persistent shell session\n",
         prog_name);
  printf("  %s index <file1> ...           : Index files and exit\n",
         prog_name);
  printf("  %s search <word>               : Search for a keyword\n",
         prog_name);
  printf("  %s autocomplete <prefix>       : Suggest words\n", prog_name);
}

// -----------------------------------------------------------------------------
// Interactive Shell
// -----------------------------------------------------------------------------

void run_interactive_mode(InvertedIndex *idx, TrieNode *trie) {
  printf("\n=== Interactive Search Engine Shell ===\n");
  printf("Type 'help' for commands, 'exit' to quit.\n");

  // Initialize registry
  file_registry = create_file_registry();

  char line[1024];
  int session_file_id_counter = 1;

#define MAX_ARGS 32
  char *args[MAX_ARGS];

  while (1) {
    printf("> ");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin))
      break;

    line[strcspn(line, "\n")] = 0;
    if (strlen(line) == 0)
      continue;

    const char *delim = " \t\r\n";
    char *cmd = strtok(line, delim);
    if (!cmd)
      continue;

    if (strcmp(cmd, "help") == 0) {
      printf("Commands:\n");
      printf("  index <path>       : Index a file\n");
      printf("  search <query>     : Search for a word or phrase\n");
      printf("  autocomplete <pre> : Get suggestions\n");
      printf("  save               : Save index to disk\n");
      printf("  exit               : Save and exit\n");

    } else if (strcmp(cmd, "index") == 0) {
      char *path = strtok(NULL, delim);
      while (path) {
        index_file(idx, trie, session_file_id_counter++, path);
        path = strtok(NULL, delim);
      }

    } else if (strcmp(cmd, "search") == 0) {
      char *arg = strtok(NULL, ""); // Get rest of line
      if (!arg) {
        printf("Usage: search <query>\n");
        continue;
      }

      // Trim leading spaces
      while (*arg == ' ' || *arg == '\t')
        arg++;

      Occurrence *results = search_sentence(idx, arg);
      print_search_results(results);
      // results list leaks here in interactive mode (should free list nodes)

    } else if (strcmp(cmd, "autocomplete") == 0) {
      char *prefix = strtok(NULL, delim);
      if (!prefix) {
        printf("Usage: autocomplete <prefix>\n");
        continue;
      }
      autocomplete(trie, prefix);

    } else if (strcmp(cmd, "save") == 0) {
      save_index(idx, INDEX_FILENAME);

    } else if (strcmp(cmd, "exit") == 0) {
      save_index(idx, INDEX_FILENAME);
      break;

    } else {
      printf("Unknown command: %s\n", cmd);
    }
  }

  if (file_registry)
    free(file_registry); // registry structure itself
}

int main(int argc, char *argv[]) {
  // Initialize Index and Trie
  InvertedIndex *idx = load_index(INDEX_FILENAME);

  if (idx) {
    // Index loaded!
  } else {
    idx = create_index(1024); // Default size
    if (!idx) {
      fprintf(stderr, "Failed to initialize index.\n");
      return 1;
    }
  }

  TrieNode *root = create_trie_node();
  if (!root) {
    fprintf(stderr, "Failed to initialize trie.\n");
    free_index(idx);
    return 1;
  }

  // Populate Trie from Index (simple rebuild)
  if (idx) {
    for (int i = 0; i < idx->size; i++) {
      IndexEntry *entry = idx->buckets[i];
      while (entry) {
        insert_word(root, entry->word);
        entry = entry->next;
      }
    }
  }

  if (argc < 2) {
    print_help(argv[0]);
    // Cleanup
    free_trie(root);
    free_index(idx);
    return 0;
  }

  char *command = argv[1];

  if (strcmp(command, "interactive") == 0) {
    run_interactive_mode(idx, root);
  } else if (strcmp(command, "index") == 0) {
    // Index one or more files
    for (int i = 2; i < argc; i++) {
      index_file(idx, root, i - 1, argv[i]);
    }
    save_index(idx, INDEX_FILENAME);

  } else if (strcmp(command, "search") == 0) {
    if (argc < 3) {
      printf("Usage: %s search <keyword>\n", argv[0]);
    } else {
      // Reconstruct query
      char query[1024] = "";
      for (int i = 2; i < argc; i++) {
        strcat(query, argv[i]);
        if (i < argc - 1)
          strcat(query, " ");
      }
      Occurrence *results = search_sentence(idx, query);
      print_search_results(results);
    }

  } else if (strcmp(command, "autocomplete") == 0) {
    if (argc < 3) {
      printf("Usage: %s autocomplete <prefix>\n", argv[0]);
    } else {
      autocomplete(root, argv[2]);
    }

  } else {
    printf("Unknown command: %s\n", command);
    print_help(argv[0]);
  }

  // Cleanup
  free_trie(root);
  free_index(idx);

  return 0;
}
