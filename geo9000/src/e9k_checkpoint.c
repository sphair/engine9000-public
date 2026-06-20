#include <stddef.h>
#include <string.h>

#include "e9k_checkpoint.h"
#include "geo_serial.h"

static e9k_debug_checkpoint_t e9k_checkpoint_data[E9K_CHECKPOINT_COUNT];
static e9k_debug_checkpoint_t e9k_checkpoint_publishedData[E9K_CHECKPOINT_COUNT];
static int e9k_checkpoint_active = -1;
static int e9k_checkpoint_enabled = 0;
static int e9k_checkpoint_hasPublishedData = 0;

static void
e9k_checkpoint_publishData(void)
{
    memcpy(e9k_checkpoint_publishedData, e9k_checkpoint_data, sizeof(e9k_checkpoint_data));
    e9k_checkpoint_hasPublishedData = 1;
}

static void
e9k_checkpoint_recordScanline(e9k_debug_checkpoint_t *entry, uint64_t scanline)
{
    if (entry->scanlineCount == 0) {
        entry->scanlineMinimum = scanline;
        entry->scanlineMaximum = scanline;
    } else {
        if (scanline < entry->scanlineMinimum) {
            entry->scanlineMinimum = scanline;
        }
        if (scanline > entry->scanlineMaximum) {
            entry->scanlineMaximum = scanline;
        }
    }
    entry->scanlineCount += 1;
    entry->scanlineLast = scanline;
    entry->scanlineAccumulator += scanline;
    entry->scanlineAverage = entry->scanlineAccumulator / entry->scanlineCount;
}

static void
e9k_checkpoint_recordScanlineSpan(e9k_debug_checkpoint_t *entry, uint64_t scanlineSpan)
{
    if (entry->scanlineSpanCount == 0) {
        entry->scanlineSpanMinimum = scanlineSpan;
        entry->scanlineSpanMaximum = scanlineSpan;
    } else {
        if (scanlineSpan < entry->scanlineSpanMinimum) {
            entry->scanlineSpanMinimum = scanlineSpan;
        }
        if (scanlineSpan > entry->scanlineSpanMaximum) {
            entry->scanlineSpanMaximum = scanlineSpan;
        }
    }
    entry->scanlineSpanCount += 1;
    entry->scanlineSpanLast = scanlineSpan;
    entry->scanlineSpanAccumulator += scanlineSpan;
    entry->scanlineSpanAverage = entry->scanlineSpanAccumulator / entry->scanlineSpanCount;
}

static uint64_t
e9k_checkpoint_scanlineSpan(uint64_t startScanline, uint64_t endScanline, uint64_t scanlineCount)
{
    if (scanlineCount == 0) {
        return 0;
    }
    if (endScanline >= startScanline) {
        return endScanline - startScanline;
    }
    return (scanlineCount - startScanline) + endScanline;
}

void
e9k_checkpoint_reset(void)
{
    for (size_t i = 0; i < E9K_CHECKPOINT_COUNT; ++i) {
        memset(&e9k_checkpoint_data[i].current, 0, sizeof(e9k_checkpoint_data[i]) - offsetof(e9k_debug_checkpoint_t, current));
        memset(&e9k_checkpoint_publishedData[i].current, 0, sizeof(e9k_checkpoint_publishedData[i]) - offsetof(e9k_debug_checkpoint_t, current));
    }
    e9k_checkpoint_active = -1;
    e9k_checkpoint_hasPublishedData = 0;
}

void
e9k_checkpoint_resetHard(void)
{
    memset(e9k_checkpoint_data, 0, sizeof(e9k_checkpoint_data));
    memset(e9k_checkpoint_publishedData, 0, sizeof(e9k_checkpoint_publishedData));
    e9k_checkpoint_active = -1;
    e9k_checkpoint_hasPublishedData = 0;
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
e9k_checkpoint_stateSave(uint8_t *st)
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
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineSpanLast);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineSpanCount);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineSpanAccumulator);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineSpanAverage);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineSpanMinimum);
        geo_serial_push64(st, e9k_checkpoint_data[i].scanlineSpanMaximum);
    }
}

void
e9k_checkpoint_stateLoad(uint8_t *st)
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
        e9k_checkpoint_data[i].scanlineSpanLast = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineSpanCount = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineSpanAccumulator = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineSpanAverage = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineSpanMinimum = geo_serial_pop64(st);
        e9k_checkpoint_data[i].scanlineSpanMaximum = geo_serial_pop64(st);
    }
    if (!e9k_checkpoint_enabled) {
        e9k_checkpoint_active = -1;
    }
    if (e9k_checkpoint_active < -1 || e9k_checkpoint_active >= (int)E9K_CHECKPOINT_COUNT) {
        e9k_checkpoint_active = -1;
    }
    e9k_checkpoint_publishData();
}

void
e9k_checkpoint_setName(uint8_t index, const char *name)
{
    if (index >= E9K_CHECKPOINT_COUNT) {
        return;
    }

    e9k_debug_checkpoint_t *entry = &e9k_checkpoint_data[index];
    e9k_debug_checkpoint_t *publishedEntry = &e9k_checkpoint_publishedData[index];
    memset(entry->name, 0, sizeof(entry->name));
    memset(publishedEntry->name, 0, sizeof(publishedEntry->name));
    if (!name) {
        return;
    }

    for (size_t i = 0; i + 1 < sizeof(entry->name); ++i) {
        char c = name[i];
        entry->name[i] = c;
        publishedEntry->name[i] = c;
        if (c == '\0') {
            return;
        }
    }
    entry->name[sizeof(entry->name) - 1] = '\0';
    publishedEntry->name[sizeof(publishedEntry->name) - 1] = '\0';
}

void
e9k_checkpoint_write(uint8_t index, uint32_t scanline, uint32_t scanlineCount)
{
    int previousIndex = e9k_checkpoint_active;
    uint64_t scanlineSample = (uint64_t)scanline;
    uint64_t scanlineTotal = (uint64_t)scanlineCount;

    if (!e9k_checkpoint_enabled) {
        return;
    }
    if (index >= E9K_CHECKPOINT_COUNT) {
        return;
    }
    if (previousIndex >= 0 && index != 0) {
        e9k_debug_checkpoint_t *prev = &e9k_checkpoint_data[previousIndex];
        uint64_t span = e9k_checkpoint_scanlineSpan(prev->scanlineLast, scanlineSample, scanlineTotal);
        e9k_checkpoint_recordScanlineSpan(prev, span);
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

    if (index == 0 && previousIndex >= 0) {
        e9k_checkpoint_publishData();
    }

    e9k_debug_checkpoint_t *cur = &e9k_checkpoint_data[index];
    e9k_checkpoint_recordScanline(cur, scanlineSample);

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
    if (e9k_checkpoint_hasPublishedData) {
        memcpy(out, e9k_checkpoint_publishedData, sizeof(e9k_checkpoint_publishedData));
    } else {
        memcpy(out, e9k_checkpoint_data, sizeof(e9k_checkpoint_data));
    }
    return sizeof(e9k_checkpoint_data);
}
