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