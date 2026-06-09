/*
 * ============================================================================
 * TRIE.C  —  FM-INDEX AUTOCOMPLETE SHIM
 * ============================================================================
 * FILE: src/trie.c
 *
 * The 26-ary prefix trie is REPLACED by FM-Index prefix-range search.
 * All original function signatures are preserved so main.c compiles without
 * modification.
 *
 * Runtime behaviour:
 *   create_trie_node() → returns a non-NULL sentinel (1-byte allocation).
 *   insert_word()      → no-op (FM-Index already contains all words).
 *   autocomplete()     → calls fm_prefix_search(), prints "Suggestion: <w>".
 *   free_trie()        → frees the sentinel node.
 * ============================================================================
 */

#include "../include/trie.h"
#include "../include/fm_index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * LIFECYCLE
 * ========================================================================== */

/*
 * create_trie_node: returns a small sentinel allocation so that callers
 * (main.c) can safely check `if (!root)` and pass root around.
 * The internal TrieNode fields are zeroed but never used.
 */
TrieNode *create_trie_node(void) {
    TrieNode *node = calloc(1, sizeof(TrieNode));
    return node;   /* NULL on OOM — caller checks */
}

/*
 * insert_word: intentionally a no-op.
 *
 * Under the FM-Index backend, every indexed word is reachable via backward
 * search on any prefix.  Maintaining a parallel trie would be redundant.
 */
void insert_word(TrieNode *root, const char *word) {
    (void)root;
    (void)word;
}

/*
 * free_trie: frees only the sentinel node — no children were ever allocated.
 */
void free_trie(TrieNode *root) {
    free(root);
}

/* ============================================================================
 * AUTOCOMPLETE
 * ========================================================================== */

/*
 * autocomplete:
 *   1. Retrieves the global FMIndex (set by create_index / load_index).
 *   2. Calls fm_prefix_search() to get up to FM_MAX_SUGGEST unique words.
 *   3. Prints each word in the format expected by api.py:
 *        "Suggestion: <word>"
 *   4. Frees the word list.
 *
 * The `root` parameter is ignored — the FM-Index singleton provides data.
 *
 * Output format mirror (api.py parses lines starting with "Suggestion: "):
 *   --- Autocomplete results for '<prefix>' ---
 *   Suggestion: word1
 *   Suggestion: word2
 *   ...
 */
void autocomplete(TrieNode *root, const char *prefix) {
    (void)root;

    if (!prefix || prefix[0] == '\0') {
        printf("No suggestions found for prefix: (empty)\n");
        fflush(stdout);
        return;
    }

    FMIndex *fm = fm_get_global();
    if (!fm || fm->bwt_len == 0) {
        printf("No suggestions found for prefix: %s\n", prefix);
        fflush(stdout);
        return;
    }

    /* Lowercase the prefix for consistent matching */
    char lc_prefix[256];
    int pl = (int)strlen(prefix);
    if (pl > 255) pl = 255;
    for (int i = 0; i < pl; i++)
        lc_prefix[i] = (char)tolower((unsigned char)prefix[i]);
    lc_prefix[pl] = '\0';

    char *words[FM_MAX_SUGGEST];
    int   count = fm_prefix_search(fm, lc_prefix, words, FM_MAX_SUGGEST);

    printf("--- Autocomplete results for '%s' ---\n", prefix);
    if (count == 0) {
        printf("No suggestions found for prefix: %s\n", prefix);
    } else {
        for (int i = 0; i < count; i++) {
            printf("Suggestion: %s\n", words[i]);
            free(words[i]);
        }
    }
    fflush(stdout);
}