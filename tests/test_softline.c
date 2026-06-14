#define _POSIX_C_SOURCE 200809L
#include "softline/softline.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name)                               \
  do {                                           \
    tests_run++;                                 \
    printf("  %-50s", name);                    \
  } while (0)

#define PASS()                                                               \
  do {                                                                       \
    tests_passed++;                                                          \
    printf("PASS\n");                                                        \
  } while (0)

#define FAIL(msg)                                                            \
  do {                                                                       \
    printf("FAIL: %s\n", msg);                                              \
  } while (0)

#define ASSERT(cond, msg)                                                    \
  do {                                                                       \
    if (!(cond)) {                                                           \
      FAIL(msg);                                                             \
      return;                                                                \
    }                                                                        \
  } while (0)

static void test_config_init(void) {
  sl_config_t cfg;

  TEST("config_init zeroes fields");
  memset(&cfg, 0xFF, sizeof(cfg));
  sl_config_init(&cfg);
  ASSERT(cfg.screen_x == 0, "screen_x should be 0");
  ASSERT(cfg.screen_y == 0, "screen_y should be 0");
  ASSERT(cfg.screen_width == 0, "screen_width should be 0");
  ASSERT(cfg.screen_height == 0, "screen_height should be 0");
  ASSERT(cfg.prompt_symbol == NULL, "prompt_symbol should be NULL");
  ASSERT(cfg.indent_mode == SL_INDENT_SPACES, "indent_mode should be SPACES");
  PASS();
}

static void test_create_destroy(void) {
  sl_t *sl;

  TEST("sl_create returns non-NULL");
  sl = sl_create();
  ASSERT(sl != NULL, "sl_create should return non-NULL");
  PASS();

  TEST("sl_destroy on valid handle");
  sl_destroy(sl);
  PASS();

  TEST("sl_destroy on NULL is safe");
  sl_destroy(NULL);
  PASS();
}

static void test_create_with_config(void) {
  sl_config_t cfg;
  sl_t *sl;

  sl_config_init(&cfg);
  cfg.screen_x = 10;
  cfg.screen_y = 5;
  cfg.screen_width = 60;
  cfg.screen_height = 20;
  cfg.prompt_symbol = ">";
  cfg.indent_mode = SL_INDENT_SYMBOL;

  TEST("sl_create_with_config with bounding box");
  sl = sl_create_with_config(&cfg);
  ASSERT(sl != NULL, "should return non-NULL");
  sl_destroy(sl);
  sl = NULL;
  PASS();

  TEST("sl_create_with_config NULL config");
  sl = sl_create_with_config(NULL);
  ASSERT(sl != NULL, "should return non-NULL with NULL config");
  sl_destroy(sl);
  PASS();
}

static void test_set_bounds(void) {
  sl_t *sl;

  sl = sl_create();
  ASSERT(sl != NULL, "create failed");

  TEST("sl_set_bounds valid");
  ASSERT(sl_set_bounds(sl, 0, 0, 80, 24) == 0, "set_bounds should return 0");
  PASS();

  TEST("sl_set_bounds NULL handle");
  ASSERT(sl_set_bounds(NULL, 0, 0, 80, 24) == -1,
         "set_bounds NULL should return -1");
  PASS();

  sl_destroy(sl);
}

static void test_set_prompt_symbol(void) {
  sl_t *sl;

  sl = sl_create();
  ASSERT(sl != NULL, "create failed");

  TEST("sl_set_prompt_symbol valid");
  ASSERT(sl_set_prompt_symbol(sl, "> ") == 0,
         "set_prompt_symbol should return 0");
  PASS();

  TEST("sl_set_prompt_symbol NULL clears");
  ASSERT(sl_set_prompt_symbol(sl, NULL) == 0,
         "set_prompt_symbol NULL should return 0");
  PASS();

  sl_destroy(sl);
}

static void test_set_indent_mode(void) {
  sl_t *sl;

  sl = sl_create();
  ASSERT(sl != NULL, "create failed");

  TEST("sl_set_indent_mode SPACES");
  ASSERT(sl_set_indent_mode(sl, SL_INDENT_SPACES) == 0,
         "set spaces should return 0");
  PASS();

  TEST("sl_set_indent_mode SYMBOL");
  ASSERT(sl_set_indent_mode(sl, SL_INDENT_SYMBOL) == 0,
         "set symbol should return 0");
  PASS();

  sl_destroy(sl);
}

static void test_on_resize(void) {
  sl_t *sl;

  sl = sl_create();
  ASSERT(sl != NULL, "create failed");

  TEST("sl_on_resize valid");
  ASSERT(sl_on_resize(sl, 120, 40) == 0, "on_resize should return 0");
  PASS();

  TEST("sl_on_resize NULL handle");
  ASSERT(sl_on_resize(NULL, 80, 24) == -1,
         "on_resize NULL should return -1");
  PASS();

  sl_destroy(sl);
}

static void test_sl_free(void) {
  char *s;

  TEST("sl_free on malloc'd string");
  s = strdup("hello");
  ASSERT(s != NULL, "strdup failed");
  sl_free(s);
  PASS();

  TEST("sl_free on NULL is safe");
  sl_free(NULL);
  PASS();
}

static void test_header_self(void) {
  TEST("softline.h compiles standalone (implicit)");
  PASS();
}

static void test_history_delegates(void) {
  sl_t *sl;

  sl = sl_create();
  ASSERT(sl != NULL, "create failed");

  TEST("sl_history_add delegates");
  sl_history_add(sl, "test line");
  PASS();

  TEST("sl_history_set_max_len delegates");
  sl_history_set_max_len(sl, 100);
  PASS();

  sl_destroy(sl);
}

int main(void) {
  printf("softline unit tests\n");
  printf("===================\n\n");

  test_config_init();
  test_create_destroy();
  test_create_with_config();
  test_set_bounds();
  test_set_prompt_symbol();
  test_set_indent_mode();
  test_on_resize();
  test_sl_free();
  test_header_self();
  test_history_delegates();

  printf("\n%d/%d tests passed\n", tests_passed, tests_run);
  return tests_passed == tests_run ? 0 : 1;
}