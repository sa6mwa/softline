#include "softline/softline.h"

#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>

#include <stdlib.h>
#include <string.h>

#define SOFTLINE_LUA_HANDLE "softline.handle"
#define SOFTLINE_LUA_MAX_KEY_BINDINGS 64

typedef struct softline_lua_handle {
  sl_t *sl;
  lua_State *L;
  struct {
    sl_key_t key;
    int ref;
  } key_bindings[SOFTLINE_LUA_MAX_KEY_BINDINGS];
} softline_lua_handle_t;

typedef struct softline_lua_stream {
  lua_State *L;
  int ref;
  int kind;
  int index;
  int base_top;
} softline_lua_stream_t;

static softline_lua_handle_t *softline_lua_check(lua_State *L, int index) {
  softline_lua_handle_t *handle;
  handle =
      (softline_lua_handle_t *)luaL_checkudata(L, index, SOFTLINE_LUA_HANDLE);
  luaL_argcheck(L, handle != NULL && handle->sl != NULL, index,
                "closed softline handle");
  return handle;
}

static int softline_lua_status(lua_State *L, int status) {
  if (status == SL_OK) {
    lua_pushboolean(L, 1);
    return 1;
  }
  lua_pushnil(L);
  lua_pushinteger(L, status);
  return 2;
}

static int softline_lua_find_key_ref(softline_lua_handle_t *handle,
                                     sl_key_t key) {
  int i;
  for (i = 0; i < SOFTLINE_LUA_MAX_KEY_BINDINGS; i++) {
    if (handle->key_bindings[i].ref != LUA_NOREF &&
        handle->key_bindings[i].key == key)
      return i;
  }
  return -1;
}

static int softline_lua_find_empty_key_ref(softline_lua_handle_t *handle) {
  int i;
  for (i = 0; i < SOFTLINE_LUA_MAX_KEY_BINDINGS; i++) {
    if (handle->key_bindings[i].ref == LUA_NOREF)
      return i;
  }
  return -1;
}

static int softline_lua_key_callback(sl_t *sl, sl_key_t key, void *userdata,
                                     sl_key_action_t *action) {
  softline_lua_handle_t *handle;
  lua_State *L;
  int slot;
  int result;
  (void)sl;
  handle = (softline_lua_handle_t *)userdata;
  if (!handle || !handle->L || !action)
    return SL_ERROR_INVALID;
  L = handle->L;
  slot = softline_lua_find_key_ref(handle, key);
  if (slot < 0)
    return SL_ERROR_INVALID;
  lua_rawgeti(L, LUA_REGISTRYINDEX, handle->key_bindings[slot].ref);
  lua_pushinteger(L, key);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
    lua_pop(L, 1);
    return SL_ERROR;
  }
  if (lua_isnil(L, -1)) {
    *action = SL_KEY_ACTION_HANDLED;
    lua_pop(L, 1);
    return SL_OK;
  }
  if (!lua_isinteger(L, -1)) {
    lua_pop(L, 1);
    return SL_ERROR_INVALID;
  }
  result = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  switch (result) {
  case SL_KEY_ACTION_PASS:
  case SL_KEY_ACTION_HANDLED:
  case SL_KEY_ACTION_SUBMIT:
  case SL_KEY_ACTION_CANCEL:
  case SL_KEY_ACTION_INTERRUPT:
    *action = (sl_key_action_t)result;
    return SL_OK;
  default:
    return SL_ERROR_INVALID;
  }
}

static void softline_lua_config(lua_State *L, int index, sl_config_t *config) {
  if (lua_isnoneornil(L, index))
    return;
  luaL_checktype(L, index, LUA_TTABLE);

  lua_getfield(L, index, "input_fd");
  if (!lua_isnil(L, -1))
    config->input_fd = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "output_fd");
  if (!lua_isnil(L, -1))
    config->output_fd = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "screen_x");
  if (!lua_isnil(L, -1))
    config->screen_x = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "screen_y");
  if (!lua_isnil(L, -1))
    config->screen_y = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "screen_width");
  if (!lua_isnil(L, -1))
    config->screen_width = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "screen_height");
  if (!lua_isnil(L, -1))
    config->screen_height = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "bounded");
  if (!lua_isnil(L, -1))
    config->bounded = lua_toboolean(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, index, "prompt_queue");
  if (!lua_isnil(L, -1))
    config->prompt_queue = lua_toboolean(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, index, "prompt_queue_max_entries");
  if (!lua_isnil(L, -1))
    config->prompt_queue_max_entries = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, index, "prompt_queue_preview_entries");
  if (!lua_isnil(L, -1))
    config->prompt_queue_preview_entries = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
  lua_getfield(L, index, "prompt_theme");
  if (!lua_isnil(L, -1))
    config->prompt_theme = (sl_prompt_theme_t)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "history_max_len");
  if (!lua_isnil(L, -1))
    config->history_max_len = (int)luaL_checkinteger(L, -1);
  lua_pop(L, 1);

  lua_getfield(L, index, "line_max_len");
  if (!lua_isnil(L, -1))
    config->line_max_len = (size_t)luaL_checkinteger(L, -1);
  lua_pop(L, 1);
}

static int softline_lua_new(lua_State *L) {
  sl_config_t config;
  softline_lua_handle_t *handle;
  int i;
  sl_config_init(&config);
  softline_lua_config(L, 1, &config);
  handle = (softline_lua_handle_t *)lua_newuserdatauv(L, sizeof(*handle), 0);
  handle->L = L;
  for (i = 0; i < SOFTLINE_LUA_MAX_KEY_BINDINGS; i++) {
    handle->key_bindings[i].key = SL_KEY_NONE;
    handle->key_bindings[i].ref = LUA_NOREF;
  }
  handle->sl = sl_create_with_config(&config);
  if (!handle->sl)
    return luaL_error(L, "failed to create softline handle");
  luaL_getmetatable(L, SOFTLINE_LUA_HANDLE);
  lua_setmetatable(L, -2);
  return 1;
}

static int softline_lua_gc(lua_State *L) {
  softline_lua_handle_t *handle;
  int i;
  handle = (softline_lua_handle_t *)luaL_checkudata(L, 1, SOFTLINE_LUA_HANDLE);
  for (i = 0; i < SOFTLINE_LUA_MAX_KEY_BINDINGS; i++) {
    if (handle->key_bindings[i].ref != LUA_NOREF) {
      luaL_unref(L, LUA_REGISTRYINDEX, handle->key_bindings[i].ref);
      handle->key_bindings[i].ref = LUA_NOREF;
    }
  }
  if (handle->sl) {
    sl_destroy(handle->sl);
    handle->sl = NULL;
  }
  return 0;
}

static int softline_lua_close(lua_State *L) { return softline_lua_gc(L); }

static int softline_lua_readline(lua_State *L) {
  softline_lua_handle_t *handle;
  const char *prompt;
  char *line;
  handle = softline_lua_check(L, 1);
  prompt = luaL_optstring(L, 2, NULL);
  line = sl_readline(handle->sl, prompt);
  if (!line) {
    lua_pushnil(L);
    lua_pushinteger(L, sl_last_readline_status(handle->sl));
    return 2;
  }
  lua_pushstring(L, line);
  sl_free_string(handle->sl, line);
  return 1;
}

static int softline_lua_next_prompt(lua_State *L) {
  softline_lua_handle_t *handle;
  const char *prompt;
  char *line;
  sl_prompt_source_t source;
  handle = softline_lua_check(L, 1);
  prompt = luaL_optstring(L, 2, NULL);
  source = SL_PROMPT_SOURCE_NONE;
  line = sl_next_prompt(handle->sl, prompt, &source);
  if (!line) {
    lua_pushnil(L);
    lua_pushinteger(L, sl_last_readline_status(handle->sl));
    return 2;
  }
  lua_pushstring(L, line);
  lua_pushinteger(L, source);
  sl_free_string(handle->sl, line);
  return 2;
}

static int softline_lua_history_add(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_history_add(handle->sl, luaL_checkstring(L, 2)));
}

static int softline_lua_history_set_max_len(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_history_set_max_len(handle->sl, (int)luaL_checkinteger(L, 2)));
}

static int softline_lua_history_save(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_history_save(handle->sl, luaL_checkstring(L, 2)));
}

static int softline_lua_history_load(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_history_load(handle->sl, luaL_checkstring(L, 2)));
}

static int softline_lua_set_bounds(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(L, sl_set_bounds(handle->sl,
                                              (int)luaL_checkinteger(L, 2),
                                              (int)luaL_checkinteger(L, 3),
                                              (int)luaL_checkinteger(L, 4),
                                              (int)luaL_checkinteger(L, 5)));
}

static int softline_lua_set_screen_width(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_set_screen_width(handle->sl, (int)luaL_checkinteger(L, 2)));
}

static int softline_lua_set_prompt_queue(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_set_prompt_queue(handle->sl, lua_toboolean(L, 2),
                             (int)luaL_checkinteger(L, 3),
                             (int)luaL_checkinteger(L, 4)));
}

static int softline_lua_set_prompt_theme(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_set_prompt_theme(handle->sl,
                             (sl_prompt_theme_t)luaL_checkinteger(L, 2)));
}

static int softline_lua_insert(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(L, sl_insert(handle->sl, luaL_checkstring(L, 2)));
}

static int softline_lua_set_buffer(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(L,
                             sl_set_buffer(handle->sl, luaL_checkstring(L, 2)));
}

static int softline_lua_buffer(lua_State *L) {
  softline_lua_handle_t *handle;
  const char *buffer;
  handle = softline_lua_check(L, 1);
  buffer = sl_buffer(handle->sl);
  lua_pushstring(L, buffer ? buffer : "");
  return 1;
}

static int softline_lua_cursor(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  lua_pushinteger(L, (lua_Integer)sl_cursor(handle->sl));
  return 1;
}

static int softline_lua_set_cursor(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(
      L, sl_set_cursor(handle->sl, (size_t)luaL_checkinteger(L, 2)));
}

static int softline_lua_submit(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(L, sl_submit(handle->sl));
}

static int softline_lua_cancel(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  return softline_lua_status(L, sl_cancel(handle->sl));
}

static int softline_lua_bind_key(lua_State *L) {
  softline_lua_handle_t *handle;
  sl_key_t key;
  int slot;
  int status;
  handle = softline_lua_check(L, 1);
  key = (sl_key_t)luaL_checkinteger(L, 2);
  slot = softline_lua_find_key_ref(handle, key);
  if (lua_isnoneornil(L, 3)) {
    if (slot >= 0) {
      luaL_unref(L, LUA_REGISTRYINDEX, handle->key_bindings[slot].ref);
      handle->key_bindings[slot].ref = LUA_NOREF;
    }
    return softline_lua_status(L, sl_bind_key(handle->sl, key, NULL, NULL));
  }
  luaL_checktype(L, 3, LUA_TFUNCTION);
  if (slot < 0) {
    slot = softline_lua_find_empty_key_ref(handle);
    if (slot < 0)
      return softline_lua_status(L, SL_ERROR_NOMEM);
  } else {
    luaL_unref(L, LUA_REGISTRYINDEX, handle->key_bindings[slot].ref);
  }
  lua_pushvalue(L, 3);
  handle->key_bindings[slot].key = key;
  handle->key_bindings[slot].ref = luaL_ref(L, LUA_REGISTRYINDEX);
  status = sl_bind_key(handle->sl, key, softline_lua_key_callback, handle);
  if (status != SL_OK) {
    luaL_unref(L, LUA_REGISTRYINDEX, handle->key_bindings[slot].ref);
    handle->key_bindings[slot].ref = LUA_NOREF;
  }
  return softline_lua_status(L, status);
}

static int softline_lua_last_readline_status(lua_State *L) {
  softline_lua_handle_t *handle;
  handle = softline_lua_check(L, 1);
  lua_pushinteger(L, sl_last_readline_status(handle->sl));
  return 1;
}

static int softline_lua_last_error(lua_State *L) {
  softline_lua_handle_t *handle;
  const char *error;
  handle = softline_lua_check(L, 1);
  error = sl_last_error(handle->sl);
  if (!error) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushstring(L, error);
  return 1;
}

static int softline_lua_next_chunk(sl_t *sl, void *userdata, const char **chunk,
                                   size_t *len) {
  softline_lua_stream_t *stream;
  lua_State *L;
  (void)sl;
  stream = (softline_lua_stream_t *)userdata;
  L = stream->L;
  lua_settop(L, stream->base_top);
  *chunk = NULL;
  *len = 0;
  if (stream->kind == LUA_TSTRING) {
    if (stream->index > 0)
      return SL_OK;
    lua_rawgeti(L, LUA_REGISTRYINDEX, stream->ref);
    *chunk = lua_tolstring(L, -1, len);
    stream->index++;
    return SL_OK;
  }
  if (stream->kind == LUA_TTABLE) {
    stream->index++;
    lua_rawgeti(L, LUA_REGISTRYINDEX, stream->ref);
    lua_rawgeti(L, -1, stream->index);
    if (lua_isnil(L, -1))
      return SL_OK;
    if (!lua_isstring(L, -1))
      return SL_ERROR_INVALID;
    *chunk = lua_tolstring(L, -1, len);
    return SL_OK;
  }
  lua_rawgeti(L, LUA_REGISTRYINDEX, stream->ref);
  lua_pushinteger(L, stream->index + 1);
  if (lua_pcall(L, 1, 1, 0) != LUA_OK)
    return SL_ERROR;
  stream->index++;
  if (lua_isnil(L, -1))
    return SL_OK;
  if (!lua_isstring(L, -1))
    return SL_ERROR_INVALID;
  *chunk = lua_tolstring(L, -1, len);
  return SL_OK;
}

static int softline_lua_print_above(lua_State *L) {
  softline_lua_handle_t *handle;
  softline_lua_stream_t stream;
  int status;
  handle = softline_lua_check(L, 1);
  luaL_checkany(L, 2);
  stream.L = L;
  stream.kind = lua_type(L, 2);
  stream.index = 0;
  stream.base_top = lua_gettop(L);
  if (stream.kind != LUA_TSTRING && stream.kind != LUA_TTABLE &&
      stream.kind != LUA_TFUNCTION)
    return luaL_error(L, "print_above expects string, table, or function");
  lua_pushvalue(L, 2);
  stream.ref = luaL_ref(L, LUA_REGISTRYINDEX);
  status = sl_print_above(handle->sl, softline_lua_next_chunk, &stream);
  luaL_unref(L, LUA_REGISTRYINDEX, stream.ref);
  if (status == SL_ERROR && lua_isstring(L, -1)) {
    lua_pushnil(L);
    lua_pushinteger(L, status);
    lua_pushvalue(L, -3);
    return 3;
  }
  return softline_lua_status(L, status);
}

static const luaL_Reg softline_lua_methods[] = {
    {"readline", softline_lua_readline},
    {"next_prompt", softline_lua_next_prompt},
    {"history_add", softline_lua_history_add},
    {"history_set_max_len", softline_lua_history_set_max_len},
    {"history_save", softline_lua_history_save},
    {"history_load", softline_lua_history_load},
    {"set_bounds", softline_lua_set_bounds},
    {"set_screen_width", softline_lua_set_screen_width},
    {"set_prompt_queue", softline_lua_set_prompt_queue},
    {"set_prompt_theme", softline_lua_set_prompt_theme},
    {"insert", softline_lua_insert},
    {"set_buffer", softline_lua_set_buffer},
    {"buffer", softline_lua_buffer},
    {"cursor", softline_lua_cursor},
    {"set_cursor", softline_lua_set_cursor},
    {"submit", softline_lua_submit},
    {"cancel", softline_lua_cancel},
    {"bind_key", softline_lua_bind_key},
    {"print_above", softline_lua_print_above},
    {"last_readline_status", softline_lua_last_readline_status},
    {"last_error", softline_lua_last_error},
    {"close", softline_lua_close},
    {NULL, NULL}};

static const luaL_Reg softline_lua_functions[] = {{"new", softline_lua_new},
                                                  {NULL, NULL}};

int luaopen_softline(lua_State *L) {
  luaL_newmetatable(L, SOFTLINE_LUA_HANDLE);
  lua_pushcfunction(L, softline_lua_gc);
  lua_setfield(L, -2, "__gc");
  lua_newtable(L);
  luaL_setfuncs(L, softline_lua_methods, 0);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);

  lua_newtable(L);
  luaL_setfuncs(L, softline_lua_functions, 0);
  lua_pushinteger(L, SL_READLINE_NONE);
  lua_setfield(L, -2, "READLINE_NONE");
  lua_pushinteger(L, SL_READLINE_SUBMITTED);
  lua_setfield(L, -2, "READLINE_SUBMITTED");
  lua_pushinteger(L, SL_READLINE_EOF);
  lua_setfield(L, -2, "READLINE_EOF");
  lua_pushinteger(L, SL_READLINE_CANCELLED);
  lua_setfield(L, -2, "READLINE_CANCELLED");
  lua_pushinteger(L, SL_READLINE_INTERRUPTED);
  lua_setfield(L, -2, "READLINE_INTERRUPTED");
  lua_pushinteger(L, SL_READLINE_ERROR);
  lua_setfield(L, -2, "READLINE_ERROR");
  lua_pushinteger(L, SL_OK);
  lua_setfield(L, -2, "OK");
  lua_pushinteger(L, SL_PROMPT_SOURCE_NONE);
  lua_setfield(L, -2, "PROMPT_SOURCE_NONE");
  lua_pushinteger(L, SL_PROMPT_SOURCE_DIRECT);
  lua_setfield(L, -2, "PROMPT_SOURCE_DIRECT");
  lua_pushinteger(L, SL_PROMPT_SOURCE_QUEUED);
  lua_setfield(L, -2, "PROMPT_SOURCE_QUEUED");
  lua_pushinteger(L, SL_PROMPT_THEME_PLAIN);
  lua_setfield(L, -2, "PROMPT_THEME_PLAIN");
  lua_pushinteger(L, SL_PROMPT_THEME_ACCENT);
  lua_setfield(L, -2, "PROMPT_THEME_ACCENT");
  lua_pushinteger(L, SL_PROMPT_THEME_RICED);
  lua_setfield(L, -2, "PROMPT_THEME_RICED");
  lua_pushinteger(L, SL_KEY_CTRL_C);
  lua_setfield(L, -2, "KEY_CTRL_C");
  lua_pushinteger(L, SL_KEY_ACTION_PASS);
  lua_setfield(L, -2, "KEY_ACTION_PASS");
  lua_pushinteger(L, SL_KEY_ACTION_HANDLED);
  lua_setfield(L, -2, "KEY_ACTION_HANDLED");
  lua_pushinteger(L, SL_KEY_ACTION_SUBMIT);
  lua_setfield(L, -2, "KEY_ACTION_SUBMIT");
  lua_pushinteger(L, SL_KEY_ACTION_CANCEL);
  lua_setfield(L, -2, "KEY_ACTION_CANCEL");
  lua_pushinteger(L, SL_KEY_ACTION_INTERRUPT);
  lua_setfield(L, -2, "KEY_ACTION_INTERRUPT");
  return 1;
}
