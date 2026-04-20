/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdint.h>
#include <stdlib.h>
#include <SDL.h>

#include "aux_window.h"
#include "runtime.h"
#include "ui.h"
#include "debug.h"
#include "e9ui.h"
#include "input_record.h"
#include "libretro_host.h"
#include "linebuf.h"
#include "machine.h"
#include "profile.h"
#include "protect.h"
#include "smoke_test.h"
#include "ui_test.h"
#include "state_buffer.h"
#include "train.h"
#include "debugger_signal.h"
#include "settings.h"
#include "debugger.h"

static const char *
runtime_watchAccessSourceName(uint32_t accessSource)
{
    switch (accessSource) {
    case E9K_WATCH_ACCESS_SOURCE_CPU:
        return "cpu";
    case E9K_WATCH_ACCESS_SOURCE_DMA:
        return "dma";
    case E9K_WATCH_ACCESS_SOURCE_BLITTER:
        return "blitter";
    case E9K_WATCH_ACCESS_SOURCE_COPPER:
        return "copper";
    case E9K_WATCH_ACCESS_SOURCE_AUDIO:
        return "audio";
    case E9K_WATCH_ACCESS_SOURCE_VIDEO:
        return "video";
    case E9K_WATCH_ACCESS_SOURCE_PERIPHERAL:
        return "peripheral";
    default:
        return "unknown";
    }
}

void
runtime_onVblank(void *user)
{
    (void)user;
    if (state_buffer_isPaused()) {
        return;
    }
    target->onVblank();
    debugger.frameCounter++;
    if (!debugger.smokeTestFailed && !debugger.smokeTestCompleted) {
        int smokeResult = smoke_test_captureFrame(debugger.frameCounter);
        if (smokeResult == 1) {
            debugger.smokeTestFailed = 1;
            debugger.smokeTestExitCode = 1;
        } else if (smokeResult == 2) {
            debugger.smokeTestCompleted = 1;
            debugger.smokeTestExitCode = 0;
            debug_printf("*** SMOKE TEST PASSED ***");
        }
    }
}

void
runtime_executeFrame(debugger_run_mode_t mode, int restoreFrame)
{
    (void)restoreFrame;
    if (mode == DEBUGGER_RUNMODE_CAPTURE) {
        state_buffer_setCurrentFrameNo(debugger.frameCounter);
        state_buffer_capture();
    } else if (mode == DEBUGGER_RUNMODE_RESTORE) {
        state_buffer_setCurrentFrameNo(restoreFrame);
        state_buffer_restoreFrameNo(restoreFrame);
    }
    _libretro_host_runOnce();
}

static void
runtime_restoreSuppressedBreakpoint(void)
{
    if (!debugger.suppressBpActive) {
        return;
    }
    uint32_t addr = debugger.suppressBpAddr;
    libretro_host_debugAddBreakpoint(addr);
    debugger.suppressBpActive = 0;
}

static int
runtime_isUiTestDeterministicMode(void)
{
    if (!ui_test_isEnabled()) {
        return 0;
    }
    ui_test_mode_t mode = ui_test_getMode();
    if (mode == UI_TEST_MODE_RECORD ||
        mode == UI_TEST_MODE_COMPARE ||
        mode == UI_TEST_MODE_REMAKE) {
        return 1;
    }
    return 0;
}

static int
runtime_isBreakpointHit(void)
{
    unsigned long pc = 0;
    if (!machine_findReg(&debugger.machine, "PC", &pc)) {
        return 0;
    }
    machine_breakpoint_t *bp =
        machine_findBreakpointByAddr(&debugger.machine, (uint32_t)(pc & 0x00ffffffu));
    if (!bp) {
        return 0;
    }
    return bp->enabled ? 1 : 0;
}

static void
runtime_executeNextFrame(void)
{
    if (debugger.loopEnabled) {
        if (debugger.frameCounter < debugger.loopFrom ||
            debugger.frameCounter >= debugger.loopTo) {
            debugger.frameCounter = debugger.loopFrom;
        } else {
            runtime_executeFrame(DEBUGGER_RUNMODE_RESTORE, debugger.frameCounter + 1);
        }
    } else {
        input_record_applyFrame(debugger.frameCounter + 1);
        runtime_executeFrame(DEBUGGER_RUNMODE_CAPTURE, 0);
    }
}

void
runtime_runLoop(void)
{
    SDL_StartTextInput();
    static char dbg_line[1024];
    static size_t dbg_line_len = 0;
    while (1) {
        if (e9ui->transition.inTransition < 0) {
            e9ui->transition.inTransition += 5;
            if (e9ui->transition.inTransition > 0) {
                e9ui->transition.inTransition = 0;
            }
        }
        input_record_applyUiFrame(debugger.uiFrameCounter + 1);
        if (signal_getExitCode() || e9ui_processEvents()) {
            break;
        }
        if (debugger.restartRequested) {
            break;
        }
        if (e9ui->pendingRemove) {
            e9ui_component_t *hostRoot = e9ui_getOverlayHost();
            e9ui_component_t *removeParent = NULL;
            if (hostRoot) {
                for (list_t *ptr = hostRoot->children; ptr; ptr = ptr->next) {
                    e9ui_component_child_t *container = (e9ui_component_child_t *)ptr->data;
                    if (container && container->component == e9ui->pendingRemove) {
                        removeParent = hostRoot;
                        break;
                    }
                }
            }
            if (!removeParent && e9ui->root) {
                removeParent = e9ui->root;
            }
            if (removeParent) {
                e9ui_childRemove(removeParent, e9ui->pendingRemove, &e9ui->ctx);
            }
            e9ui->pendingRemove = NULL;
        }
        settings_pollRebuild(&e9ui->ctx);
        if (debugger.libretro.enabled) {
            int paused = 0;
            if (libretro_host_debugIsPaused(&paused)) {
                int wasRunning = machine_getRunning(debugger.machine);
                machine_setRunning(&debugger.machine, paused ? 0 : 1);
                if (paused && wasRunning) {
                    debugger_clearFrameStep();
                    runtime_restoreSuppressedBreakpoint();
                    int watchbreakHit = 0;
                    e9k_debug_watchbreak_t watchbreak;
                    if (libretro_host_debugConsumeWatchbreak(&watchbreak)) {
                        watchbreakHit = 1;
                        train_setLastWatchbreak(&watchbreak);
                        if (protect_handleWatchbreak(&watchbreak)) {
                            libretro_host_debugResume();
                            machine_setRunning(&debugger.machine, 1);
                            continue;
                        }
                        if (train_isIgnoredAddr(watchbreak.access_addr & 0x00ffffffu)) {
                            libretro_host_debugResume();
                            machine_setRunning(&debugger.machine, 1);
                            continue;
                        }
                        const char *kind = watchbreak.access_kind == E9K_WATCH_ACCESS_WRITE ? "write" : "read";
                        const char *source = runtime_watchAccessSourceName(watchbreak.access_source);
                        if (watchbreak.old_value_valid) {
                            debug_printf("watchbreak: wp[%u] %s/%s addr=0x%06X value=0x%08X old=0x%08X\n",
                                        (unsigned)watchbreak.index, kind, source,
                                        (unsigned)(watchbreak.access_addr & 0x00ffffffu),
                                        (unsigned)watchbreak.value, (unsigned)watchbreak.old_value);
                        } else {
                            debug_printf("watchbreak: wp[%u] %s/%s addr=0x%06X value=0x%08X\n",
                                        (unsigned)watchbreak.index, kind, source,
                                        (unsigned)(watchbreak.access_addr & 0x00ffffffu),
                                        (unsigned)watchbreak.value);
                        }
                    }
                    if (!watchbreakHit && runtime_isBreakpointHit()) {
                        e9ui_showTransientMessage("BREAKPOINT HIT");
                    }
                }
            }
            uint64_t now = SDL_GetPerformanceCounter();
            if (debugger.frameTimeCounter == 0) {
                debugger.frameTimeCounter = now;
            }
            double dt = (double)(now - debugger.frameTimeCounter) / (double)SDL_GetPerformanceFrequency();
            debugger.frameTimeCounter = now;

            int running = machine_getRunning(debugger.machine);
            int modalOpen = (e9ui && (e9ui->settingsModal || e9ui->coreOptionsModal || e9ui->helpModal)) ? 1 : 0;
            if (modalOpen) {
                running = 0;
            }
            if (debugger_isSeeking() || debugger.frameStepMode || !running) {
                debugger.frameTimeAccum = 0.0;
            }

            if (!debugger_isSeeking() && !modalOpen) {
                if (debugger.frameStepMode) {
                    if (debugger.frameStepPending) {
                        if (debugger.frameStepPending > 0) {
                            //runtime_executeFrame(DEBUGGER_RUNMODE_CAPTURE, 0);
                            runtime_executeNextFrame();
                        } else if (debugger.frameStepPending < 0) {
                            runtime_executeFrame(DEBUGGER_RUNMODE_RESTORE, debugger.frameCounter - 2);
                            debugger.frameCounter -= 2;
                        }
                        debugger.frameStepPending = 0;
                    }
                } else if (running) {
                    if (runtime_isUiTestDeterministicMode()) {
                        debugger.frameTimeAccum = 0.0;
                        runtime_executeNextFrame();
                    } else {
                        int mult = debugger.speedMultiplier > 0 ? debugger.speedMultiplier : 1;
                        if (mult > 1) {
                            debugger.frameTimeAccum = 0.0;
                            for (int frameIndex = 0; frameIndex < mult; ++frameIndex) {
                                input_record_applyFrame(debugger.frameCounter + 1);
                                runtime_executeFrame(DEBUGGER_RUNMODE_CAPTURE, 0);
                            }
                        } else {
                            double fps = libretro_host_getTimingFps();
                            double frameTime = (fps > 1e-3) ? (1.0 / fps) : (1.0 / 60.0);
                            debugger.frameTimeAccum += dt;
                            if (debugger.frameTimeAccum >= frameTime) {
                                runtime_executeNextFrame();
                                debugger.frameTimeAccum -= frameTime;
                            }
                        }
                    }
                }
            }
            {
                char buf[256];
                size_t n = 0;
                while ((n = libretro_host_debugTextRead(buf, sizeof(buf))) > 0) {
                    int stdoutNeedsFlush = 0;
                    for (size_t i = 0; i < n; ++i) {
                        char c = buf[i];
                        if (c == '\r') {
                            continue;
                        }
                        if (debugger.opts.redirectStdout) {
                            fputc(c, stdout);
                            stdoutNeedsFlush = 1;
                            if (c == '\n') {
                                fflush(stdout);
                                stdoutNeedsFlush = 0;
                            }
                        }
                        if (c == '\n') {
                            dbg_line[dbg_line_len] = '\0';
                            if (dbg_line_len > 0) {
                                linebuf_push(&debugger.console, dbg_line);
                            }
                            dbg_line_len = 0;
                            continue;
                        }
                        if (dbg_line_len + 1 < sizeof(dbg_line)) {
                            dbg_line[dbg_line_len++] = c;
                        } else {
                            dbg_line[dbg_line_len] = '\0';
                            if (dbg_line_len > 0) {
                                linebuf_push(&debugger.console, dbg_line);
                            }
                            dbg_line_len = 0;
                        }
                    }
                    if (stdoutNeedsFlush) {
                        fflush(stdout);
                    }
                }
            }
        }
        profile_drainStream();
        ui_updateSourceTitle();
        ui_updateWindowTitle();
        e9ui_renderFrame();
        aux_window_render();
        if (debugger.smokeTestCompleted) {
            break;
        }
        if (debugger.smokeTestFailed) {
            break;
        }
        if (ui_test_hasFailed()) {
            break;
        }
        if (debugger.smokeTestMode != SMOKE_TEST_MODE_NONE &&
            debugger.smokeTestMode != SMOKE_TEST_MODE_RECORD &&
            input_record_isPlaybackComplete()) {
            debugger.smokeTestCompleted = 1;
            debugger.smokeTestExitCode = 0;
            if (debugger.smokeTestMode == SMOKE_TEST_MODE_COMPARE) {
                debug_printf("*** SMOKE TEST PASSED ***");
            } else if (debugger.smokeTestMode == SMOKE_TEST_MODE_REMAKE) {
                debug_printf("*** REMAKE SMOKE COMPLETE ***");
            }
            break;
        }
        if (ui_test_checkPlaybackComplete()) {
            break;
        }
        //  SDL_Delay(16);
    }
}
