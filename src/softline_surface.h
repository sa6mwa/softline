#ifndef SOFTLINE_SURFACE_H
#define SOFTLINE_SURFACE_H
#include <stddef.h>
/* Native terminal output state: cursor, geometry and bounded parser state.
 * Transcript cells are owned exclusively by the terminal. */
typedef struct sl_surface sl_surface_t;
sl_surface_t *sl_surface_create_validator(void);
sl_surface_t *sl_surface_create_native(int fd, int width, int height, int row,
                                       int col,
                                       int (*cell_width)(unsigned long));
int sl_surface_is_native(const sl_surface_t *surface);
int sl_surface_native_prompt_top(const sl_surface_t *surface);
void sl_surface_native_cursor(sl_surface_t *surface, int row, int col);
void sl_surface_native_position(const sl_surface_t *surface, int *row,
                                int *col);
int sl_surface_native_write(sl_surface_t *surface, const char *bytes,
                            size_t length);
void sl_surface_destroy(sl_surface_t *surface);
int sl_surface_resize(sl_surface_t *surface, int width, int height);
void sl_surface_geometry(const sl_surface_t *surface, int *width, int *height,
                         int *terminal_rows);
int sl_surface_native_resize_pending(const sl_surface_t *surface);
/* Validate without rendering. accepted reports the consumed prefix. */
int sl_surface_validate(sl_surface_t *surface, const char *bytes, size_t length,
                        size_t *accepted);
int sl_surface_complete(const sl_surface_t *surface);
void sl_surface_reset_partial(sl_surface_t *surface);
int sl_surface_matches(const sl_surface_t *surface, int width, int height);
#endif
