/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"
#include "target.h"
#include <SDL.h>
#include <string.h>

target_iface_t* target_targets[3];
target_iface_t* target;
static size_t target_targetCount = 0;

static size_t
target_slotCount(void)
{
    return sizeof(target_targets) / sizeof(target_targets[0]);
}

static void
target_applyConfigDefaultsFor(target_iface_t *iface)
{
    if (iface && iface->setConfigDefaults) {
        iface->setConfigDefaults(&debugger.config);
    }
}

void
target_ctor(void)
{
    memset(target_targets, 0, sizeof(target_targets));
    target_targetCount = 0;
#if E9K_ENABLE_AMIGA
    target_targets[TARGET_AMIGA] = target_amiga();
    target_targetCount++;
#endif
#if E9K_ENABLE_NEOGEO
    target_targets[TARGET_NEOGEO] = target_neogeo();
    target_targetCount++;
#endif
#if E9K_ENABLE_MEGADRIVE
    target_targets[TARGET_MEGADRIVE] = target_megadrive();
    target_targetCount++;
#endif

    target_setConfigDefaults();
}

void
target_releaseUiResources(void)
{
    for (size_t i = 0; i < target_slotCount(); ++i) {
        target_iface_t *iface = target_targets[i];
        if (!iface) {
            continue;
        }
        if (iface->badge) {
            SDL_DestroyTexture(iface->badge);
            iface->badge = NULL;
        }
        iface->badgeRenderer = NULL;
        iface->badgeW = 0;
        iface->badgeH = 0;
    }
}

void
target_setConfigDefaults(void)
{
    target_iface_t *amiga = target_amiga();
    target_iface_t *neogeo = target_neogeo();
    target_iface_t *megadrive = target_megadrive();

    target_applyConfigDefaultsFor(amiga);
    target_applyConfigDefaultsFor(neogeo);
    target_applyConfigDefaultsFor(megadrive);
}

target_iface_t *
target_getByIndex(int index)
{
    if (index < 0 || index >= (int)target_slotCount()) {
        return NULL;
    }
    return target_targets[index];
}

int
target_firstEnabledIndex(void)
{
    for (size_t i = 0; i < target_slotCount(); ++i) {
        if (target_targets[i]) {
            return (int)i;
        }
    }
    return -1;
}

target_iface_t *
target_firstEnabled(void)
{
    int index = target_firstEnabledIndex();
    if (index < 0) {
        return NULL;
    }
    return target_targets[index];
}

int
target_coreOptionsIsSyntheticOptionKey(const char *key)
{
    if (!key || !*key) {
        return 0;
    }
    for (size_t i = 0; i < target_slotCount(); ++i) {
        target_iface_t *iface = target_targets[i];
        if (!iface || !iface->coreOptionsIsSyntheticOptionKey) {
            continue;
        }
        if (iface->coreOptionsIsSyntheticOptionKey(key)) {
            return 1;
        }
    }
    return 0;
}

void
target_settingsClearAllOptions(void)
{
    for (size_t i = 0; i < target_slotCount(); ++i) {
        if (target_targets[i] && target_targets[i]->settingsClearOptions) {
            target_targets[i]->settingsClearOptions();
        }
    }
}

void
target_setTarget(target_iface_t* newTarget)
{
  target = newTarget;
  debugger.config.target = newTarget;
}

void
target_setTargetIndex(int index)
{
  if (index >= 0 && index < (int)target_slotCount() && target_targets[index]) {
    target = target_targets[index];
    debugger.config.target = target;
    return;
  }

  debug_printf("target_setTargetIndex: invalid target index %d\n", index);
}

void
target_nextTarget(void)
{
  if (target_targetCount == 0) {
    debug_printf("target_nextTarget: no enabled targets\n");
    return;
  }

  size_t currentIndex = 0;
  int foundCurrent = 0;
  for (size_t i = 0; i < target_slotCount(); ++i) {
    if (target == target_targets[i]) {
      currentIndex = i;
      foundCurrent = 1;
      break;
    }
  }

  for (size_t step = 1; step <= target_slotCount(); ++step) {
    size_t nextIndex = (foundCurrent ? currentIndex : 0) + step;
    nextIndex %= target_slotCount();
    if (!target_targets[nextIndex]) {
      continue;
    }
    target = target_targets[nextIndex];
    debugger.config.target = target;
    return;
  }

  debug_printf("target_nextTarget: failed to find enabled target\n");
}
