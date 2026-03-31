#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Flags for varjson5_process / varjson5_query */
#define VARJSON5_RAW     1  /* output strings without JSON encoding */
#define VARJSON5_COMPACT 2  /* compact (single-line) output */

/*
 * Process JSON5 input with {{vars}} substitution and an optional jq-style filter.
 *
 *   input  - null-terminated JSON5 string
 *   filter - jq-style filter (e.g. ".", ".body", "map(.x)"); pass NULL for "."
 *   flags  - bitwise OR of VARJSON5_RAW / VARJSON5_COMPACT (0 for defaults)
 *
 * Returns a heap-allocated, null-terminated JSON string on success.
 * Multiple results are separated by newlines, matching jq behaviour.
 * Returns NULL on error; call varjson5_last_error() for the message.
 *
 * The caller must free the returned pointer with varjson5_free().
 * Thread-safe: error state is stored per-thread.
 */
char* varjson5_process(const char* input, const char* filter, int flags);

/* Free a string returned by varjson5_process / varjson5_query. */
void varjson5_free(char* ptr);

/* Return the last error message for the calling thread.
 * Valid until the next call to varjson5_process / varjson5_load on the same thread. */
const char* varjson5_last_error(void);

/* -------------------------------------------------------------------------
 * Document API: parse once, query many times
 * -------------------------------------------------------------------------
 * Use these when the same JSON5 document is queried with multiple filters.
 * Parsing and {{vars}} substitution happen only once in varjson5_load().
 *
 * Example:
 *   varjson5_doc* doc = varjson5_load(input);
 *   char* r1 = varjson5_query(doc, ".body",  0);
 *   char* r2 = varjson5_query(doc, ".config", VARJSON5_COMPACT);
 *   varjson5_free(r1);
 *   varjson5_free(r2);
 *   varjson5_free_doc(doc);
 */

/* Opaque handle to a parsed and variable-substituted JSON5 document. */
typedef struct varjson5_doc varjson5_doc;

/*
 * Parse JSON5 input and apply {{vars}} substitution.
 * Returns an opaque document handle on success, NULL on error.
 * Free with varjson5_free_doc() when done.
 */
varjson5_doc* varjson5_load(const char* input);

/*
 * Apply a jq-style filter to an already-loaded document.
 * filter - filter expression; pass NULL for "."
 * flags  - same as varjson5_process
 * Returns a heap-allocated string (free with varjson5_free()), or NULL on error.
 */
char* varjson5_query(varjson5_doc* doc, const char* filter, int flags);

/* Release a document handle created by varjson5_load(). */
void varjson5_free_doc(varjson5_doc* doc);

#ifdef __cplusplus
}
#endif
