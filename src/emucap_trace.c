/*
 * EmuCap - bounded, opt-in GBA action tracing for Android.
 *
 * Hot path: one instruction hit counter / sampled ordered PC per instruction.
 * No file I/O, dynamic allocation or JNI calls on the emulator thread.
 * Outputs instruction coverage and frame-level RAM differences, NOT an
 * exhaustive bus-write trace or a reconstruction of the game's C source.
 */
#include "emucap_trace.h"
#include <jni.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <sched.h>

#define TRACE_PC_CAP 65536u
#define TRACE_SAMPLE_CAP 32768u
#define TRACE_CHANGE_CAP 16384u
#define TRACE_BUTTON_CAP 256u
#define TRACE_FRAME_CAP 1200u
#define TRACE_SAMPLE_PER_PRESS 4096u

typedef struct {
    uint32_t pc;
    uint32_t opcode;
    uint32_t hits;
    uint32_t first_frame;
    uint8_t thumb;
    uint8_t used;
} TracePc;

typedef struct {
    uint32_t pc;
    uint32_t opcode;
    uint32_t frame;
    uint16_t button_event;
    uint8_t thumb;
} TraceSample;

typedef struct {
    uint32_t frame;
    uint32_t address;
    uint8_t before, after;
} TraceChange;

typedef struct {
    uint32_t frame;
    uint16_t keyinput;
} TraceButtons;

static atomic_bool recording = ATOMIC_VAR_INIT(false);
static atomic_uint in_callback = ATOMIC_VAR_INIT(0);
static TracePc pc_hits[TRACE_PC_CAP];
static TraceSample samples[TRACE_SAMPLE_CAP];
static TraceChange changes[TRACE_CHANGE_CAP];
static TraceButtons buttons[TRACE_BUTTON_CAP];
static uint8_t previous_ewram[256 * 1024];
static uint8_t previous_iwram[32 * 1024];
static bool have_ram_baseline;
static uint16_t last_keys;
static uint16_t current_button_event;
static uint32_t remaining_sample;
static uint32_t frame_count, pc_count, sample_count, change_count, button_count;
static uint64_t instructions_seen;
static uint64_t rom_fingerprint;
static uint32_t rom_bytes;
static bool rom_hashed;
static uint32_t pc_overflow, change_overflow, button_overflow;

/* A stop on the UI thread must not access the buffers during an emulator write. */
static bool trace_enter(void) {
    if (!atomic_load_explicit(&recording, memory_order_acquire)) return false;
    atomic_fetch_add_explicit(&in_callback, 1u, memory_order_acq_rel);
    if (!atomic_load_explicit(&recording, memory_order_acquire)) {
        atomic_fetch_sub_explicit(&in_callback, 1u, memory_order_acq_rel);
        return false;
    }
    return true;
}
static void trace_leave(void) {
    atomic_fetch_sub_explicit(&in_callback, 1u, memory_order_release);
}
static void trace_quiesce(void) {
    atomic_store_explicit(&recording, false, memory_order_release);
    while (atomic_load_explicit(&in_callback, memory_order_acquire) != 0u) {
        sched_yield();
    }
}

void emucap_trace_on_keys(uint16_t active_low_keyinput) {
    if (!trace_enter()) return;
    active_low_keyinput &= 0x03ffu;
    if (active_low_keyinput != last_keys) {
        last_keys = active_low_keyinput;
        if (button_count < TRACE_BUTTON_CAP) {
            buttons[button_count].frame = frame_count;
            buttons[button_count].keyinput = active_low_keyinput;
            current_button_event = (uint16_t)(button_count + 1);
            button_count++;
        } else {
            button_overflow++;
            current_button_event = 0;
        }
        remaining_sample = TRACE_SAMPLE_PER_PRESS;
    }
    trace_leave();
}

void emucap_trace_on_instruction(uint32_t address, uint32_t opcode, uint8_t thumb) {
    if (!trace_enter()) return;
    instructions_seen++;

    if (remaining_sample) {
        if (sample_count < TRACE_SAMPLE_CAP) {
            TraceSample *sample = &samples[sample_count++];
            sample->pc = address;
            sample->opcode = opcode;
            sample->frame = frame_count;
            sample->button_event = current_button_event;
            sample->thumb = thumb;
        }
        remaining_sample--;
    }

    /* The ROM appears in three mirrored 32 MiB GBA bus regions.
       Normalize those mirrors so one function maps to one ROM address. */
    if (address >= 0x08000000u && address < 0x0e000000u) {
        uint32_t pc = 0x08000000u | ((address - 0x08000000u) & 0x01fffffeu);
        uint32_t slot = ((pc >> 1) * 2654435761u) & (TRACE_PC_CAP - 1u);
        bool found = false;
        for (unsigned n = 0; n < 32; ++n) {
            TracePc *entry = &pc_hits[(slot + n) & (TRACE_PC_CAP - 1u)];
            if (!entry->used) {
                entry->used = 1;
                entry->pc = pc;
                entry->opcode = opcode;
                entry->thumb = thumb;
                entry->hits = 1;
                entry->first_frame = frame_count;
                pc_count++;
                found = true;
                break;
            }
            if (entry->pc == pc && entry->thumb == thumb) {
                if (entry->hits != UINT32_MAX) entry->hits++;
                found = true;
                break;
            }
        }
        if (!found) pc_overflow++;
    }
    trace_leave();
}

static void trace_diff_ram(const uint8_t *curr, uint8_t *prev,
                           uint32_t size, uint32_t base) {
    unsigned captured_this_frame = 0;
    for (uint32_t i = 0; i < size; ++i) {
        if (curr[i] != prev[i]) {
            if (captured_this_frame < 32u && change_count < TRACE_CHANGE_CAP) {
                TraceChange *change = &changes[change_count++];
                change->frame = frame_count;
                change->address = base + i;
                change->before = prev[i];
                change->after = curr[i];
                captured_this_frame++;
            } else {
                change_overflow++;
            }
            prev[i] = curr[i];
        }
    }
}

void emucap_trace_on_frame(const uint8_t *rom, uint32_t rom_size,
                           const uint8_t *ewram, const uint8_t *iwram) {
    if (!trace_enter()) return;
    /* ROM identification is computed once per trace and never copies ROM data.
       FNV-1a is a comparison fingerprint, not a security digest. */
    if (!rom_hashed && rom && rom_size > 0 && rom_size <= 32u * 1024u * 1024u) {
        uint64_t hash = UINT64_C(14695981039346656037);
        for (uint32_t i = 0; i < rom_size; ++i) {
            hash ^= rom[i];
            hash *= UINT64_C(1099511628211);
        }
        rom_fingerprint = hash;
        rom_bytes = rom_size;
        rom_hashed = true;
    }
    if (!have_ram_baseline) {
        memcpy(previous_ewram, ewram, sizeof(previous_ewram));
        memcpy(previous_iwram, iwram, sizeof(previous_iwram));
        have_ram_baseline = true;
    } else {
        trace_diff_ram(ewram, previous_ewram, sizeof(previous_ewram), 0x02000000u);
        trace_diff_ram(iwram, previous_iwram, sizeof(previous_iwram), 0x03000000u);
    }
    frame_count++;
    if (frame_count >= TRACE_FRAME_CAP)
        atomic_store_explicit(&recording, false, memory_order_release);
    trace_leave();
}

JNIEXPORT jboolean JNICALL
Java_com_sky_SkyEmu_EnhancedNativeActivity_emucapStartTrace(JNIEnv *env, jobject obj) {
    (void)env; (void)obj;
    if (atomic_load_explicit(&recording, memory_order_acquire)) return JNI_FALSE;
    trace_quiesce();
    memset(pc_hits, 0, sizeof(pc_hits));
    frame_count = pc_count = sample_count = change_count = button_count = 0;
    pc_overflow = change_overflow = button_overflow = 0;
    instructions_seen = 0;
    rom_fingerprint = 0;
    rom_bytes = 0;
    rom_hashed = false;
    current_button_event = 0;
    remaining_sample = 0;
    last_keys = 0x03ffu;
    have_ram_baseline = false;
    atomic_store_explicit(&recording, true, memory_order_release);
    return JNI_TRUE;
}

/* JSON is generated off the emulator thread, after writes have stopped. */
JNIEXPORT jboolean JNICALL
Java_com_sky_SkyEmu_EnhancedNativeActivity_emucapStopTrace(JNIEnv *env, jobject obj, jstring filename) {
    (void)obj;
    trace_quiesce();
    if (!filename) return JNI_FALSE;
    const char *path = (*env)->GetStringUTFChars(env, filename, NULL);
    if (!path) return JNI_FALSE;
    FILE *fp = fopen(path, "wb");
    (*env)->ReleaseStringUTFChars(env, filename, path);
    if (!fp) return JNI_FALSE;

    fprintf(fp, "{\n  \"schema\": \"emucap.gba.trace.v1\",\n");
    fprintf(fp, "  \"rom_included\": false,\n  \"frame_limit\": %u,\n", TRACE_FRAME_CAP);
    fprintf(fp, "  \\"rom_fingerprint_fnv1a64\\": \\"0x%016llx\\",\\n  \\"rom_bytes\\": %u,\\n",
            (unsigned long long)rom_fingerprint, rom_bytes);
    fprintf(fp, "  \"frames\": %u,\n  \"instructions_observed\": %llu,\n",
            frame_count, (unsigned long long)instructions_seen);
    fprintf(fp, "  \"pc_overflow\": %u,\n  \"ram_change_overflow\": %u,\n  \"button_overflow\": %u,\n",
            pc_overflow, change_overflow, button_overflow);
    fprintf(fp, "  \"keyinput_note\": \"GBA key register, active low; 0 means pressed\",\n");
    fprintf(fp, "  \"ram_note\": \"Changes are frame-to-frame samples, max 32 bytes per RAM region per frame\",\n");
    fprintf(fp, "  \"pc_note\": \"Canonical ROM bus addresses; executed code outside ROM omitted from pc_coverage\",\n");

    fprintf(fp, "  \"button_changes\": [\n");
    for (uint32_t i = 0; i < button_count; ++i)
        fprintf(fp, "    {\"event\":%u,\"frame\":%u,\"keyinput\":\"0x%03x\"}%s\n",
                i + 1, buttons[i].frame, buttons[i].keyinput,
                i + 1 < button_count ? "," : "");
    fprintf(fp, "  ],\n  \"instruction_samples\": [\n");
    for (uint32_t i = 0; i < sample_count; ++i)
        fprintf(fp, "    {\"frame\":%u,\"button_event\":%u,\"pc\":\"0x%08x\",\"opcode\":\"0x%08x\",\"thumb\":%s}%s\n",
                samples[i].frame, samples[i].button_event, samples[i].pc,
                samples[i].opcode, samples[i].thumb ? "true" : "false",
                i + 1 < sample_count ? "," : "");
    fprintf(fp, "  ],\n  \"pc_coverage\": [\n");
    uint32_t written = 0;
    for (uint32_t i = 0; i < TRACE_PC_CAP; ++i) {
        TracePc *p = &pc_hits[i];
        if (!p->used) continue;
        fprintf(fp, "    {\"pc\":\"0x%08x\",\"opcode\":\"0x%08x\",\"thumb\":%s,\"hits\":%u,\"first_frame\":%u}%s\n",
                p->pc, p->opcode, p->thumb ? "true" : "false",
                p->hits, p->first_frame, ++written < pc_count ? "," : "");
    }
    fprintf(fp, "  ],\n  \"ram_changes\": [\n");
    for (uint32_t i = 0; i < change_count; ++i)
        fprintf(fp, "    {\"frame\":%u,\"address\":\"0x%08x\",\"before\":%u,\"after\":%u}%s\n",
                changes[i].frame, changes[i].address,
                changes[i].before, changes[i].after,
                i + 1 < change_count ? "," : "");
    fprintf(fp, "  ]\n}\n");
    bool success = ferror(fp) == 0;
    if (fclose(fp) != 0) success = false;
    return success ? JNI_TRUE : JNI_FALSE;
}
