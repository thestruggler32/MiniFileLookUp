#ifndef TRIE_H
#define TRIE_H

#include <stdbool.h>

// Assuming only lowercase English letters a-z
#define ALPHABET_SIZE 26

typedef struct TrieNode {
    struct TrieNode *children[ALPHABET_SIZE];
    bool is_end_of_word;
} TrieNode;

/**
 * Creates a new initialized Trie node.
 * Returns NULL on allocation failure.
 */
TrieNode* create_trie_node(void);

/**
 * Inserts a word into the Trie.
 * Converts uppercase to lowercase automatically if needed, or expects lowercase.
 * We will strict to lowercase a-z based on constraints.
 */
void insert_word(TrieNode *root, const char *word);

/**
 * Prints all words in the Trie that start with the given prefix.
 */
void autocomplete(TrieNode *root, const char *prefix);

/**
 * Frees all memory associated with the Trie.
 */
void free_trie(TrieNode *root);

#endif
