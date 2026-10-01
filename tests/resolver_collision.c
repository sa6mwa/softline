/* Simulate an application also linking its own c-ares. The real ares_init is
 * in the same upstream object as ares_init_options, so a missing namespace
 * causes a duplicate symbol instead of silently redirecting either resolver. */
int ares_init(void *channel) {
  (void)channel;
  return -123;
}
