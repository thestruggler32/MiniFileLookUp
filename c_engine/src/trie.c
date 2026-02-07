/*
 * ============================================================================
 * TRIE DATA STRUCTURE FOR AUTOCOMPLETE
 * ============================================================================
 *
 * FILE: trie.c
 * DESCRIPTION: Prefix tree (Trie) implementation for fast word suggestions
 *
 * WHAT IS A TRIE?
 * ---------------
 * A Trie (pronounced "try") is a tree data structure used for efficient
 * string prefix matching. Each node represents a single character, and
 * paths from root to leaf spell out complete words.
 *
 * Example Trie for words: "cat", "car", "card", "dog"
 *
 *           (root)
 *          /      \
 *         c        d
 *         |        |
 *         a        o
 *        / \       |
 *       t   r      g*
 *      *    |
 *           d*
 *
 * (* = end of word marker)
 *
 * WHY USE A TRIE FOR AUTOCOMPLETE?
 * ---------------------------------
 * 1. FAST PREFIX SEARCH: O(k) where k = prefix length
 *    - Much faster than scanning all words O(n*k)
 * 2. SPACE EFFICIENT: Shares common prefixes
 *    - "cat", "car", "card" share "ca" prefix
 * 3. SORTED OUTPUT: In-order traversal gives alphabetical results
 *
 * IMPLEMENTATION DETAILS:
 * ----------------------
 * 1. TRIE NODE STRUCTURE:
 *    - children[26]: Array of pointers to child nodes (a-z)
 *    - is_end_of_word: Boolean flag marking complete words
 *
 * 2. ALPHABET SIZE: 26 (lowercase a-z only)
 *    - Non-alphabetic characters are skipped during insertion
 *    - All words are normalized to lowercase
 *
 * 3. MEMORY LAYOUT:
 *    - Each node: 26 pointers + 1 bool = ~208 bytes
 *    - Total memory: O(ALPHABET_SIZE * N * L)
 *      where N = number of words, L = average word length
 *
 * KEY OPERATIONS:
 * --------------
 * - create_trie_node(): Allocate and initialize a new trie node
 * - insert_word(root, word): Add a word to the trie
 * - autocomplete(root, prefix): Find all words starting with prefix
 * - free_trie(root): Recursively free all nodes
 *
 * AUTOCOMPLETE ALGORITHM:
 * ----------------------
 * 1. Navigate to the node representing the prefix
 *    - "ca" -> follow c->a from root
 * 2. Perform DFS (depth-first search) from that node
 * 3. Print all words found in the subtree
 *    - "cat", "car", "card" for prefix "ca"
 *
 * PERFORMANCE:
 * -----------
 * - Insert word: O(L) where L = word length
 * - Search prefix: O(L) to find prefix node
 * - Get suggestions: O(k) where k = number of matching words
 * - Space complexity: O(ALPHABET_SIZE * N * L)
 *
 * USAGE EXAMPLE:
 * -------------
 * TrieNode *root = create_trie_node();
 * insert_word(root, "hello");
 * insert_word(root, "help");
 * insert_word(root, "world");
 * autocomplete(root, "hel");  // Prints: "hello", "help"
 * free_trie(root);
 *
 * INTEGRATION WITH SEARCH ENGINE:
 * ------------------------------
 * The trie is populated during indexing (same time as inverted index).
 * Every word added to the inverted index is also added to the trie.
 * This allows instant autocomplete suggestions as users type their queries.
 *
 * ============================================================================
 */

#include "../include/trie.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TrieNode *create_trie_node(void) {
  TrieNode *node = (TrieNode *)malloc(sizeof(TrieNode));
  if (node) {
    node->is_end_of_word = false;
    for (int i = 0; i < ALPHABET_SIZE; i++) {
      node->children[i] = NULL;
    }
  }
  return node;
}

void insert_word(TrieNode *root, const char *word) {
  if (!root || !word)
    return;

  TrieNode *current = root;
  for (int i = 0; word[i] != '\0'; i++) {
    char ch = tolower(word[i]);
    if (ch < 'a' || ch > 'z') {
      // Skip non-alphabetic characters or handle error
      continue;
    }
    int index = ch - 'a';
    if (!current->children[index]) {
      current->children[index] = create_trie_node();
    }
    current = current->children[index];
  }
  current->is_end_of_word = true;
}

// Recursive helper to print all words from a given node
static void print_suggestions(TrieNode *node, char *buffer, int depth) {
  if (node->is_end_of_word) {
    buffer[depth] = '\0';
    printf("Suggestion: %s\n", buffer);
  }

  for (int i = 0; i < ALPHABET_SIZE; i++) {
    if (node->children[i]) {
      buffer[depth] = 'a' + i;
      print_suggestions(node->children[i], buffer, depth + 1);
    }
  }
}

void autocomplete(TrieNode *root, const char *prefix) {
  if (!root || !prefix)
    return;

  TrieNode *current = root;
  int len = strlen(prefix);

  // Navigate to the end of the prefix
  for (int i = 0; i < len; i++) {
    char ch = tolower(prefix[i]);
    if (ch < 'a' || ch > 'z')
      return; // Invalid prefix
    int index = ch - 'a';
    if (!current->children[index]) {
      printf("No suggestions found for prefix: %s\n", prefix);
      return;
    }
    current = current->children[index];
  }

  // Buffer to hold the formed words. Assuming max word length 100 for demo.
  // We prepend the prefix to the buffer.
  char buffer[256];
  strcpy(buffer, prefix);

  printf("--- Autocomplete results for '%s' ---\n", prefix);
  print_suggestions(current, buffer, len);
}

void free_trie(TrieNode *root) {
  if (!root)
    return;
  for (int i = 0; i < ALPHABET_SIZE; i++) {
    if (root->children[i]) {
      free_trie(root->children[i]);
    }
  }
  free(root);
}