#ifndef SOFTLINE_SURFACE_H
#define SOFTLINE_SURFACE_H

#include <stddef.h>

/* A terminal-cell viewport, never a retained response. All coordinates are
 * absolute terminal cells. The surface owns only the visible output rows. */
typedef struct sl_surface sl_surface_t;

sl_surface_t *sl_surface_create(int fd, int x, int y, int width, int height,
                                int initial_col,
                                int (*cell_width)(unsigned long),
                                int (*cluster_width)(const char *, size_t));
/* A parser-only session for redirected output; keeps only ANSI/UTF-8 state. */
sl_surface_t *sl_surface_create_validator(void);
/* Native full-width output owns no transcript cells. Payload bytes are
 * forwarded verbatim inside the rows above the prompt. */
sl_surface_t *sl_surface_create_native(int fd, int width, int height, int row,
                                       int col,
                                       int (*cell_width)(unsigned long));
int sl_surface_is_native(const sl_surface_t *surface);
int sl_surface_native_prompt_top(const sl_surface_t *surface);
void sl_surface_native_cursor(sl_surface_t *surface, int row, int col);
void sl_surface_native_position(const sl_surface_t *surface, int *row,
                                int *col);
int sl_surface_native_detach(sl_surface_t *surface);
int sl_surface_native_write(sl_surface_t *surface, const char *bytes,
                            size_t length);
void sl_surface_destroy(sl_surface_t *surface);
/* after_native_scroll means the caller already scrolled the displaced top
 * rows into terminal history; the remaining cells are physically aligned.
 * native_layout identifies layouts whose physical resize is terminal-owned. */
int sl_surface_resize(sl_surface_t *surface, int x, int y, int width,
                      int height, int after_native_scroll, int native_layout);
/* Report the current viewport and the physical terminal height observed when
 * it was created or last resized. */
void sl_surface_geometry(const sl_surface_t *surface, int *x, int *y,
                         int *width, int *height, int *terminal_rows);
/* Native terminal geometry changed since the last viewport reconciliation. */
int sl_surface_native_resize_pending(const sl_surface_t *surface);
/* Returns 0 on success, -1 for output/allocation failure, -2 for invalid
 * input bytes or a viewport too small for the next glyph. accepted reports
 * the prefix consumed before a failure, including any partial parser bytes. */
int sl_surface_write(sl_surface_t *surface, const char *bytes, size_t length,
                     size_t *accepted);
/* Validate a span without rendering. On failure, accepted is the valid prefix.
 */
int sl_surface_validate(sl_surface_t *surface, const char *bytes, size_t length,
                        size_t *accepted);
/* A partial SGR or UTF-8 sequence keeps the session open until completed. */
int sl_surface_complete(const sl_surface_t *surface);
/* Drop a partial sequence after a finite print fails; keep completed style. */
void sl_surface_reset_partial(sl_surface_t *surface);
/* Separate the next output producer from a completed session without moving
 * visible cells until it writes. Reset parser/style, then advance past a
 * nonempty final row on that producer's first write. */
void sl_surface_mark_boundary(sl_surface_t *surface);
/* Visible empty rows at the viewport bottom, capped at limit. */
int sl_surface_trailing_blank_rows(const sl_surface_t *surface, int limit);
/* Whether the pending producer boundary will advance an occupied last row. */
int sl_surface_boundary_will_scroll(const sl_surface_t *surface);
int sl_surface_matches(const sl_surface_t *surface, int x, int y, int width,
                       int height);

#endif
