#ifndef SOFTLINE_SURFACE_H
#define SOFTLINE_SURFACE_H

#include <stddef.h>

/* A terminal-cell viewport, never a retained response. All coordinates are
 * absolute terminal cells. The surface owns only the visible output rows. */
typedef struct sl_surface sl_surface_t;

sl_surface_t *sl_surface_create(int fd, int x, int y, int width, int height,
                                int (*cell_width)(unsigned long),
                                int (*cluster_width)(const char *, size_t));
/* A parser-only session for redirected output; keeps only ANSI/UTF-8 state. */
sl_surface_t *sl_surface_create_validator(void);
void sl_surface_destroy(sl_surface_t *surface);
/* after_native_scroll means the caller already scrolled the displaced top
 * rows into terminal history; the remaining cells are physically aligned. */
int sl_surface_resize(sl_surface_t *surface, int x, int y, int width,
                      int height, int after_native_scroll);
/* Report the current viewport and the physical terminal height observed when
 * it was created or last resized. */
void sl_surface_geometry(const sl_surface_t *surface, int *x, int *y,
                         int *width, int *height, int *terminal_rows);
/* Returns 0 on success, -1 for output/allocation failure, -2 for invalid
 * input bytes or a viewport too small for the next glyph. */
int sl_surface_write(sl_surface_t *surface, const char *bytes, size_t length);
/* Validate a span without rendering. On failure, accepted is the valid prefix.
 */
int sl_surface_validate(sl_surface_t *surface, const char *bytes, size_t length,
                        size_t *accepted);
/* A partial SGR or UTF-8 sequence keeps the session open until completed. */
int sl_surface_complete(const sl_surface_t *surface);
int sl_surface_matches(const sl_surface_t *surface, int x, int y, int width,
                       int height);
/* Called before and after a visible viewport row is scrolled away. */
void sl_surface_set_scroll_hook(sl_surface_t *surface, int (*hook)(void *, int),
                                void *userdata);

#endif
