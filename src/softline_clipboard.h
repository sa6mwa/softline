#ifndef SOFTLINE_CLIPBOARD_H
#define SOFTLINE_CLIPBOARD_H

#include <stddef.h>

/* On success, the caller owns *path. The image file persists for the host
 * application to consume after the editor closes. */
int sl_clipboard_save_image(const char *path_template, char **path, char *error,
                            size_t error_size);
void sl_clipboard_discard_image(const char *path, const char *path_template);
int sl_clipboard_valid_path_template(const char *path_template);

#endif
