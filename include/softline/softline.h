#ifndef SOFTLINE_H
#define SOFTLINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#define SL_INDENT_SPACES 0
#define SL_INDENT_SYMBOL 1

typedef struct sl_t sl_t;

typedef struct sl_config_t {
  int screen_x;
  int screen_y;
  int screen_width;
  int screen_height;
  const char *prompt_symbol;
  int indent_mode;
} sl_config_t;

sl_t *sl_create(void);
sl_t *sl_create_with_config(const sl_config_t *config);
void sl_destroy(sl_t *sl);
char *sl_readline(sl_t *sl, const char *prompt);
void sl_free(void *ptr);
int sl_history_add(sl_t *sl, const char *line);
int sl_history_set_max_len(sl_t *sl, int max_len);
int sl_history_save(sl_t *sl, const char *filename);
int sl_history_load(sl_t *sl, const char *filename);
void sl_clear_screen(sl_t *sl);
int sl_set_bounds(sl_t *sl, int x, int y, int width, int height);
int sl_set_prompt_symbol(sl_t *sl, const char *symbol);
int sl_set_indent_mode(sl_t *sl, int mode);
int sl_on_resize(sl_t *sl, int cols, int rows);
void sl_config_init(sl_config_t *config);

#ifdef __cplusplus
}
#endif

#endif