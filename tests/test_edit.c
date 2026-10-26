#include "../src/softline_edit.h"
#include <stdio.h>
#include <string.h>

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "line %d: %s\n", __LINE__, #c);                          \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static int invalid_ranges(void) {
  static const struct {
    size_t at, removed, inserted, cursor, len, cap;
  } cases[] = {
      {0, 0, 3, 0, 0, 8}, /* Suspended draft cleared without journaling. */
      {3, 0, 1, 0, 1, 8},
      {(size_t)-1, 0, 1, 0, 1, 8},
      {0, 0, (size_t)-1, 0, 1, 8},
      {0, 3, 0, 0, 1, 4}, /* Restored bytes plus NUL must fit. */
      {0, 0, 1, 3, 1, 8}, /* A recorded cursor cannot exceed restored text. */
      {0, 0, 1, 0, 1, 0},
      {0, 0, 1, 0, 8, 8}};
  size_t index;
  for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
    sl_edit_t edit;
    char buffer[9] = "sentinel", before[9];
    size_t len = cases[index].len, cursor = 1;
    memset(&edit, 0, sizeof(edit));
    memcpy(before, buffer, sizeof(before));
    edit.enabled = 1;
    CHECK(sl_edit_record(&edit, cases[index].at, "old", cases[index].removed,
                         cases[index].inserted, cases[index].cursor) == 0);
    CHECK(sl_edit_undo(&edit, buffer, cases[index].cap, &len, &cursor) == -1);
    CHECK(len == cases[index].len && cursor == 1 &&
          !memcmp(buffer, before, sizeof(buffer)));
    CHECK(sl_edit_undo(&edit, buffer, sizeof(buffer), &len, &cursor) == 0);
    sl_edit_destroy(&edit);
  }
  return 0;
}

static int group_atomicity(void) {
  sl_edit_t edit;
  char buffer[8] = "XY";
  size_t len = 2, cursor = 2;
  memset(&edit, 0, sizeof(edit));
  edit.enabled = 1;
  sl_edit_begin(&edit, SL_EDIT_INSERT);
  CHECK(sl_edit_record(&edit, 3, "", 0, 1, 0) == 0);
  CHECK(sl_edit_record(&edit, 0, "", 0, 1, 0) == 0);
  sl_edit_end(&edit);
  CHECK(sl_edit_undo(&edit, buffer, sizeof(buffer), &len, &cursor) == -1);
  CHECK(!strcmp(buffer, "XY") && len == 2 && cursor == 2);
  sl_edit_destroy(&edit);
  return 0;
}

int main(void) { return invalid_ranges() || group_atomicity(); }
