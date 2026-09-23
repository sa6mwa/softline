#ifndef SOFTLINE_SURFACE_H
#define SOFTLINE_SURFACE_H

#include <stddef.h>

/* A terminal-cell viewport, never a retained response. All coordinates are
 * absolute terminal cells. The surface owns only the visible output rows. */
typedef struct sl_surface sl_surface_t;

sl_surface_t *sl_surface_create(int fd, int x, int y, int width, int height,
                                int (*cell_width)(unsigned long));
void sl_surface_destroy(sl_surface_t *surface);
int sl_surface_resize(sl_surface_t *surface, int x, int y, int width,
                      int height);
/* Returns 0 on success, -1 for output/allocation failure, -2 for invalid
 * input bytes or a viewport too small for the next glyph. */
int sl_surface_write(sl_surface_t *surface, const char *bytes, size_t length);
/* A partial SGR or UTF-8 sequence keeps the session open until completed. */
int sl_surface_complete(const sl_surface_t *surface);
int sl_surface_matches(const sl_surface_t *surface, int x, int y, int width,
                       int height);

#endif
