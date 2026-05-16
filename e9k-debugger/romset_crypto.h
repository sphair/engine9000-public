/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

int
romset_crypto_cmc42GfxDecrypt(uint8_t *rom, size_t romSize, uint8_t extraXor);

int
romset_crypto_cmc50GfxDecrypt(uint8_t *rom, size_t romSize, uint8_t extraXor);

void
romset_crypto_sfixDecrypt(const uint8_t *rom,
                          size_t romSize,
                          uint8_t *fixed,
                          size_t fixedSize);

int
romset_crypto_cmc50M1Decrypt(uint8_t *rom, size_t cryptSize, size_t audioSize);

int
romset_crypto_pcm2Decrypt(uint8_t *rom, size_t romSize, int value);

int
romset_crypto_applyNgh264Cmc50Pcm2(uint8_t *crom,
                                   size_t cromSize,
                                   uint8_t *srom,
                                   size_t sromSize,
                                   uint8_t *mrom,
                                   size_t mromSize,
                                   uint8_t *vrom,
                                   size_t vromSize);

int
romset_crypto_applyNgh256SmaCmc42(uint8_t *prom,
                                  size_t promSize,
                                  uint8_t *crom,
                                  size_t cromSize,
                                  uint8_t *srom,
                                  size_t sromSize);

int
romset_crypto_applyNgh253SmaCmc42(uint8_t *prom,
                                  size_t promSize,
                                  uint8_t *crom,
                                  size_t cromSize,
                                  uint8_t *srom,
                                  size_t sromSize);

int
romset_crypto_applyNgh253RevisionBSmaCmc42(uint8_t *prom,
                                           size_t promSize,
                                           uint8_t *crom,
                                           size_t cromSize,
                                           uint8_t *srom,
                                           size_t sromSize);
