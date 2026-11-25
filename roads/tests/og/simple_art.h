/* Host shim: the OG screen is not part of the reference; only the editor cookie it defines. */
static uint32_t ms_editor_cookie(int slot) { (void)slot; return 1; }
