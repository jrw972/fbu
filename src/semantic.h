#ifndef semantic_h
#define semantic_h

#include "types.h"

/* Associate a symbol table with each node in the tree. */
void construct_symbol_table (ast_t * node, symtab_t * symtab);

/* Enter all symbols except vars. */
void enter_symbols (ast_t * node);

/* Process all declarations (non-code). */
void process_declarations (ast_t * node);

/* Process all definitions (code). */
void process_definitions (ast_t * node);

/* Check the compositions. */
void check_composition (ast_t * node);

#endif /* semantic_h */
