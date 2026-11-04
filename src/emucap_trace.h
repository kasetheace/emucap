#ifndef EMUCAP_TRACE_H
#define EMUCAP_TRACE_H

#include <stdint.h>

/* Optional Android-only instrumentation. No ROM image is copied into a trace. */
#if defined(SE_PLATFORM_ANDROID)
void emucap_trace_on_keys(uint16_t active_low_keyinput);
void emucap_trace_on_instruction(uint32_t address, uint32_t opcode, uint8_t thumb);
void emucap_trace_on_frame(const uint8_t *ewram, const uint8_t *iwram);
#endif

#endif /* EMUCAP_TRACE_H */
