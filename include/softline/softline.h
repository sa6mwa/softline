#ifndef SOFTLINE_H
#define SOFTLINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/** Opaque receiver handle for one independent softline editor instance. */
typedef struct sl sl_t;

/**
 * Idle hook invoked while sl_readline() is active and no input byte is ready.
 *
 * The callback runs synchronously on the thread calling sl_readline(). It may
 * use public receiver methods to inspect or mutate the active buffer, submit,
 * cancel, or print above a bounded prompt. It must not destroy self.
 */
typedef void (*sl_idle_callback_t)(sl_t *self, void *userdata);

/**
 * Chunk producer for sl_print_above().
 *
 * Return SL_OK with a non-empty chunk to continue. Return SL_OK with *len set
 * to 0 to end the stream. The chunk memory only needs to remain valid until
 * the callback is invoked again or sl_print_above() returns.
 */
typedef int (*sl_stream_callback_t)(sl_t *self, void *userdata,
                                    const char **chunk, size_t *len);

/**
 * Decoded key identifiers delivered to key binding callbacks.
 *
 * Printable UTF-8 input is inserted by the built-in editor path rather than
 * reported through these constants. Alt-letter keys not listed directly are
 * represented as SL_KEY_ALT_BASE plus the unsigned byte value of the letter
 * when the terminal sequence is recognized.
 */
typedef enum sl_key {
  /** No key was decoded. */
  SL_KEY_NONE = 0,
  /** Ctrl-C, normally terminal interrupt. */
  SL_KEY_CTRL_C = 3,
  /** Ctrl-D, EOF on an empty buffer or delete-forward in text. */
  SL_KEY_CTRL_D = 4,
  /** Ctrl-A, move to beginning of buffer. */
  SL_KEY_CTRL_A = 1,
  /** Ctrl-B, move backward by one UTF-8 cluster. */
  SL_KEY_CTRL_B = 2,
  /** Ctrl-E, move to end of buffer. */
  SL_KEY_CTRL_E = 5,
  /** Ctrl-F, move forward by one UTF-8 cluster. */
  SL_KEY_CTRL_F = 6,
  /** Tab key. */
  SL_KEY_TAB = 9,
  /** Ctrl-J, insert a literal newline. */
  SL_KEY_CTRL_J = 10,
  /** Ctrl-K, delete from cursor to end of buffer. */
  SL_KEY_CTRL_K = 11,
  /** Enter or carriage return. */
  SL_KEY_ENTER = 13,
  /** Ctrl-R, reverse incremental history search unless rebound. */
  SL_KEY_CTRL_R = 18,
  /** Ctrl-U, delete from beginning of buffer to cursor. */
  SL_KEY_CTRL_U = 21,
  /** Ctrl-W, delete the previous word. */
  SL_KEY_CTRL_W = 23,
  /** Escape key. */
  SL_KEY_ESCAPE = 27,
  /** Backspace key. */
  SL_KEY_BACKSPACE = 127,
  /** Up arrow. */
  SL_KEY_UP = 1000,
  /** Down arrow. */
  SL_KEY_DOWN = 1001,
  /** Left arrow. */
  SL_KEY_LEFT = 1002,
  /** Right arrow. */
  SL_KEY_RIGHT = 1003,
  /** Home key. */
  SL_KEY_HOME = 1004,
  /** End key. */
  SL_KEY_END = 1005,
  /** Delete key. */
  SL_KEY_DELETE = 1006,
  /** Alt-B, move backward by word. */
  SL_KEY_ALT_B = 1007,
  /** Alt-F, move forward by word. */
  SL_KEY_ALT_F = 1008,
  /** Escape sequence was recognized as unsupported or incomplete. */
  SL_KEY_UNKNOWN = 1009,
  /** Bracketed paste begin sequence. */
  SL_KEY_PASTE_BEGIN = 1010,
  /** Bracketed paste end sequence. */
  SL_KEY_PASTE_END = 1011,
  /** Function key F1. */
  SL_KEY_F1 = 1012,
  /** Function key F2. */
  SL_KEY_F2 = 1013,
  /** Function key F3. */
  SL_KEY_F3 = 1014,
  /** Function key F4. */
  SL_KEY_F4 = 1015,
  /** Function key F5. */
  SL_KEY_F5 = 1016,
  /** Function key F6. */
  SL_KEY_F6 = 1017,
  /** Function key F7. */
  SL_KEY_F7 = 1018,
  /** Function key F8. */
  SL_KEY_F8 = 1019,
  /** Function key F9. */
  SL_KEY_F9 = 1020,
  /** Function key F10. */
  SL_KEY_F10 = 1021,
  /** Ctrl-Enter when a terminal sends a distinguishable sequence. */
  SL_KEY_CTRL_ENTER = 1022,
  /** Base value for Alt-letter bindings not listed as dedicated constants. */
  SL_KEY_ALT_BASE = 4096,
  /** Alt-M key. */
  SL_KEY_ALT_M = 4205
} sl_key_t;

/**
 * Action requested by a key binding callback.
 *
 * A callback stores one of these values in its action out-parameter before
 * returning SL_OK. Returning a negative sl_status_t fails the active operation
 * regardless of the action value.
 */
typedef enum sl_key_action {
  /** Let softline run the built-in behavior for the key. */
  SL_KEY_ACTION_PASS = 0,
  /** Treat the key as fully handled by the callback. */
  SL_KEY_ACTION_HANDLED = 1,
  /** Submit the current buffer. */
  SL_KEY_ACTION_SUBMIT = 2,
  /** Cancel the active readline() call. */
  SL_KEY_ACTION_CANCEL = 3,
  /** Restore terminal state and raise SIGINT. */
  SL_KEY_ACTION_INTERRUPT = 4
} sl_key_action_t;

/**
 * Per-handle key binding callback.
 *
 * The callback may inspect or mutate the active buffer through the public
 * receiver methods. It must store the requested outcome in *action and return
 * SL_OK, or return a negative sl_status_t to fail the active operation.
 */
typedef int (*sl_key_callback_t)(sl_t *self, sl_key_t key, void *userdata,
                                 sl_key_action_t *action);

/**
 * Status codes returned by fallible softline operations.
 *
 * Public methods return SL_OK on success or one of the negative values on
 * failure. When a handle is available, sl_last_error() may contain additional
 * handle-owned diagnostic text after a failure.
 */
typedef enum sl_status {
  /** Operation completed successfully. */
  SL_OK = 0,
  /** Unclassified failure. */
  SL_ERROR = -1,
  /** Invalid argument or invalid configuration. */
  SL_ERROR_INVALID = -2,
  /** Memory allocation failed. */
  SL_ERROR_NOMEM = -3,
  /** Terminal, file, or descriptor I/O failed. */
  SL_ERROR_IO = -4
} sl_status_t;

/** Classification of the most recent sl_readline() result. */
typedef enum sl_readline_status {
  /** No readline() call has completed on this handle. */
  SL_READLINE_NONE = 0,
  /** readline() returned submitted text owned by the caller. */
  SL_READLINE_SUBMITTED = 1,
  /** readline() returned NULL because input ended without submitted text. */
  SL_READLINE_EOF = 2,
  /** readline() returned NULL because editing was cancelled. */
  SL_READLINE_CANCELLED = 3,
  /** readline() restored terminal state and raised SIGINT. */
  SL_READLINE_INTERRUPTED = 4,
  /** readline() returned NULL because an I/O, allocation, or render error
     occurred. */
  SL_READLINE_ERROR = 5
} sl_readline_status_t;

/** How a text value returned by next_prompt() entered the application. */
typedef enum sl_prompt_source {
  /** No prompt text was returned. */
  SL_PROMPT_SOURCE_NONE = 0,
  /** The user submitted the active editor with Enter. */
  SL_PROMPT_SOURCE_DIRECT = 1,
  /** The user previously queued the text with Tab. */
  SL_PROMPT_SOURCE_QUEUED = 2
} sl_prompt_source_t;

/** Renderer-owned visual treatment for an interactive prompt UI. */
typedef enum sl_prompt_theme {
  /** Compact, uncoloured prompt UI. */
  SL_PROMPT_THEME_PLAIN = 0,
  /** A restrained cyan accent treatment. */
  SL_PROMPT_THEME_ACCENT = 1,
  /** The Dracula true-colour palette. */
  SL_PROMPT_THEME_DRACULA = 2,
  /** The Gruvbox true-colour palette. */
  SL_PROMPT_THEME_GRUVBOX = 3,
  /** An amber monochrome CRT palette. */
  SL_PROMPT_THEME_MONOCHROME = 4,
  /** A green monochrome terminal palette. */
  SL_PROMPT_THEME_MONOGREEN = 5,
  /** The Outrun true-colour palette. */
  SL_PROMPT_THEME_OUTRUN = 6,
  /** The Riced true-colour palette. */
  SL_PROMPT_THEME_RICED = 7,
  /** The Synthwave true-colour palette. */
  SL_PROMPT_THEME_SYNTHWAVE = 8,
  /** The standard 16-colour ANSI prompt palette. */
  SL_PROMPT_THEME_DEFAULT = 9
} sl_prompt_theme_t;

/** Maximum number of retained status-line elements. */
#define SL_STATUS_MAX_ELEMENTS 32

/**
 * Editor configuration initialized by sl_config_init().
 *
 * Call sl_config_init() for documented defaults before overriding fields.
 * Passing an all-zero struct to sl_create_with_config() is invalid because
 * line_max_len must be non-zero.
 */
typedef struct sl_config {
  /** Input file descriptor; default is STDIN_FILENO. */
  int input_fd;
  /** Output file descriptor; default is STDOUT_FILENO. */
  int output_fd;
  /** Left edge of bounded prompt area, in terminal cells. */
  int screen_x;
  /** Top edge of bounded prompt area, in terminal cells. */
  int screen_y;
  /** Width of bounded prompt area; zero means dynamic terminal width. */
  int screen_width;
  /** Height of bounded prompt area; zero means dynamic bottom-prompt mode. */
  int screen_height;
  /** Non-zero enables bounded prompt rendering. */
  int bounded;
  /** Maximum retained history entries; default is 100. */
  int history_max_len;
  /** Maximum editable line length in bytes; default is 4096. */
  size_t line_max_len;
  /** Non-zero enables Tab queueing in interactive prompt mode. */
  int prompt_queue;
  /** Maximum queued prompts; default is 64 when queueing is enabled. */
  int prompt_queue_max_entries;
  /** Maximum FIFO previews shown above the active editor; default is 3. */
  int prompt_queue_preview_entries;
  /** Built-in visual treatment for interactive prompt UI. */
  sl_prompt_theme_t prompt_theme;
  /** Non-zero renders the optional status line between queue previews and the
   * editor. */
  int statusline;
  /** Palette index assigned to the first status-line element. */
  size_t statusline_start_element;
  /** Non-zero animates /-\\| while the status line is busy. */
  int status_spinner;
  /** Non-zero selects the busy status marker. */
  int status_busy;
  /** Printable ASCII marker shown while idle; defaults to '+'. '\0' leaves
   * its reserved status marker slot blank. */
  char status_idle_marker;
} sl_config_t;

/**
 * Public receiver shell for one editor instance.
 *
 * Method fields are initialized by sl_create() and sl_create_with_config() and
 * mirror the sl_* wrapper functions. The impl field is private implementation
 * state and is exposed only to keep the receiver shell ABI stable; callers must
 * not read, write, copy, or free it.
 */
struct sl {
  /**
   * Read one submitted line.
   *
   * Returns a softline-allocated string on submitted input, or NULL for EOF,
   * cancellation, interrupt, or error. Use last_readline_status() before the
   * next readline() call to classify a NULL return.
   */
  char *(*readline)(sl_t *self, const char *prompt);
  /** Destroy the handle; NULL-safe through sl_destroy(). */
  void (*destroy)(sl_t *self);
  /** Release strings returned by softline with the handle's allocator; NULL is
   * ignored. */
  void (*free_string)(sl_t *self, char *ptr);
  /** Add one non-NULL entry to this handle's in-memory history. */
  int (*history_add)(sl_t *self, const char *line);
  /** Set the maximum retained history length; zero clears and disables history.
   */
  int (*history_set_max_len)(sl_t *self, int max_len);
  /** Save this handle's history to a private owner-only history file. */
  int (*history_save)(sl_t *self, const char *filename);
  /** Load history entries from a history file into this handle's current
   * history. */
  int (*history_load)(sl_t *self, const char *filename);
  /** Configure bounded prompt geometry; width or height zero uses dynamic
   * terminal bounds. */
  int (*set_bounds)(sl_t *self, int x, int y, int width, int height);
  /** Set normal prompt wrapping width for non-bounded rendering. */
  int (*set_screen_width)(sl_t *self, int width);
  /** Register or clear the per-handle idle callback. */
  int (*set_idle_callback)(sl_t *self, sl_idle_callback_t callback,
                           void *userdata);
  /** Bind one decoded key to a per-handle callback. */
  int (*bind_key)(sl_t *self, sl_key_t key, sl_key_callback_t callback,
                  void *userdata);
  /** Insert text bytes at the active cursor position. UTF-8 text is kept
   * cluster-safe. */
  int (*insert)(sl_t *self, const char *text);
  /** Replace the active buffer with text bytes and move the cursor to the end.
   */
  int (*set_buffer)(sl_t *self, const char *text);
  /** Return the active buffer as a handle-owned NUL-terminated string. */
  const char *(*buffer)(const sl_t *self);
  /** Return the active cursor position as a byte offset into buffer(). */
  size_t (*cursor)(const sl_t *self);
  /** Move the active cursor to a byte offset, clamped to a UTF-8 cluster
   * boundary. */
  int (*set_cursor)(sl_t *self, size_t cursor);
  /** Submit the active buffer from a callback or idle hook. */
  int (*submit)(sl_t *self);
  /** Cancel the active readline() call from a callback or idle hook. */
  int (*cancel)(sl_t *self);
  /** Stream output through the area above the active bounded prompt. */
  int (*print_above)(sl_t *self, sl_stream_callback_t callback, void *userdata);
  /** Return the most recent readline() status for this handle. */
  sl_readline_status_t (*last_readline_status)(const sl_t *self);
  /** Return the most recent handle-owned diagnostic string, or NULL. */
  const char *(*last_error)(const sl_t *self);
  /** Private implementation pointer; callers must not read or modify it. */
  void *impl;
  /** Return the next queued prompt FIFO, or read a direct prompt when empty. */
  char *(*next_prompt)(sl_t *self, const char *prompt,
                       sl_prompt_source_t *source);
  /** Enable/configure Tab queueing; reducing capacity below queued work
   * fails. */
  int (*set_prompt_queue)(sl_t *self, int enabled, int max_entries,
                          int preview_entries);
  /** Select one of the built-in interactive prompt themes. */
  int (*set_prompt_theme)(sl_t *self, sl_prompt_theme_t theme);
  /** Enable or disable the status line and select its first palette index. */
  int (*set_statusline)(sl_t *self, int enabled, size_t starting_element);
  /** Replace status-line elements; inputs beyond 32 are rendered with a final
   * ellipsis element. */
  int (*set_status_elements)(sl_t *self, const char *const *elements,
                             size_t count);
  /** Set, replace, or clear one retained status-line element. */
  int (*set_status_element)(sl_t *self, size_t index, const char *element);
  /** Set busy state; busy renders x (or a spinner), while idle uses the
   * configured marker, which defaults to green +. */
  int (*set_status_busy)(sl_t *self, int busy);
  /** Enable or disable the 500ms /-\\| busy spinner. */
  int (*set_status_spinner)(sl_t *self, int enabled);
  /** Set the printable ASCII idle marker, or '\0' for a blank reserved slot. */
  int (*set_status_idle_marker)(sl_t *self, char marker);
};

/**
 * Fill config with default file descriptors, bounds, history, and line limits.
 *
 * Passing NULL is ignored. Defaults are stdin/stdout file descriptors,
 * unbounded rendering, 100 history entries, and a 4096-byte editable line.
 */
void sl_config_init(sl_config_t *config);

/**
 * Create a handle with default configuration.
 *
 * Returns a new receiver shell on success, or NULL on allocation failure. The
 * caller owns the handle and must release it with sl_destroy() or destroy().
 */
sl_t *sl_create(void);

/**
 * Create a handle from config.
 *
 * Passing NULL uses the same defaults as sl_create(). On success the caller
 * owns the returned handle and must release it with sl_destroy() or destroy().
 */
sl_t *sl_create_with_config(const sl_config_t *config);

/**
 * readline() returns a project-allocated string on submitted input and NULL for
 * EOF, cancellation, interrupt, or error. Call sl_last_readline_status() before
 * the next readline() call to classify NULL precisely.
 */
char *sl_readline(sl_t *self, const char *prompt);

/**
 * Return the next application prompt. Queued prompts are returned FIFO without
 * entering the terminal editor and set *source to SL_PROMPT_SOURCE_QUEUED.
 * With no queued prompt this behaves as sl_readline() and sets *source to
 * SL_PROMPT_SOURCE_DIRECT when text is submitted. source may be NULL.
 */
char *sl_next_prompt(sl_t *self, const char *prompt,
                     sl_prompt_source_t *source);

/**
 * Destroy a handle and restore terminal state owned by it.
 *
 * This function is NULL-safe. Do not use self or any handle-owned buffer after
 * destruction.
 */
void sl_destroy(sl_t *self);

/** Release a string returned by softline; NULL ptr is ignored. */
void sl_free_string(sl_t *self, char *ptr);

/** Add one non-NULL entry to this handle's in-memory history. */
int sl_history_add(sl_t *self, const char *line);

/** Set the maximum retained history length; zero clears and disables history.
 */
int sl_history_set_max_len(sl_t *self, int max_len);

/** Save history to filename using owner-only file permissions. */
int sl_history_save(sl_t *self, const char *filename);

/** Load history entries from filename into existing history, dropping old
 * entries if capped. */
int sl_history_load(sl_t *self, const char *filename);

/** Enable bounded prompt rendering at x,y,width,height; zero width or height
 * uses dynamic bounds. */
int sl_set_bounds(sl_t *self, int x, int y, int width, int height);

/** Set normal prompt wrapping width; zero returns to terminal-width probing. */
int sl_set_screen_width(sl_t *self, int width);

/** Enable/configure Tab queueing; disabling clears the queue. Reducing the
 * capacity below the current queue length returns SL_ERROR_INVALID. */
int sl_set_prompt_queue(sl_t *self, int enabled, int max_entries,
                        int preview_entries);

/** Select a built-in interactive prompt theme. */
int sl_set_prompt_theme(sl_t *self, sl_prompt_theme_t theme);

/** Enable or disable the status line and choose the palette index used for
 * its first element. Status elements wrap between elements where possible. */
int sl_set_statusline(sl_t *self, int enabled, size_t starting_element);

/** Replace all status-line elements. At most 32 elements are retained; longer
 * input is represented by the first 31 elements followed by `...`. */
int sl_set_status_elements(sl_t *self, const char *const *elements,
                           size_t count);

/** Set, replace, or clear one status-line element. index must be below
 * SL_STATUS_MAX_ELEMENTS. */
int sl_set_status_element(sl_t *self, size_t index, const char *element);

/** Set the status-line busy state. With the spinner disabled, busy renders x;
 * idle renders the configured marker or a blank reserved slot by default. */
int sl_set_status_busy(sl_t *self, int busy);

/** Enable or disable the 500ms /-\\| spinner used while status is busy. */
int sl_set_status_spinner(sl_t *self, int enabled);

/** Set a printable ASCII idle marker, or '\0' for a blank reserved slot. */
int sl_set_status_idle_marker(sl_t *self, char marker);

/** Register or clear an idle callback for this handle. */
int sl_set_idle_callback(sl_t *self, sl_idle_callback_t callback,
                         void *userdata);

/** Bind key to callback for this handle; NULL callback removes the binding. */
int sl_bind_key(sl_t *self, sl_key_t key, sl_key_callback_t callback,
                void *userdata);

/** Insert text bytes at the active cursor position during readline(),
 * callbacks, or tests. */
int sl_insert(sl_t *self, const char *text);

/** Replace the active buffer with text bytes and move the cursor to the end. */
int sl_set_buffer(sl_t *self, const char *text);

/** Return the active buffer, owned by the handle until it is changed or
 * destroyed. */
const char *sl_buffer(const sl_t *self);

/** Return the active cursor position as a byte offset into sl_buffer(). */
size_t sl_cursor(const sl_t *self);

/** Set cursor to a byte offset; invalid offsets are clamped to valid text
 * boundaries. */
int sl_set_cursor(sl_t *self, size_t cursor);

/** Submit the active readline() buffer from inside a callback. */
int sl_submit(sl_t *self);

/** Cancel the active readline() operation from inside a callback. */
int sl_cancel(sl_t *self);

/** Write callback-produced chunks above the active bounded prompt. */
int sl_print_above(sl_t *self, sl_stream_callback_t callback, void *userdata);

/** Return the status of the most recent readline() call on this handle. */
sl_readline_status_t sl_last_readline_status(const sl_t *self);

/** Return the most recent diagnostic string owned by the handle, or NULL. */
const char *sl_last_error(const sl_t *self);

#ifdef __cplusplus
}
#endif

#endif
