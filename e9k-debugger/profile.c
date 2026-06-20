/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <SDL.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "e9ui.h"
#include "profile.h"
#include "analyse.h"
#include "debug.h"
#include "debugger.h"
#include "libretro_host.h"
#include "profile_list.h"
#include "profile_view.h"
#include "runtime.h"


static e9ui_component_t *s_profile_metricButton = NULL;

static void
profile_metricButtonRefresh(void)
{
    if (!s_profile_metricButton) {
        return;
    }
    if (profile_list_showSamples()) {
        e9ui_button_setLabel(s_profile_metricButton, "Samples");
    } else {
        e9ui_button_setLabel(s_profile_metricButton, "Cycles");
    }
}

static void
profile_analyseRefresh(void)
{
  e9ui_component_t *btn = e9ui->analyseButton;
  if (!btn) {
    return;
  }
  int hasData = (debugger.geo.streamPacketCount > 0) && !debugger.geo.profilerEnabled;
  e9ui_setHidden(btn, !hasData);
}

static void
profile_updateEnabled(int enabled)
{
    if (debugger.geo.profilerEnabled == enabled) {
        return;
    }
    debugger.geo.profilerEnabled = enabled;
    profile_buttonRefresh();
    profile_analyseRefresh();
}

static void
profile_streamStart(void)
{
    if (!analyse_reset()) {
        debug_error("profile: aggregator reset failed");
    }
    debugger.geo.streamPacketCount = 0;
    profile_analyseRefresh();
}

void
profile_streamStop(void)
{
}

static void
profile_handleStreamLine(const char *line)
{
    if (!line || !*line) {
        return;
    }
    debugger.geo.streamPacketCount++;
    if (strstr(line, "\"enabled\":\"enabled\"")) {
        profile_updateEnabled(1);
    } else if (strstr(line, "\"enabled\":\"disabled\"")) {
        profile_updateEnabled(0);
    }
    analyse_handlePacket(line, strlen(line));
    profile_list_notifyUpdate();
}

void
profile_buttonRefresh(void)
{
    e9ui_component_t *btn = e9ui->profileButton;
    if (!btn) {
        return;
    }
    if (debugger.geo.profilerEnabled) {
        e9ui_button_setTheme(btn, e9ui_theme_button_preset_profile_active());
    } else {
        e9ui_button_clearTheme(btn);
    }
}

static int
profile_defaultJsonPath(char *out, size_t cap)
{
    if (!out || cap == 0) {
        return 0;
    }
    return debugger_platform_makeTempFilePath(out, cap, "e9k-profile", ".json");
}

static void
profile_drainStreamBudget(unsigned int maxMillis, size_t maxPackets)
{
    if (!debugger.libretro.enabled) {
        return;
    }
    enum { kBufSize = 262144 };
    static char buf[kBufSize];
    size_t len = 0;
    size_t packetCount = 0;
    unsigned int startTicks = SDL_GetTicks();
    while (libretro_host_profilerStreamNext(buf, sizeof(buf) - 1, &len) && len > 0) {
        buf[len] = '\0';
        profile_handleStreamLine(buf);
        len = 0;
        packetCount++;
        if (maxPackets > 0 && packetCount >= maxPackets) {
            break;
        }
        if (maxMillis > 0 && SDL_GetTicks() - startTicks >= maxMillis) {
            break;
        }
    }
}

void
profile_uiAnalyse(e9ui_context_t *ctx, void *user)
{
    (void)ctx; (void)user;
    char jsonPath[PATH_MAX];
    if (!profile_defaultJsonPath(jsonPath, sizeof(jsonPath))) {
        debug_error("profile: unable to create temporary json output path");
        return;
    }
    jsonPath[sizeof(jsonPath) - 1] = '\0';
    unsigned int start_ticks = SDL_GetTicks();
    debug_printf("Profile analysis started (output=%s)\n", jsonPath);
    profile_drainStreamBudget(0, 0);
    if (!analyse_writeFinalJson(jsonPath)) {
        unsigned int elapsed = SDL_GetTicks() - start_ticks;
        runtime_resetFrameTiming();
        debug_error("profile: analysis failed after %u ms; see earlier logs", elapsed);
        return;
    }
    if (!profile_viewer_run(jsonPath)) {
        unsigned int elapsed = SDL_GetTicks() - start_ticks;
        runtime_resetFrameTiming();
        debug_error("profile: viewer generation failed after %u ms; see earlier logs", elapsed);
        return;
    }
    unsigned int elapsed = SDL_GetTicks() - start_ticks;
    runtime_resetFrameTiming();
    debug_printf("Profile analysis completed (%s) in %.3fs\n", jsonPath, elapsed / 1000.0f);
}

void
profile_analyseOnExitIfRunning(void)
{
    if (!debugger.libretro.enabled || !debugger.geo.profilerEnabled) {
        return;
    }
    debug_printf("profile: debug exit requested; stopping profiler before analysis\n");
    if (!libretro_host_profilerStop()) {
        debug_error("profile: failed to stop profiler before debug exit");
    }
    profile_drainStream();
    profile_streamStop();
    profile_updateEnabled(0);
    profile_uiAnalyse(NULL, NULL);
}

void
analyse_buttonRefresh(void)
{
    e9ui_component_t *btn = e9ui->analyseButton;
    if (!btn) {
        return;
    }
    e9ui_button_setTheme(btn, e9ui_theme_button_preset_red());
}

void
profile_uiToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx; (void)user;
    if (!debugger.libretro.enabled) {
        return;
    }
    if (debugger.geo.profilerEnabled) {
        if (!libretro_host_profilerStop()) {
            return;
        }
        profile_streamStop();
        profile_updateEnabled(0);
    } else {
        if (!libretro_host_profilerStart(1)) {
            return;
        }
        profile_streamStart();
        profile_updateEnabled(1);
    }
}

void
profile_uiReset(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;

    int wasEnabled = debugger.geo.profilerEnabled ? 1 : 0;
    if (wasEnabled && debugger.libretro.enabled) {
        if (!libretro_host_profilerStop()) {
            debug_error("profile: failed to stop profiler for reset");
            return;
        }
        profile_drainStreamBudget(0, 0);
        profile_streamStop();
        profile_updateEnabled(0);
    }

    if (!analyse_reset()) {
        debug_error("profile: aggregator reset failed");
        return;
    }
    debugger.geo.streamPacketCount = 0;
    profile_list_notifyUpdate();
    profile_analyseRefresh();

    if (wasEnabled && debugger.libretro.enabled) {
        if (!libretro_host_profilerStart(1)) {
            debug_error("profile: failed to restart profiler after reset");
            return;
        }
        profile_streamStart();
        profile_updateEnabled(1);
    }
}

void
profile_uiMetricToggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;

    profile_list_toggleMetricMode();
    profile_metricButtonRefresh();
}

void
profile_metricButtonRegister(e9ui_component_t *btn)
{
    s_profile_metricButton = btn;
    profile_metricButtonRefresh();
}

void
profile_startFromDebugWrite(void)
{
    if (!debugger.profileStartOnWrite || !debugger.libretro.enabled || debugger.geo.profilerEnabled) {
        return;
    }
    debug_printf("profile: started on debug write\n");
    if (!libretro_host_profilerStart(1)) {
        debug_error("profile-start-on-write: failed to start profiler");
        return;
    }
    profile_streamStart();
    profile_updateEnabled(1);
}

void
profile_drainStream(void)
{
    if (!debugger.geo.profilerEnabled) {
        return;
    }
    profile_drainStreamBudget(2, 8);
}
