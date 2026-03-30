#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Flags for varjson5_process */
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

/* Free a string returned by varjson5_process. */
void varjson5_free(char* ptr);

/* Return the last error message for the calling thread.
 * Valid until the next call to varjson5_process on the same thread. */
const char* varjson5_last_error(void);

#ifdef __cplusplus
}
#endif
