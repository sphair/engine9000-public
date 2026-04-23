/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdint.h>

int
hex_byte_color_isEnabled(void);

void
hex_byte_color_setEnabled(int enabled);

SDL_Color
hex_byte_color_get(uint8_t byte);

void
hex_byte_color_drawHexByte(SDL_Renderer *renderer, TTF_Font *font, uint8_t byte, int x, int y);

int
hex_byte_color_drawHexByteRow(SDL_Renderer *renderer,
                              TTF_Font *font,
                              const uint8_t *bytes,
                              int byteCount,
                              int x,
                              int y,
                              int columnWidth);

void
hex_byte_color_drawAsciiByte(SDL_Renderer *renderer, TTF_Font *font, uint8_t byte, int x, int y);
