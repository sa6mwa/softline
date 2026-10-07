#ifndef SOFTLINE_HISTORY_H
#define SOFTLINE_HISTORY_H

#include "softline/softline.h"
#include <stdio.h>

/* Private native persistence; editor policy stays in softline.c. */
typedef struct sl_history_store sl_history_store_t;
int sl_history_store_create(const char *key, const char *directory,
                            size_t line_max, sl_history_store_t **out);
void sl_history_store_destroy(sl_history_store_t *store);
int sl_history_store_load(const char *key, sl_history_emit_t emit,
                          void *context, void *userdata);
int sl_history_store_append(const char *key, const char *prompt,
                            void *userdata);
int sl_history_store_compact(sl_history_store_t *store, int max_entries);
int sl_history_write_record(FILE *fp, const char *line);
/* 1 record, 0 EOF/incomplete native tail, negative status. Legacy load accepts
 * an unterminated last record; native load requires a physical LF commit. */
int sl_history_read_record(FILE *fp, char *line, size_t cap, int terminated);

#endif
