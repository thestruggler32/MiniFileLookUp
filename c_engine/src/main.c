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
                          char *text) {
    if (!text)
      return;

    int sentence_id = 1;
    char *cursor = text;
    char *sentence_start = text;

    // Iterate through the text to find sentence delimiters
    while (*cursor != '\0') {
      if (*cursor == '.' || *cursor == '?' || *cursor == '!') {
        // Found a sentence end
        // Temporarily terminate the string here to process the sentence
        char delimiter = *cursor;
        *cursor = '\0';

        // Process terms in this sentence
        // Make a copy to tokenize words safely
        char *sentence_copy = strdup(sentence_start);
        if (sentence_copy) {
          // Tokenize by space and common punctuation
          int position = 0;
          char *word = strtok(sentence_copy, " \t\n\r,;:\"()[]{}");
          while (word) {
            index_word(idx, word, file_id, sentence_id, position);
            insert_word(trie, word);
            position++;
            word = strtok(NULL, " \t\n\r,;:\"()[]{}");
          }
          free(sentence_copy);
        }

        // Restore delimiter and move to next sentence
        *cursor = delimiter;
        sentence_id++;
        sentence_start = cursor + 1;
      }
      cursor++;
    }

    // Handle any remaining text as the last sentence if not empty
    if (cursor > sentence_start) {
      char *sentence_copy = strdup(sentence_start);
      if (sentence_copy) {
        int position = 0;
        char *word = strtok(sentence_copy, " \t\n\r,;:\"()[]{}");
        while (word) {
          index_word(idx, word, file_id, sentence_id, position);
          insert_word(trie, word);
          position++;
          word = strtok(NULL, " \t\n\r,;:\"()[]{}");
        }
        free(sentence_copy);
      }
    }
  }

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

    // Get file size
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
      fprintf(stderr, "Error: Memory allocation failed for file '%s'.\n",
              filepath);
      fclose(f);
      return;
    }

    fread(buffer, 1, length, f);
    buffer[length] = '\0';
    fclose(f);

    // Check for UTF-16 BOM (Little Endian FF FE or Big Endian FE FF)
    // Cast to unsigned char to avoid sign extension issues
    if (length >= 2 &&
        ((unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xFE)) {
      fprintf(stderr,
              "Error: File '%s' appears to be UTF-16 LE (has BOM). Please save "
              "as ASCII or UTF-8.\n",
              filepath);
      free(buffer);
      return;
    }

    // Heuristic: Check for null bytes in the first 100 bytes (typical for UTF-16
    // 'h\0e\0l\0l\0o')
    int scan_limit = (length > 100) ? 100 : length;
    for (int i = 0; i < scan_limit; i++) {
      if (buffer[i] == '\0') {
        fprintf(stderr,
                "Error: File '%s' appears to be binary or UTF-16 (null bytes "
                "detected). Please save as ASCII or UTF-8.\n",
                filepath);
        free(buffer);
        return;
      }
    }

    printf("Indexing File ID %d: %s (%ld bytes)\n", file_id, filepath, length);
    fflush(stdout);
    process_text_buffer(idx, trie, file_id, buffer);
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
    printf("%-10s %-12s %-10s\n", "File ID", "Sentence ID", "Frequency");
    printf("----------------------------------------\n");
    while (occ) {
      printf("%-10d %-12d %-10d\n", occ->file_id, occ->sentence_id,
            occ->frequency);
      occ = occ->next;
    }
  }

  void print_help(const char *prog_name) {
    printf("Mini Search Engine CLI\n");
    printf("Usage:\n");
    printf("  %s interactive                 : Start persistent shell session "
          "(Recommended).\n",
          prog_name);
    printf("  %s index <file1> ...           : Index files and exit.\n",
          prog_name);
    printf("  %s search <word>               : Search for a keyword (fresh "
          "index).\n",
          prog_name);
    printf("  %s autocomplete <prefix>       : Suggest words (fresh index).\n",
          prog_name);
  }

  // -----------------------------------------------------------------------------
  // Interactive Shell
  // -----------------------------------------------------------------------------

  void run_interactive_mode(InvertedIndex *idx, TrieNode *trie) {
    printf("\n=== Interactive Search Engine Shell ===\n");
    printf("Type 'help' for commands, 'exit' to quit.\n");

    char line[1024];
    int session_file_id_counter = 1;

  // Buffer for collecting filenames to avoid nested strtok calls
  #define MAX_ARGS 32
    char *args[MAX_ARGS];

    while (1) {
      printf("> ");
      fflush(stdout);
      if (!fgets(line, sizeof(line), stdin))
        break;

      // Strip newline but keep robust delimiters
      line[strcspn(line, "\n")] = 0;
      if (strlen(line) == 0)
        continue;

      // Tokenize command with robust delimiters for Windows (\r)
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
        printf("  autocomplete <prefix>\n");
        printf("  exit\n");
      } else if (strcmp(cmd, "index") == 0) {
        // Collect all filenames FIRST to release strtok state
        int arg_count = 0;
        char *t = strtok(NULL, delim);
        while (t && arg_count < MAX_ARGS) {
          args[arg_count++] = t;
          t = strtok(NULL, delim);
        }

        // NOW process them without strtok conflict
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
        // Concatenate remaining tokens to form the query
        char query[1024] = "";
        char *arg = strtok(NULL, delim);
        while (arg) {
          strcat(query, arg);
          arg = strtok(NULL, delim);
          if (arg)
            strcat(query, " ");
        }

        // Remove quotes if present
        if (query[0] == '"' && query[strlen(query) - 1] == '"') {
          query[strlen(query) - 1] = '\0';
          memmove(query, query + 1, strlen(query));
        }

        if (strlen(query) > 0) {
          printf("Searching for sentence: '%s'\n", query);
          Occurrence *results = search_sentence(idx, query);
          print_search_results(results);

          // Free the results list (but not the positions array inside, as it's
          // NULL for results)
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
