/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include "e9ui.h"
#include "e9k-mega.h"
#include "emu.h"

void
emu_mega_setSpriteState(const e9k_debug_mega_sprite_state_t *state, int ready);

void
emu_mega_setAudioFrame(const e9k_debug_mega_audio_frame_t *frame, int ready);

extern const emu_system_iface_t emu_mega_iface;
