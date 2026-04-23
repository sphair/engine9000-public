#include <stddef.h>
#include <string.h>

#include "e9k_checkpoint.h"
#include "geo_serial.h"

static e9k_debug_checkpoint_t e9k_checkpoint_data[E9K_CHECKPOINT_COUNT];
static int e9k_checkpoint_active = -1;
static int e9k_checkpoint_enabled = 0;

void
e9k_checkpoint_reset(void)
{
    for (size_t i = 0; i < E9K_CHECKPOINT_COUNT; ++i) {
        memset(&e9k_checkpoint_data[i].current, 0, sizeof(e9k_checkpoint_data[i]) - offsetof(e9k_debug_checkpoint_t, current));
    }
    e9k_checkpoint_active = -1;
}

void
e9k_checkpoint_resetHard(void)
{
    memset(e9k_checkpoint_data, 0, sizeof(e9k_checkpoint_data));
    e9k_checkpoint_active = -1;
}

void
e9k_checkpoint_setEnabled(int enabled)
{
    e9k_checkpoint_enabled = enabled ? 1 : 0;
    if (!e9k_checkpoint_enabled) {
        e9k_checkpoint_active = -1;
    }
}

int
e9k_checkpoint_isEnabled(void)
{
    return e9k_checkpoint_enabled;
}

void
e9k_checkpoint_state_save(uint8_t *st)
{
    geo_serial_push8(st, (uint8_t)e9k_checkpoint_enabled);
    geo_serial_push32(st, (uint32_t)e9k_checkpoint_active);
    for (size_t i = 0; i < E9K_CHECKPOINT_COUNT; ++i) {
        for (size_t j = 0; j < E9K_CHECKPOINT_NAME_MAX; ++j) {
            geo_serial_push8(st, (uint8_t)e9k_checkpoint_data[i].name[j]);
        }
        geo_serial_push64(st, e9k_checkpoint_data[i].current);
        geo_serial_push64(st, e9k_checkpoint_data[i].accumulator);
        geo_serial_push64(st, e9k_checkpoint_data[i].count);
        geo_serial_push64(st, e9k_checkpoint_data[i].average);
        geo_serial_push64(st, e9k_checkpoint_data[i].minimum);
        geo_serial_push64(st, e9k_checkpoint_data[i].maximum);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineLast);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineCount);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineAccumulator);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineAverage);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineMinimum);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineMaximum);
    }
}

void
e9k_checkpoint_state_load(uint8_t *st)
{
    if (!st) {
        return;
    }
    e9k_checkpoint_enabled = geo_serial_pop8(st) ? 1 : 0;
    e9k_checkpoint_active = (int)geo_serial_pop32(st);
    for (size_t i = 0; i < E9K_CHECKPOINT_COUNT; ++i) {
        memset(&e9k_checkpoint_data[i], 0, sizeof(e9k_checkpoint_data[i]));
        for (size_t j = 0; j < E9K_CHECKPOINT_NAME_MAX; ++j) {
            e9k_checkpoint_data[i].name[j] = (char)geo_serial_pop8(st);
        }
        e9k_checkpoint_data[i].name[E9K_CHECKPOINT_NAME_MAX - 1] = '\0';
        e9k_checkpoint_data[i].current = geo_serial_pop64(st);
        e9k_checkpoint_data[i].accumulator = geo_serial_pop64(st);
        e9k_checkpoint_data[i].count = geo_serial_pop64(st);
        e9k_checkpoint_data[i].average = geo_serial_pop64(st);
        e9k_checkpoint_data[i].minimum = geo_serial_pop64(st);
        e9k_checkpoint_data[i].maximum = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineLast = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineCount = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineAccumulator = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineAverage = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineMinimum = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineMaximum = geo_serial_pop64(st);
    }
    if (!e9k_checkpoint_enabled) {
        e9k_checkpoint_active = -1;
    }
    if (e9k_checkpoint_active < -1 || e9k_checkpoint_active >= (int)E9K_CHECKPOINT_COUNT) {
        e9k_checkpoint_active = -1;
    }
}

void
e9k_checkpoint_setName(uint8_t index, const char *name)
{
    if (index >= E9K_CHECKPOINT_COUNT) {
        return;
    }

    e9k_debug_checkpoint_t *entry = &e9k_checkpoint_data[index];
    memset(entry->name, 0, sizeof(entry->name));
    if (!name) {
        return;
    }

    for (size_t i = 0; i + 1 < sizeof(entry->name); ++i) {
        char c = name[i];
        entry->name[i] = c;
        if (c == '\0') {
            return;
        }
    }
    entry->name[sizeof(entry->name) - 1] = '\0';
}

void
e9k_checkpoint_write(uint8_t index, uint32_t scanline)
{
    if (!e9k_checkpoint_enabled) {
        return;
    }
    if (index >= E9K_CHECKPOINT_COUNT) {
        return;
    }
    if (e9k_checkpoint_active >= 0) {
        e9k_debug_checkpoint_t *prev = &e9k_checkpoint_data[e9k_checkpoint_active];
        uint64_t sample = prev->current;
        if (prev->count == 0) {
            prev->minimum = sample;
            prev->maximum = sample;
        } else {
            if (sample < prev->minimum) {
                prev->minimum = sample;
            }
            if (sample > prev->maximum) {
                prev->maximum = sample;
            }
        }
        prev->count += 1;
        prev->accumulator += sample;
        prev->average = prev->count ? (prev->accumulator / prev->count) : 0;
        prev->current = 0;
    }

    e9k_debug_checkpoint_t *cur = &e9k_checkpoint_data[index];
    uint64_t scanlineSample = (uint64_t)scanline;
    if (cur->scanlineCount == 0) {
        cur->scanlineMinimum = scanlineSample;
        cur->scanlineMaximum = scanlineSample;
    } else {
        if (scanlineSample < cur->scanlineMinimum) {
            cur->scanlineMinimum = scanlineSample;
        }
        if (scanlineSample > cur->scanlineMaximum) {
            cur->scanlineMaximum = scanlineSample;
        }
    }
    cur->scanlineCount += 1;
    cur->scanlineLast = scanlineSample;
    cur->scanlineAccumulator += scanlineSample;
    cur->scanlineAverage = cur->scanlineAccumulator / cur->scanlineCount;

    e9k_checkpoint_active = (int)index;
    cur->current = 0;
}

void
e9k_checkpoint_tick(uint64_t ticks)
{
    if (!e9k_checkpoint_enabled) {
        return;
    }
    if (e9k_checkpoint_active < 0) {
        return;
    }
    e9k_checkpoint_data[e9k_checkpoint_active].current += ticks;
}

size_t
e9k_checkpoint_read(e9k_debug_checkpoint_t *out, size_t cap)
{
    if (!out || cap < sizeof(e9k_checkpoint_data)) {
        return 0;
    }
    memcpy(out, e9k_checkpoint_data, sizeof(e9k_checkpoint_data));
    return sizeof(e9k_checkpoint_data);
}
