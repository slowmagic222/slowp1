/* Host shim: the OG loader's names, with the bank handed over by og_ref.c. */
static const uint8_t *volatile ms_audio;
static int ms_bank_ready(void) { return ms_audio != 0; }
