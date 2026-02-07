#include "../include/index.h"
#include "../include/trie.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BUFFER_SIZE 1024 * 1024 // 1MB buffer cap

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
        // Actually, a marker might be in middle of sentence (unlikely with our
        // extractor) or start of new one. Let's assume it doesn't break a
        // sentence unless punctuation follows. But if we are tracking "sentence
        // start", we should probably advance it if it was pointing to the
        // marker.
        if (sentence_start < cursor) {
          sentence_start = cursor;
        }
        continue;
      }
    }

    if (*cursor == '.' || *cursor == '?' || *cursor == '!') {
      // Found a sentence end
      char delimiter = *cursor;
      *cursor = '\0';

      // Process terms in this sentence
      char *sentence_copy = strdup(sentence_start);
      if (sentence_copy) {
        int position = 0;
        char *word = strtok(sentence_copy, " \t\n\r,;:\"()[]{}");
        // Note: brackets [] added to delimiters to clean up any marker residue
        // if parsing failed
        while (word) {
          // Skip if it looks like part of a marker (unlikely due to above
          // logic, but safety)
          if (strncmp(word, "PAGE:", 5) != 0) {
            index_word(idx, word, file_id, sentence_id, current_page, position);
            insert_word(trie, word);
            position++;
            word_count++;
          }
          word = strtok(NULL, " \t\n\r,;:\"()[]{}");
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
      char *word = strtok(sentence_copy, " \t\n\r,;:\"()[]{}");
      while (word) {
        if (strncmp(word, "PAGE:", 5) != 0) {
          index_word(idx, word, file_id, sentence_id, current_page, position);
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

  if (length > MAX_BUFFER_SIZE) {
    fprintf(stderr,
            "Warning: File '%s' is too large for this demo (limit 1MB). "
            "indexing truncated.\n",
            filepath);
    length = MAX_BUFFER_SIZE - 1;
  }

  char *buffer = (char *)malloc(length + 1);
  if (!buffer) {
    fclose(f);
    return;
  }

  fread(buffer, 1, length, f);
  buffer[length] = '\0';
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
  printf("%-10s %-12s %-10s %-10s\n", "File ID", "Page", "Sentence",
         "Frequency");
  printf("----------------------------------------------------\n");
  while (occ) {
    printf("%-10d %-12d %-10d %-10d\n", occ->file_id, occ->page_number,
           occ->sentence_id, occ->frequency);
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

    if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
      break;

    } else if (strcmp(cmd, "help") == 0) {
      printf("Commands:\n");
      printf("  index <file1> <file2> ...\n");
      printf("  search <word>\n");
      printf("  sentence \"<phrase>\"\n");
      printf("  files\n"); // NEW
      printf("  autocomplete <prefix>\n");
      printf("  exit\n");

    } else if (strcmp(cmd, "files") == 0) {
      print_file_registry(file_registry);

    } else if (strcmp(cmd, "index") == 0) {
      int arg_count = 0;
      char *t = strtok(NULL, delim);
      while (t && arg_count < MAX_ARGS) {
        args[arg_count++] = t;
        t = strtok(NULL, delim);
      }

      for (int i = 0; i < arg_count; i++) {
        index_file(idx, trie, session_file_id_counter++, args[i]);
      }

      if (arg_count == 0)
        printf("Usage: index <file1> ...\n");
      else
        printf("Indexing complete. Current File ID Counter: %d\n",
               session_file_id_counter);

    } else if (strcmp(cmd, "search") == 0) {
      char *arg = strtok(NULL, delim);
      if (arg) {
        printf("Searching for: '%s'\n", arg);
        Occurrence *results = search_keyword(idx, arg);
        print_search_results(results);
      } else {
        printf("Usage: search <word>\n");
      }

    } else if (strcmp(cmd, "autocomplete") == 0) {
      char *arg = strtok(NULL, delim);
      if (arg) {
        printf("Autocomplete suggestions for: '%s'\n", arg);
        autocomplete(trie, arg);
      } else {
        printf("Usage: autocomplete <prefix>\n");
      }

    } else if (strcmp(cmd, "sentence") == 0) {
      char query[1024] = "";
      char *arg = strtok(NULL, delim);
      while (arg) {
        strcat(query, arg);
        arg = strtok(NULL, delim);
        if (arg)
          strcat(query, " ");
      }

      // Remove quotes
      if (strlen(query) > 1 && query[0] == '"' &&
          query[strlen(query) - 1] == '"') {
        query[strlen(query) - 1] = '\0';
        memmove(query, query + 1, strlen(query));
      }

      if (strlen(query) > 0) {
        printf("Searching for sentence: '%s'\n", query);
        Occurrence *results = search_sentence(idx, query);
        print_search_results(results);

        // Free result list details if we deep copied, but here we just free
        // nodes
        while (results) {
          Occurrence *tmp = results;
          results = results->next;
          free(tmp);
        }
      } else {
        printf("Usage: sentence \"<query>\"\n");
      }

    } else {
      printf("Unknown command: '%s'. Type 'help' for usage.\n", cmd);
    }
    fflush(stdout);
  }
}

// -----------------------------------------------------------------------------
// Main Driver
// -----------------------------------------------------------------------------

int main(int argc, char **argv) {
  if (argc < 2) {
    print_help(argv[0]);
    return 1;
  }

  InvertedIndex *idx = create_index(1024);
  TrieNode *trie = create_trie_node();

  if (!idx || !trie) {
    fprintf(stderr, "Critical Error: Failed to initialize data structures.\n");
    return 1;
  }

  const char *command = argv[1];

  if (strcmp(command, "interactive") == 0) {
    run_interactive_mode(idx, trie);
  } else if (strcmp(command, "index") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: %s index <file1> ...\n", argv[0]);
    } else {
      for (int i = 2; i < argc; i++) {
        index_file(idx, trie, i - 1, argv[i]);
      }
      printf("\nIndexing complete.\n");
    }
  } else if (strcmp(command, "search") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: %s search <word>\n", argv[0]);
    } else {
      const char *word = argv[2];
      printf("Searching for: '%s'\n", word);
      Occurrence *results = search_keyword(idx, word);
      print_search_results(results);
    }
  } else if (strcmp(command, "autocomplete") == 0) {
    if (argc < 3) {
      fprintf(stderr, "Usage: %s autocomplete <prefix>\n", argv[0]);
    } else {
      const char *prefix = argv[2];
      printf("Autocomplete suggestions for: '%s'\n", prefix);
      autocomplete(trie, prefix);
    }
  } else {
    fprintf(stderr, "Unknown command: %s\n", command);
    print_help(argv[0]);
  }

  // Cleanup
  free_index(idx);
  free_trie(trie);

  return 0;
}
