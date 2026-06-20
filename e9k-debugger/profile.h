/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui_component.h"
#include "e9ui_context.h"

void
profile_buttonRefresh(void);

void
profile_buttonRefresh(void);

void
profile_uiToggle(e9ui_context_t *ctx, void *user);

void
profile_uiReset(e9ui_context_t *ctx, void *user);

void
profile_uiMetricToggle(e9ui_context_t *ctx, void *user);

void
profile_metricButtonRegister(e9ui_component_t *btn);

void
profile_startFromDebugWrite(void);

void
profile_uiAnalyse(e9ui_context_t *ctx, void *user);

void
profile_analyseOnExitIfRunning(void);

void
profile_drainStream(void);

void
profile_streamStop(void);

void analyse_buttonRefresh(void);
