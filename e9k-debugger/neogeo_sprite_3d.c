/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if E9K_ENABLE_GL_COMPOSITE
#include <SDL_opengl.h>
#endif

#include "neogeo_sprite_3d.h"
#include "alloc.h"
#include "config.h"
#include "e9ui_box.h"
#include "e9ui_checkbox.h"
#include "e9ui_hstack.h"
#include "e9ui_scroll.h"
#include "e9ui_seek_bar.h"
#include "e9ui_slider.h"
#include "e9ui_spacer.h"
#include "e9ui_stack.h"
#include "e9ui_window.h"
#include "libretro_host.h"
#include "neogeo_memview_internal.h"
#include "neogeo_sprite_decode.h"

#define NEOGEO_SPRITE_3D_SPRITE_VRAM_WORDS_PER_SPRITE 64u
#define NEOGEO_SPRITE_3D_SPRITE_TILE_ODD_WORD_OFFSET 1u
#define NEOGEO_SPRITE_3D_SPRITE_TILE_HIGH_MASK 0x00f0u
#define NEOGEO_SPRITE_3D_SPRITE_TILE_HIGH_SHIFT 12u
#define NEOGEO_SPRITE_3D_SPRITE_HFLIP 0x0001u
#define NEOGEO_SPRITE_3D_SPRITE_VFLIP 0x0002u
#define NEOGEO_SPRITE_3D_SPRITE_ANIM_MASK 0x0cu
#define NEOGEO_SPRITE_3D_SPRITE_ANIM_SHIFT 2u
#define NEOGEO_SPRITE_3D_CROM_TILE_BYTES 128u
#define NEOGEO_SPRITE_3D_TILE_W 16u
#define NEOGEO_SPRITE_3D_TILE_H 16u
#define NEOGEO_SPRITE_3D_LINE_COUNT NEOGEO_SPRITE_DECODE_LINE_COUNT
#define NEOGEO_SPRITE_3D_L0_SIZE 0x10000u
#define NEOGEO_SPRITE_3D_ROM_ENTRY_MAX 12
#define NEOGEO_SPRITE_3D_SCROLL_CONTENT_W 1280
#define NEOGEO_SPRITE_3D_SCROLL_CONTENT_H 960

static neogeo_sprite_3d_source_t neogeo_sprite_3dSource;

#if E9K_ENABLE_GL_COMPOSITE
typedef neogeo_sprite_decode_sprite_t neogeo_sprite_3d_decoded_sprite_t;
typedef neogeo_sprite_decode_line_sprites_t neogeo_sprite_3d_line_sprites_t;

static uint32_t
neogeo_sprite_3d_color(Uint8 r, Uint8 g, Uint8 b);

static uint32_t
neogeo_sprite_3d_shrinkRgbColor(unsigned hshrink, unsigned vshrink);

static uint32_t
neogeo_sprite_3d_paletteColor(unsigned paletteBank);

static uint32_t
neogeo_sprite_3d_chainColor(unsigned chainRootIndex);

static uint32_t
neogeo_sprite_3d_color(Uint8 r, Uint8 g, Uint8 b)
{
    return (uint32_t)(0xFF000000u | (r << 16) | (g << 8) | b);
}

static uint32_t
neogeo_sprite_3d_shrinkRgbColor(unsigned hshrink, unsigned vshrink)
{
    float horizontalCoverage = (float)neogeo_sprite_decode_countShrinkWidth(hshrink) / 16.0f;
    float verticalCoverage = (float)(vshrink + 1u) / 256.0f;
    float horizontalShrink = 1.0f - horizontalCoverage;
    float verticalShrink = 1.0f - verticalCoverage;
    Uint8 red = (Uint8)(horizontalShrink * 255.0f);
    Uint8 blue = (Uint8)(verticalShrink * 255.0f);

    return neogeo_sprite_3d_color(red, 255, blue);
}

static uint32_t
neogeo_sprite_3d_paletteColor(unsigned paletteBank)
{
    unsigned hue = (paletteBank * 47u) & 0xffu;
    return neogeo_sprite_3d_color((Uint8)hue, (Uint8)(255u - hue), (Uint8)((hue * 3u) & 0xffu));
}

static uint32_t
neogeo_sprite_3d_chainColor(unsigned chainRootIndex)
{
    unsigned hue = (chainRootIndex * 37u) & 0xffu;
    return neogeo_sprite_3d_color((Uint8)hue, (Uint8)((hue * 5u) & 0xffu), (Uint8)(255u - hue));
}
#endif

static int
neogeo_sprite_3d_parseInt(const char *value, int *out);

static int
neogeo_sprite_3d_parseFloat(const char *value, float *out);

typedef struct neogeo_sprite_3d_state {
    e9ui_window_state_t windowState;
    e9ui_component_t *root;
    e9ui_component_t *controls;
    e9ui_component_t *scroll;
    e9ui_component_t *body;
    int savedScrollX;
    int savedScrollY;
    int hasSavedScroll;
    int showOutlines;
    int showContent;
    int clipToVisibleArea;
    float contentOpacityPercent;
    float yawDegrees;
    float pitchDegrees;
    float zoomPercent;
    float zSeparation;
} neogeo_sprite_3d_state_t;

typedef enum neogeo_sprite_3d_slider_kind {
    neogeo_sprite_3d_slider_yaw = 0,
    neogeo_sprite_3d_slider_pitch,
    neogeo_sprite_3d_slider_zoom,
    neogeo_sprite_3d_slider_z_separation,
    neogeo_sprite_3d_slider_content_opacity
} neogeo_sprite_3d_slider_kind_t;

typedef struct neogeo_sprite_3d_slider_binding {
    neogeo_sprite_3d_slider_kind_t kind;
    float minValue;
    float maxValue;
    const char *unit;
} neogeo_sprite_3d_slider_binding_t;

typedef enum neogeo_sprite_3d_checkbox_kind {
    neogeo_sprite_3d_checkbox_outlines = 0,
    neogeo_sprite_3d_checkbox_content,
    neogeo_sprite_3d_checkbox_clip_to_visible_area
} neogeo_sprite_3d_checkbox_kind_t;

#if E9K_ENABLE_GL_COMPOSITE
typedef struct neogeo_sprite_3d_render_space {
    int x0;
    int y0;
    int w;
    int h;
    int fullCoordinateSpace;
} neogeo_sprite_3d_render_space_t;
#endif

static neogeo_sprite_3d_state_t neogeo_sprite_3dState = {
    .windowState.winX = E9UI_WINDOW_COORD_UNSET,
    .windowState.winY = E9UI_WINDOW_COORD_UNSET,
    .windowState.openMinWidthPx = 320,
    .windowState.openMinHeightPx = 240,
    .windowState.openCenterWhenNoSaved = 1,
    .yawDegrees = 25.0f,
    .pitchDegrees = 55.0f,
    .zoomPercent = 100.0f,
    .zSeparation = 2.0f,
    .showOutlines = 1,
    .showContent = 1,
    .contentOpacityPercent = 92.0f,
};

static neogeo_sprite_3d_slider_binding_t neogeo_sprite_3d_sliderBindings[] = {
    { neogeo_sprite_3d_slider_yaw, -90.0f, 90.0f, " deg" },
    { neogeo_sprite_3d_slider_pitch, -90.0f, 90.0f, " deg" },
    { neogeo_sprite_3d_slider_zoom, 50.0f, 250.0f, "%" },
    { neogeo_sprite_3d_slider_z_separation, 0.0f, 64.0f, " px" },
    { neogeo_sprite_3d_slider_content_opacity, 0.0f, 100.0f, "%" },
};

static neogeo_sprite_3d_checkbox_kind_t neogeo_sprite_3d_checkboxOutlines =
    neogeo_sprite_3d_checkbox_outlines;
static neogeo_sprite_3d_checkbox_kind_t neogeo_sprite_3d_checkboxContent =
    neogeo_sprite_3d_checkbox_content;
static neogeo_sprite_3d_checkbox_kind_t neogeo_sprite_3d_checkboxClipToVisibleArea =
    neogeo_sprite_3d_checkbox_clip_to_visible_area;

static e9ui_window_backend_t
neogeo_sprite_3d_windowBackend(void)
{
    return e9ui_window_backend_overlay;
}

static e9ui_rect_t
neogeo_sprite_3d_windowDefaultRect(const e9ui_context_t *ctx)
{
    e9ui_rect_t rect = {
        e9ui_scale_px(ctx, 128),
        e9ui_scale_px(ctx, 128),
        e9ui_scale_px(ctx, 480),
        e9ui_scale_px(ctx, 360)
    };
    return rect;
}

static void
neogeo_sprite_3d_bodyLayout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static float
neogeo_sprite_3d_sliderPercent(float value, const neogeo_sprite_3d_slider_binding_t *binding)
{
    float range = binding->maxValue - binding->minValue;
    if (range <= 0.0f) {
        return 0.0f;
    }
    value = (value - binding->minValue) / range;
    if (value < 0.0f) {
        value = 0.0f;
    }
    if (value > 1.0f) {
        value = 1.0f;
    }
    return value;
}

static float
neogeo_sprite_3d_sliderValue(float percent, const neogeo_sprite_3d_slider_binding_t *binding)
{
    if (percent < 0.0f) {
        percent = 0.0f;
    }
    if (percent > 1.0f) {
        percent = 1.0f;
    }
    return binding->minValue + (binding->maxValue - binding->minValue) * percent;
}

static float
neogeo_sprite_3d_getSliderValue(const neogeo_sprite_3d_slider_binding_t *binding)
{
    switch (binding->kind) {
        case neogeo_sprite_3d_slider_yaw:
            return neogeo_sprite_3dState.yawDegrees;
        case neogeo_sprite_3d_slider_pitch:
            return neogeo_sprite_3dState.pitchDegrees;
        case neogeo_sprite_3d_slider_zoom:
            return neogeo_sprite_3dState.zoomPercent;
        case neogeo_sprite_3d_slider_z_separation:
            return neogeo_sprite_3dState.zSeparation;
        case neogeo_sprite_3d_slider_content_opacity:
            return neogeo_sprite_3dState.contentOpacityPercent;
    }
    return binding->minValue;
}

static void
neogeo_sprite_3d_setSliderValue(neogeo_sprite_3d_slider_binding_t *binding, float value)
{
    switch (binding->kind) {
        case neogeo_sprite_3d_slider_yaw:
            neogeo_sprite_3dState.yawDegrees = value;
            break;
        case neogeo_sprite_3d_slider_pitch:
            neogeo_sprite_3dState.pitchDegrees = value;
            break;
        case neogeo_sprite_3d_slider_zoom:
            neogeo_sprite_3dState.zoomPercent = value;
            break;
        case neogeo_sprite_3d_slider_z_separation:
            neogeo_sprite_3dState.zSeparation = value;
            break;
        case neogeo_sprite_3d_slider_content_opacity:
            neogeo_sprite_3dState.contentOpacityPercent = value;
            break;
    }
}

static void
neogeo_sprite_3d_sliderChanged(float percent, void *user)
{
    neogeo_sprite_3d_slider_binding_t *binding = (neogeo_sprite_3d_slider_binding_t *)user;
    neogeo_sprite_3d_setSliderValue(binding,
                                             neogeo_sprite_3d_sliderValue(percent, binding));
}

static void
neogeo_sprite_3d_sliderTooltip(float percent, char *out, size_t cap, void *user)
{
    neogeo_sprite_3d_slider_binding_t *binding = (neogeo_sprite_3d_slider_binding_t *)user;
    float value = neogeo_sprite_3d_sliderValue(percent, binding);
    snprintf(out, cap, "%.1f%s", value, binding->unit ? binding->unit : "");
}

static void
neogeo_sprite_3d_checkboxChanged(e9ui_component_t *self, e9ui_context_t *ctx, int selected, void *user)
{
    (void)self;
    (void)ctx;
    neogeo_sprite_3d_checkbox_kind_t *kind = (neogeo_sprite_3d_checkbox_kind_t *)user;

    if (*kind == neogeo_sprite_3d_checkbox_outlines) {
        neogeo_sprite_3dState.showOutlines = selected ? 1 : 0;
    } else if (*kind == neogeo_sprite_3d_checkbox_content) {
        neogeo_sprite_3dState.showContent = selected ? 1 : 0;
    } else if (*kind == neogeo_sprite_3d_checkbox_clip_to_visible_area) {
        neogeo_sprite_3dState.clipToVisibleArea = selected ? 1 : 0;
    }
    config_saveConfig();
}

#if E9K_ENABLE_GL_COMPOSITE
typedef void (APIENTRYP neogeo_sprite_3d_gl_use_program_fn)(GLuint program);

static neogeo_sprite_3d_gl_use_program_fn neogeo_sprite_3d_glUseProgram = NULL;

static void
neogeo_sprite_3d_colorFromUint(uint32_t color, float alpha)
{
    glColor4f((float)((color >> 16) & 0xffu) / 255.0f,
              (float)((color >> 8) & 0xffu) / 255.0f,
              (float)(color & 0xffu) / 255.0f,
              alpha);
}

static neogeo_sprite_3d_render_space_t
neogeo_sprite_3d_makeRenderSpace(void)
{
    neogeo_sprite_3d_render_space_t renderSpace;

    renderSpace.x0 = 0;
    renderSpace.y0 = 0;
    if (!neogeo_sprite_3dState.clipToVisibleArea) {
        renderSpace.x0 = NEOGEO_SPRITE_DECODE_COORD_MIN_X;
        renderSpace.y0 = NEOGEO_SPRITE_DECODE_COORD_MIN_Y;
        renderSpace.w = NEOGEO_SPRITE_DECODE_COORD_W;
        renderSpace.h = NEOGEO_SPRITE_DECODE_COORD_H;
        renderSpace.fullCoordinateSpace = 1;
    } else {
        renderSpace.w = NEOGEO_SPRITE_DECODE_VISIBLE_W;
        renderSpace.h = NEOGEO_SPRITE_DECODE_VISIBLE_H;
        renderSpace.fullCoordinateSpace = 0;
    }
    return renderSpace;
}

static float
neogeo_sprite_3d_alphaForPoint(const neogeo_sprite_3d_render_space_t *renderSpace,
                               int x,
                               int y,
                               float alpha)
{
    if (!renderSpace->fullCoordinateSpace) {
        return alpha;
    }
    if (x < NEOGEO_SPRITE_DECODE_VISIBLE_X0 ||
        x >= (NEOGEO_SPRITE_DECODE_VISIBLE_X0 + NEOGEO_SPRITE_DECODE_VISIBLE_W) ||
        y < NEOGEO_SPRITE_DECODE_VISIBLE_Y0 ||
        y >= (NEOGEO_SPRITE_DECODE_VISIBLE_Y0 + NEOGEO_SPRITE_DECODE_VISIBLE_H)) {
        return alpha * 0.5f;
    }
    return alpha;
}

static void
neogeo_sprite_3d_vertexProjected(const neogeo_sprite_3d_render_space_t *renderSpace,
                                 float x,
                                 float y,
                                 float z)
{
    const float pi = 3.14159265358979323846f;
    float localX = (x - (float)renderSpace->x0) - (float)renderSpace->w * 0.5f;
    float localY = (float)renderSpace->h * 0.5f - (y - (float)renderSpace->y0);
    float yaw = neogeo_sprite_3dState.yawDegrees * pi / 180.0f;
    float pitch = neogeo_sprite_3dState.pitchDegrees * pi / 180.0f;
    float cosYaw = cosf(yaw);
    float sinYaw = sinf(yaw);
    float cosPitch = cosf(pitch);
    float sinPitch = sinf(pitch);
    float projectedX = localX * cosYaw + z * sinYaw;
    float projectedY = localY * cosPitch + localX * sinYaw * sinPitch - z * cosYaw * sinPitch;

    glVertex3f(projectedX, projectedY, z * 0.001f);
}

static void
neogeo_sprite_3d_drawPlane(const neogeo_sprite_3d_render_space_t *renderSpace)
{
    neogeo_sprite_3d_colorFromUint(neogeo_sprite_3d_color(18, 21, 28), 1.0f);
    glBegin(GL_QUADS);
    neogeo_sprite_3d_vertexProjected(renderSpace, renderSpace->x0, renderSpace->y0, 0.0f);
    neogeo_sprite_3d_vertexProjected(renderSpace,
                                     renderSpace->x0 + renderSpace->w,
                                     renderSpace->y0,
                                     0.0f);
    neogeo_sprite_3d_vertexProjected(renderSpace,
                                     renderSpace->x0 + renderSpace->w,
                                     renderSpace->y0 + renderSpace->h,
                                     0.0f);
    neogeo_sprite_3d_vertexProjected(renderSpace,
                                     renderSpace->x0,
                                     renderSpace->y0 + renderSpace->h,
                                     0.0f);
    glEnd();

    if (renderSpace->fullCoordinateSpace) {
        glLineWidth(1.0f);
        neogeo_sprite_3d_colorFromUint(neogeo_sprite_3d_color(130, 130, 130), 1.0f);
        glBegin(GL_LINE_LOOP);
        neogeo_sprite_3d_vertexProjected(renderSpace, renderSpace->x0, renderSpace->y0, 0.0f);
        neogeo_sprite_3d_vertexProjected(renderSpace,
                                         renderSpace->x0 + renderSpace->w,
                                         renderSpace->y0,
                                         0.0f);
        neogeo_sprite_3d_vertexProjected(renderSpace,
                                         renderSpace->x0 + renderSpace->w,
                                         renderSpace->y0 + renderSpace->h,
                                         0.0f);
        neogeo_sprite_3d_vertexProjected(renderSpace,
                                         renderSpace->x0,
                                         renderSpace->y0 + renderSpace->h,
                                         0.0f);
        glEnd();
    }

    glLineWidth(2.0f);
    neogeo_sprite_3d_colorFromUint(neogeo_sprite_3d_color(230, 230, 230), 1.0f);
    glBegin(GL_LINE_LOOP);
    neogeo_sprite_3d_vertexProjected(renderSpace, NEOGEO_SPRITE_DECODE_VISIBLE_X0, NEOGEO_SPRITE_DECODE_VISIBLE_Y0, 0.0f);
    neogeo_sprite_3d_vertexProjected(renderSpace,
                                     NEOGEO_SPRITE_DECODE_VISIBLE_X0 + NEOGEO_SPRITE_DECODE_VISIBLE_W,
                                     NEOGEO_SPRITE_DECODE_VISIBLE_Y0,
                                     0.0f);
    neogeo_sprite_3d_vertexProjected(renderSpace,
                                     NEOGEO_SPRITE_DECODE_VISIBLE_X0 + NEOGEO_SPRITE_DECODE_VISIBLE_W,
                                     NEOGEO_SPRITE_DECODE_VISIBLE_Y0 + NEOGEO_SPRITE_DECODE_VISIBLE_H,
                                     0.0f);
    neogeo_sprite_3d_vertexProjected(renderSpace,
                                     NEOGEO_SPRITE_DECODE_VISIBLE_X0,
                                     NEOGEO_SPRITE_DECODE_VISIBLE_Y0 + NEOGEO_SPRITE_DECODE_VISIBLE_H,
                                     0.0f);
    glEnd();
}

static void
neogeo_sprite_3d_emitLine2d(const neogeo_sprite_3d_render_space_t *renderSpace,
                            int x0,
                            int y0,
                            int x1,
                            int y1,
                            float z,
                            uint32_t color,
                            float alpha)
{
    neogeo_sprite_3d_colorFromUint(color, alpha);
    neogeo_sprite_3d_vertexProjected(renderSpace, (float)x0, (float)y0, z);
    neogeo_sprite_3d_vertexProjected(renderSpace, (float)x1, (float)y1, z);
}

static void
neogeo_sprite_3d_emitLine2dWithVisibilityAlpha(const neogeo_sprite_3d_render_space_t *renderSpace,
                                               int x0,
                                               int y0,
                                               int x1,
                                               int y1,
                                               float z,
                                               uint32_t color,
                                               float alpha)
{
    int visibleX0 = NEOGEO_SPRITE_DECODE_VISIBLE_X0;
    int visibleX1 = NEOGEO_SPRITE_DECODE_VISIBLE_X0 + NEOGEO_SPRITE_DECODE_VISIBLE_W;
    int drawX = x0;

    if (renderSpace->fullCoordinateSpace &&
        y0 == y1 &&
        y0 >= NEOGEO_SPRITE_DECODE_VISIBLE_Y0 &&
        y0 < (NEOGEO_SPRITE_DECODE_VISIBLE_Y0 + NEOGEO_SPRITE_DECODE_VISIBLE_H) &&
        x0 < visibleX1 &&
        x1 > visibleX0) {
        if (drawX < visibleX0) {
            neogeo_sprite_3d_emitLine2d(renderSpace,
                                        drawX,
                                        y0,
                                        visibleX0,
                                        y1,
                                        z,
                                        color,
                                        alpha * 0.5f);
            drawX = visibleX0;
        }
        if (drawX < visibleX1 && drawX < x1) {
            int clippedX1 = x1 < visibleX1 ? x1 : visibleX1;
            neogeo_sprite_3d_emitLine2d(renderSpace,
                                        drawX,
                                        y0,
                                        clippedX1,
                                        y1,
                                        z,
                                        color,
                                        alpha);
            drawX = clippedX1;
        }
        if (drawX < x1) {
            neogeo_sprite_3d_emitLine2d(renderSpace,
                                        drawX,
                                        y0,
                                        x1,
                                        y1,
                                        z,
                                        color,
                                        alpha * 0.5f);
        }
        return;
    }
    neogeo_sprite_3d_emitLine2d(renderSpace,
                                x0,
                                y0,
                                x1,
                                y1,
                                z,
                                color,
                                neogeo_sprite_3d_alphaForPoint(renderSpace, x0, y0, alpha));
}

static uint32_t
neogeo_sprite_3d_paletteEntryColor(const e9k_debug_palette_state_t *paletteState,
                                   unsigned paletteBank,
                                   unsigned paletteIndex)
{
    uint32_t paletteOffset = 0u;

    if (paletteState && paletteState->colors && paletteState->color_count > 0u) {
        paletteOffset = paletteState->active_bank * 4096u + (paletteBank << 4);
        if ((size_t)paletteOffset + (size_t)paletteIndex < paletteState->color_count) {
            return paletteState->colors[paletteOffset + paletteIndex];
        }
    }
    return neogeo_memview_amberColor(paletteIndex);
}

static int
neogeo_sprite_3d_getL0Rom(e9k_debug_rom_region_t *out)
{
    e9k_debug_rom_entry_t entries[NEOGEO_SPRITE_3D_ROM_ENTRY_MAX];
    size_t count = 0u;

    memset(out, 0, sizeof(*out));
    memset(entries, 0, sizeof(entries));
    count = libretro_host_neogeo_getRoms(entries, NEOGEO_SPRITE_3D_ROM_ENTRY_MAX);
    for (size_t i = 0u; i < count; ++i) {
        if (strcmp(entries[i].label, "L0") == 0 && entries[i].data && entries[i].size >= NEOGEO_SPRITE_3D_L0_SIZE) {
            out->data = entries[i].data;
            out->size = entries[i].size;
            return 1;
        }
    }
    return 0;
}

static unsigned
neogeo_sprite_3d_mapVerticalShrinkRow(const e9k_debug_rom_region_t *l0,
                                      unsigned srow,
                                      unsigned sprsize,
                                      unsigned vshrink)
{
    unsigned invert = srow > 0xffu;
    unsigned zrow = srow & 0xffu;

    if (!l0 || !l0->data || l0->size < NEOGEO_SPRITE_3D_L0_SIZE) {
        return srow;
    }
    if (invert) {
        zrow ^= 0xffu;
    }
    if (sprsize == 33u) {
        zrow = zrow % ((vshrink + 1u) << 1u);
        if (zrow > vshrink) {
            zrow = ((vshrink + 1u) << 1u) - 1u - zrow;
            invert ^= 1u;
        }
    }
    srow = l0->data[(vshrink << 8u) + zrow];
    if (invert) {
        srow ^= 0x1ffu;
    }
    return srow;
}

static uint32_t
neogeo_sprite_3d_applyAutoAnimation(const e9k_debug_sprite_state_t *st,
                                    uint32_t tileNum,
                                    uint16_t oddWord)
{
    unsigned animBits = (unsigned)((oddWord & NEOGEO_SPRITE_3D_SPRITE_ANIM_MASK) >>
                                   NEOGEO_SPRITE_3D_SPRITE_ANIM_SHIFT);

    if (st->autoAnimationDisabled) {
        return tileNum;
    }
    switch (animBits) {
        case 1u: {
            tileNum &= ~0x03u;
            tileNum |= st->autoAnimationCounter & 0x03u;
            break;
        }
        case 2u:
        case 3u: {
            tileNum &= ~0x07u;
            tileNum |= st->autoAnimationCounter & 0x07u;
            break;
        }
        default: {
            break;
        }
    }
    return tileNum;
}

static void
neogeo_sprite_3d_emitPixelRunQuad(const neogeo_sprite_3d_render_space_t *renderSpace,
                                  int x,
                                  int y,
                                  int w,
                                  float z,
                                  uint32_t color,
                                  float alpha)
{
    float fx = (float)x;
    float fy = (float)y;
    float fw = (float)w;

    if (w <= 0) {
        return;
    }
    neogeo_sprite_3d_colorFromUint(color, alpha);
    neogeo_sprite_3d_vertexProjected(renderSpace, fx, fy, z);
    neogeo_sprite_3d_vertexProjected(renderSpace, fx + fw, fy, z);
    neogeo_sprite_3d_vertexProjected(renderSpace, fx + fw, fy + 1.0f, z);
    neogeo_sprite_3d_vertexProjected(renderSpace, fx, fy + 1.0f, z);
}

static void
neogeo_sprite_3d_emitPixelRunQuadWithVisibilityAlpha(const neogeo_sprite_3d_render_space_t *renderSpace,
                                                     int x,
                                                     int y,
                                                     int w,
                                                     float z,
                                                     uint32_t color,
                                                     float alpha)
{
    int visibleX1 = NEOGEO_SPRITE_DECODE_VISIBLE_X0 + NEOGEO_SPRITE_DECODE_VISIBLE_W;
    int drawX = x;
    int x1 = x + w;

    if (!renderSpace->fullCoordinateSpace) {
        neogeo_sprite_3d_emitPixelRunQuad(renderSpace, x, y, w, z, color, alpha);
        return;
    }
    if (y < NEOGEO_SPRITE_DECODE_VISIBLE_Y0 || y >= (NEOGEO_SPRITE_DECODE_VISIBLE_Y0 + NEOGEO_SPRITE_DECODE_VISIBLE_H)) {
        neogeo_sprite_3d_emitPixelRunQuad(renderSpace, x, y, w, z, color, alpha * 0.5f);
        return;
    }
    if (x >= visibleX1 || x1 <= NEOGEO_SPRITE_DECODE_VISIBLE_X0) {
        neogeo_sprite_3d_emitPixelRunQuad(renderSpace, x, y, w, z, color, alpha * 0.5f);
        return;
    }
    if (drawX < NEOGEO_SPRITE_DECODE_VISIBLE_X0) {
        neogeo_sprite_3d_emitPixelRunQuad(renderSpace,
                                          drawX,
                                          y,
                                          NEOGEO_SPRITE_DECODE_VISIBLE_X0 - drawX,
                                          z,
                                          color,
                                          alpha * 0.5f);
        drawX = NEOGEO_SPRITE_DECODE_VISIBLE_X0;
    }
    if (drawX < visibleX1 && drawX < x1) {
        int clippedX1 = x1 < visibleX1 ? x1 : visibleX1;
        neogeo_sprite_3d_emitPixelRunQuad(renderSpace,
                                          drawX,
                                          y,
                                          clippedX1 - drawX,
                                          z,
                                          color,
                                          alpha);
        drawX = clippedX1;
    }
    if (drawX < x1) {
        neogeo_sprite_3d_emitPixelRunQuad(renderSpace,
                                          drawX,
                                          y,
                                          x1 - drawX,
                                          z,
                                          color,
                                          alpha * 0.5f);
    }
}

static void
neogeo_sprite_3d_emitPixelRunQuadWrappedX(const neogeo_sprite_3d_render_space_t *renderSpace,
                                          int x,
                                          int y,
                                          int w,
                                          float z,
                                          uint32_t color,
                                          float alpha)
{
    int wrappedX = x - NEOGEO_SPRITE_DECODE_COORD_SIZE;

    neogeo_sprite_3d_emitPixelRunQuadWithVisibilityAlpha(renderSpace, x, y, w, z, color, alpha);
    if (!renderSpace->fullCoordinateSpace) {
        return;
    }
    if (wrappedX + w <= renderSpace->x0 ||
        wrappedX >= renderSpace->x0 + renderSpace->w) {
        return;
    }
    neogeo_sprite_3d_emitPixelRunQuadWithVisibilityAlpha(renderSpace,
                                                         wrappedX,
                                                         y,
                                                         w,
                                                         z,
                                                         color,
                                                         alpha);
}

static void
neogeo_sprite_3d_drawSpriteContentLine(const e9k_debug_sprite_state_t *st,
                                       const neogeo_sprite_3d_render_space_t *renderSpace,
                                       const neogeo_sprite_3d_decoded_sprite_t *sprite,
                                       unsigned spriteIndex,
                                       int visibleLine,
                                       float z,
                                       const e9k_debug_rom_region_t *crom,
                                       const e9k_debug_rom_region_t *l0,
                                       const e9k_debug_palette_state_t *paletteState)
{
    const uint16_t *vram = st->vram;
    unsigned srow = neogeo_sprite_decode_visibleLineSpriteRow(st, sprite, visibleLine);
    unsigned tileWordOffset = 0u;
    uint16_t evenWord = 0u;
    uint16_t oddWord = 0u;
    uint32_t cromTiles = (uint32_t)(crom->size / NEOGEO_SPRITE_3D_CROM_TILE_BYTES);
    uint32_t tileNum = 0u;
    uint32_t tileBaseAddr = 0u;
    unsigned tileY = 0u;
    unsigned hflip = 0u;
    unsigned vflip = 0u;
    unsigned paletteBank = 0u;
    unsigned drawpos = 0u;
    int runActive = 0;
    int runX = 0;
    int runW = 0;
    uint32_t runColor = 0u;
    float contentAlpha = neogeo_sprite_3dState.contentOpacityPercent / 100.0f;

    srow = neogeo_sprite_3d_mapVerticalShrinkRow(l0, srow, sprite->sprsize, sprite->vshrink);
    tileWordOffset = spriteIndex * NEOGEO_SPRITE_3D_SPRITE_VRAM_WORDS_PER_SPRITE +
        ((srow >> 3u) & 0x3eu);

    if (tileWordOffset + 1u >= st->vram_words || cromTiles == 0u) {
        return;
    }

    evenWord = vram[tileWordOffset];
    oddWord = vram[tileWordOffset + 1u];
    tileNum = (uint32_t)(evenWord |
                         ((oddWord & NEOGEO_SPRITE_3D_SPRITE_TILE_HIGH_MASK) <<
                          NEOGEO_SPRITE_3D_SPRITE_TILE_HIGH_SHIFT));
    tileNum = neogeo_sprite_3d_applyAutoAnimation(st, tileNum, oddWord);
    tileNum %= cromTiles;
    tileBaseAddr = tileNum * NEOGEO_SPRITE_3D_CROM_TILE_BYTES;
    hflip = oddWord & NEOGEO_SPRITE_3D_SPRITE_HFLIP;
    vflip = oddWord & NEOGEO_SPRITE_3D_SPRITE_VFLIP;
    paletteBank = (unsigned)((oddWord >> NEOGEO_SPRITE_DECODE_SPRITE_PALETTE_SHIFT) &
                             NEOGEO_SPRITE_DECODE_SPRITE_PALETTE_MASK);
    tileY = vflip ? (NEOGEO_SPRITE_3D_TILE_H - 1u - (srow & 0x0fu)) : (srow & 0x0fu);

    for (unsigned pixel = 0u; pixel < NEOGEO_SPRITE_3D_TILE_W; ++pixel) {
        unsigned tileX = hflip ? (NEOGEO_SPRITE_3D_TILE_W - 1u - pixel) : pixel;
        unsigned paletteIndex = 0u;
        unsigned xcoord = 0u;
        int drawX = 0;
        uint32_t color = 0u;

        if (!neogeo_sprite_decode_hshrinkPixelVisible(sprite->hshrink, pixel)) {
            continue;
        }
        paletteIndex = neogeo_memview_readCromPixel(crom->data,
                                                    crom->size,
                                                    tileBaseAddr,
                                                    tileX,
                                                    tileY);
        xcoord = (sprite->xpos + drawpos) & NEOGEO_SPRITE_DECODE_WRAP_MASK;
        drawX = (int)xcoord;
        drawpos++;
        if (paletteIndex == 0u ||
            drawX < renderSpace->x0 ||
            drawX >= (renderSpace->x0 + renderSpace->w)) {
            if (runActive) {
                neogeo_sprite_3d_emitPixelRunQuadWrappedX(renderSpace,
                                                          runX,
                                                          visibleLine,
                                                          runW,
                                                          z,
                                                          runColor,
                                                          contentAlpha);
                runActive = 0;
            }
            continue;
        }
        color = neogeo_sprite_3d_paletteEntryColor(paletteState,
                                                   paletteBank,
                                                   paletteIndex);
        if (runActive && drawX == runX + runW && color == runColor) {
            runW++;
            continue;
        }
        if (runActive) {
            neogeo_sprite_3d_emitPixelRunQuadWrappedX(renderSpace,
                                                      runX,
                                                      visibleLine,
                                                      runW,
                                                      z,
                                                      runColor,
                                                      contentAlpha);
        }
        runActive = 1;
        runX = drawX;
        runW = 1;
        runColor = color;
    }
    if (runActive) {
        neogeo_sprite_3d_emitPixelRunQuadWrappedX(renderSpace,
                                                  runX,
                                                  visibleLine,
                                                  runW,
                                                  z,
                                                  runColor,
                                                  contentAlpha);
    }
}

static void
neogeo_sprite_3d_drawSpriteContents(const e9k_debug_sprite_state_t *st,
                                    const neogeo_sprite_3d_render_space_t *renderSpace,
                                    const neogeo_sprite_3d_decoded_sprite_t *decodedSprites,
                                    neogeo_sprite_3d_line_sprites_t *lineSprites,
                                    const int *chainLayer,
                                    int chainLayerCount)
{
    e9k_debug_rom_region_t crom;
    e9k_debug_rom_region_t l0;
    e9k_debug_palette_state_t paletteState;
    int havePalette = 0;
    GLboolean depthMask = GL_TRUE;
    GLboolean depthTestEnabled = GL_FALSE;

    memset(&crom, 0, sizeof(crom));
    memset(&l0, 0, sizeof(l0));
    memset(&paletteState, 0, sizeof(paletteState));
    if (!libretro_host_neogeo_getCRom(&crom) ||
        !crom.data ||
        crom.size < NEOGEO_SPRITE_3D_CROM_TILE_BYTES) {
        return;
    }
    (void)neogeo_sprite_3d_getL0Rom(&l0);
    havePalette = libretro_host_neogeo_getPaletteState(&paletteState) &&
                  paletteState.colors &&
                  paletteState.color_count > 0u;

    depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    for (int layer = 0; layer < chainLayerCount; ++layer) {
        glBegin(GL_QUADS);
        for (int line = renderSpace->y0; line < (renderSpace->y0 + renderSpace->h); ++line) {
            neogeo_sprite_3d_line_sprites_t *lineList =
                &lineSprites[((unsigned)line) & NEOGEO_SPRITE_DECODE_WRAP_MASK];

            for (unsigned lineSpriteIndex = 0; lineSpriteIndex < lineList->count; ++lineSpriteIndex) {
                unsigned spriteIndex = (unsigned)lineList->indices[lineSpriteIndex];
                const neogeo_sprite_3d_decoded_sprite_t *sprite = &decodedSprites[spriteIndex];
                float z = 0.0f;

                if (sprite->width <= 0 || chainLayer[sprite->chainRootIndex] != layer) {
                    continue;
                }
                z = (float)layer * neogeo_sprite_3dState.zSeparation;
                neogeo_sprite_3d_drawSpriteContentLine(st,
                                                       renderSpace,
                                                       sprite,
                                                       spriteIndex,
                                                       line,
                                                       z,
                                                       &crom,
                                                       &l0,
                                                       havePalette ? &paletteState : NULL);
            }
        }
        glEnd();
    }

    glDepthMask(depthMask);
    if (depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    }
}

static void
neogeo_sprite_3d_buildSpriteXSegments(const neogeo_sprite_3d_render_space_t *renderSpace,
                                      const neogeo_sprite_3d_decoded_sprite_t *sprite,
                                      int *segmentX0,
                                      int *segmentX1,
                                      int *segmentCount)
{
    int x0 = (int)(sprite->xpos & NEOGEO_SPRITE_DECODE_WRAP_MASK);
    int x1 = x0 + sprite->width;
    int count = 0;

    segmentX0[count] = x0;
    segmentX1[count] = x1;
    count++;

    if (renderSpace->fullCoordinateSpace) {
        segmentX0[count] = x0 - NEOGEO_SPRITE_DECODE_COORD_SIZE;
        segmentX1[count] = x1 - NEOGEO_SPRITE_DECODE_COORD_SIZE;
        count++;
    } else if (x1 > NEOGEO_SPRITE_DECODE_COORD_SIZE) {
        segmentX0[count] = 0;
        segmentX1[count] = x1 - NEOGEO_SPRITE_DECODE_COORD_SIZE;
        count++;
    }
    *segmentCount = count;
}

static void
neogeo_sprite_3d_markSpriteSegment(const neogeo_sprite_3d_render_space_t *renderSpace,
                                   const neogeo_sprite_3d_decoded_sprite_t *sprite,
                                   uint8_t *spriteVisible)
{
    int segmentX0[2] = { 0, 0 };
    int segmentX1[2] = { 0, 0 };
    int segmentCount = 0;

    neogeo_sprite_3d_buildSpriteXSegments(renderSpace,
                                          sprite,
                                          segmentX0,
                                          segmentX1,
                                          &segmentCount);

    for (int i = 0; i < segmentCount; ++i) {
        int clipX0 = segmentX0[i] > renderSpace->x0 ? segmentX0[i] : renderSpace->x0;
        int clipX1 = segmentX1[i] < (renderSpace->x0 + renderSpace->w) ?
            segmentX1[i] : (renderSpace->x0 + renderSpace->w);
        if (clipX1 <= clipX0) {
            continue;
        }
        spriteVisible[i] = 1u;
    }
}

static void
neogeo_sprite_3d_emitSpriteOutlineLine(const e9k_debug_sprite_state_t *st,
                                       const neogeo_sprite_3d_render_space_t *renderSpace,
                                       const neogeo_sprite_3d_decoded_sprite_t *sprite,
                                       int visibleY,
                                       float z,
                                       uint32_t color,
                                       int selected)
{
    int w = sprite->width;
    int segmentX0[2] = { 0, 0 };
    int segmentX1[2] = { 0, 0 };
    int segmentCount = 0;
    unsigned srow = neogeo_sprite_decode_visibleLineSpriteRow(st, sprite, visibleY);
    unsigned totalH = sprite->sprsize << 4u;
    int y = visibleY;

    if (w <= 0 || totalH == 0u || srow >= totalH) {
        return;
    }
    neogeo_sprite_3d_buildSpriteXSegments(renderSpace,
                                          sprite,
                                          segmentX0,
                                          segmentX1,
                                          &segmentCount);
    for (int i = 0; i < segmentCount; ++i) {
        int clipX0 = segmentX0[i] > renderSpace->x0 ? segmentX0[i] : renderSpace->x0;
        int clipX1 = segmentX1[i] < (renderSpace->x0 + renderSpace->w) ?
            segmentX1[i] : (renderSpace->x0 + renderSpace->w);

        if (clipX1 <= clipX0) {
            continue;
        }
        if (clipX0 == segmentX0[i]) {
            neogeo_sprite_3d_emitLine2dWithVisibilityAlpha(renderSpace,
                                                           clipX0,
                                                           y,
                                                           clipX0,
                                                           y + 1,
                                                           z,
                                                           color,
                                                           selected ? 1.0f : 0.86f);
        }
        if (clipX1 == segmentX1[i]) {
            neogeo_sprite_3d_emitLine2dWithVisibilityAlpha(renderSpace,
                                                           clipX1,
                                                           y,
                                                           clipX1,
                                                           y + 1,
                                                           z,
                                                           color,
                                                           selected ? 1.0f : 0.86f);
        }
        if (srow == 0u || srow + 1u == totalH) {
            neogeo_sprite_3d_emitLine2dWithVisibilityAlpha(renderSpace,
                                                           clipX0,
                                                           y,
                                                           clipX1,
                                                           y,
                                                           z,
                                                           color,
                                                           selected ? 1.0f : 0.86f);
        }
    }
}

static int
neogeo_sprite_3d_buildVisibleChainLayers(const neogeo_sprite_3d_decoded_sprite_t *decodedSprites,
                                                  uint8_t segmentVisible[NEOGEO_SPRITE_DECODE_MAX_SPRITES][2],
                                                  int *chainLayer)
{
    int chainLayerCount = 0;

    for (int i = 0; i < NEOGEO_SPRITE_DECODE_MAX_SPRITES; ++i) {
        chainLayer[i] = -1;
    }

    for (int i = 0; i < NEOGEO_SPRITE_DECODE_MAX_SPRITES; ++i) {
        const neogeo_sprite_3d_decoded_sprite_t *sprite = &decodedSprites[i];

        if (!segmentVisible[i][0] && !segmentVisible[i][1]) {
            continue;
        }
        if (chainLayer[sprite->chainRootIndex] >= 0) {
            continue;
        }
        chainLayer[sprite->chainRootIndex] = chainLayerCount;
        chainLayerCount++;
    }
    return chainLayerCount;
}

static void
neogeo_sprite_3d_drawSpriteOutlines(const e9k_debug_sprite_state_t *st,
                                    const neogeo_sprite_3d_render_space_t *renderSpace,
                                    const neogeo_sprite_3d_decoded_sprite_t *decodedSprites,
                                    neogeo_sprite_3d_line_sprites_t *lineSprites,
                                    const uint8_t *chainHasAnimBits,
                                    const int *chainLayer,
                                    int selectedOnly)
{
    uint32_t colAnim = neogeo_sprite_3d_color(255, 192, 0);
    uint32_t colGreen = neogeo_sprite_3d_color(0, 255, 0);
    uint32_t colSelected = neogeo_sprite_3d_color(255, 255, 0);

    glLineWidth(selectedOnly ? 3.0f : 1.0f);
    glBegin(GL_LINES);
    for (int line = renderSpace->y0; line < (renderSpace->y0 + renderSpace->h); ++line) {
        neogeo_sprite_3d_line_sprites_t *lineList =
            &lineSprites[((unsigned)line) & NEOGEO_SPRITE_DECODE_WRAP_MASK];
        for (unsigned lineSpriteIndex = 0; lineSpriteIndex < lineList->count; ++lineSpriteIndex) {
            unsigned spriteIndex = (unsigned)lineList->indices[lineSpriteIndex];
            const neogeo_sprite_3d_decoded_sprite_t *sprite = &decodedSprites[spriteIndex];
            uint32_t color = chainHasAnimBits[sprite->chainRootIndex] ? colAnim : colGreen;
            int selected = 0;
            float z = 0.0f;

            if (sprite->width <= 0 || chainLayer[sprite->chainRootIndex] < 0) {
                continue;
            }
            if (neogeo_sprite_3dSource.selectedSpriteIndex >= 0) {
                if (neogeo_sprite_3dSource.highlightSelectionChain) {
                    selected = (sprite->chainRootIndex == (unsigned)neogeo_sprite_3dSource.selectedChainRootIndex) ? 1 : 0;
                } else {
                    selected = (spriteIndex == (unsigned)neogeo_sprite_3dSource.selectedSpriteIndex) ? 1 : 0;
                }
            }
            if (selectedOnly != selected) {
                continue;
            }
            z = (float)chainLayer[sprite->chainRootIndex] * neogeo_sprite_3dState.zSeparation;

            if (neogeo_sprite_3dSource.viewMode == neogeo_sprite_3d_view_mode_shrink) {
                color = neogeo_sprite_3d_shrinkRgbColor(sprite->hshrink, sprite->vshrink);
            } else if (neogeo_sprite_3dSource.viewMode == neogeo_sprite_3d_view_mode_palette) {
                color = neogeo_sprite_3d_paletteColor(sprite->paletteBank);
            } else if (neogeo_sprite_3dSource.viewMode == neogeo_sprite_3d_view_mode_chain) {
                color = neogeo_sprite_3d_chainColor(sprite->chainRootIndex);
            }

            if (selected) {
                color = colSelected;
            }
            neogeo_sprite_3d_emitSpriteOutlineLine(st,
                                                   renderSpace,
                                                   sprite,
                                                   line,
                                                   z,
                                                   color,
                                                   selected);
        }
    }
    glEnd();
}

static void
neogeo_sprite_3d_drawSprites(void)
{
    neogeo_sprite_3d_decoded_sprite_t decodedSprites[NEOGEO_SPRITE_DECODE_MAX_SPRITES];
    neogeo_sprite_3d_line_sprites_t lineSprites[NEOGEO_SPRITE_3D_LINE_COUNT];
    uint8_t chainHasAnimBits[NEOGEO_SPRITE_DECODE_MAX_SPRITES];
    uint8_t segmentVisible[NEOGEO_SPRITE_DECODE_MAX_SPRITES][2];
    int chainLayer[NEOGEO_SPRITE_DECODE_MAX_SPRITES];
    int chainLayerCount = 0;
    neogeo_sprite_3d_render_space_t renderSpace = neogeo_sprite_3d_makeRenderSpace();

    if (!neogeo_sprite_3dSource.hasLastState ||
        !neogeo_sprite_3dSource.lastState ||
        !neogeo_sprite_3dSource.lastState->vram) {
        return;
    }

    unsigned sprlimit = neogeo_sprite_3dSource.lastState->sprlimit ?
        neogeo_sprite_3dSource.lastState->sprlimit : NEOGEO_SPRITE_DECODE_SPRITES_PER_LINE_MAX;

    if (!neogeo_sprite_decode_decodeSprites(neogeo_sprite_3dSource.lastState,
                                            decodedSprites,
                                            lineSprites,
                                            chainHasAnimBits,
                                            sprlimit)) {
        return;
    }

    memset(segmentVisible, 0, sizeof(segmentVisible));

    for (int line = renderSpace.y0; line < (renderSpace.y0 + renderSpace.h); ++line) {
        neogeo_sprite_3d_line_sprites_t *lineList =
            &lineSprites[((unsigned)line) & NEOGEO_SPRITE_DECODE_WRAP_MASK];
        for (unsigned lineSpriteIndex = 0; lineSpriteIndex < lineList->count; ++lineSpriteIndex) {
            unsigned spriteIndex = (unsigned)lineList->indices[lineSpriteIndex];
            const neogeo_sprite_3d_decoded_sprite_t *sprite = &decodedSprites[spriteIndex];
            if (sprite->width <= 0) {
                continue;
            }
            neogeo_sprite_3d_markSpriteSegment(&renderSpace,
                                               sprite,
                                               segmentVisible[spriteIndex]);
        }
    }

    chainLayerCount = neogeo_sprite_3d_buildVisibleChainLayers(decodedSprites,
                                                               segmentVisible,
                                                               chainLayer);

    if (neogeo_sprite_3dState.showContent) {
        neogeo_sprite_3d_drawSpriteContents(neogeo_sprite_3dSource.lastState,
                                            &renderSpace,
                                            decodedSprites,
                                            lineSprites,
                                            chainLayer,
                                            chainLayerCount);
    }
    if (!neogeo_sprite_3dState.showOutlines) {
        return;
    }

    neogeo_sprite_3d_drawSpriteOutlines(neogeo_sprite_3dSource.lastState,
                                        &renderSpace,
                                        decodedSprites,
                                        lineSprites,
                                        chainHasAnimBits,
                                        chainLayer,
                                        0);
    neogeo_sprite_3d_drawSpriteOutlines(neogeo_sprite_3dSource.lastState,
                                        &renderSpace,
                                        decodedSprites,
                                        lineSprites,
                                        chainHasAnimBits,
                                        chainLayer,
                                        1);
}

static int
neogeo_sprite_3d_renderOpenGl(e9ui_component_t *self, e9ui_context_t *ctx)
{
    int outputW = 0;
    int outputH = 0;
    GLint prevViewport[4] = { 0, 0, 0, 0 };
    GLint prevProgram = 0;
    float aspect = 1.0f;
    float viewH = 0.0f;
    float viewW = 0.0f;
    int scissorX = 0;
    int scissorY = 0;
    int viewportY = 0;
    SDL_Rect bodyRect;
    SDL_Rect clipRect;
    SDL_Rect visibleRect;
    SDL_bool clipEnabled;
    neogeo_sprite_3d_render_space_t renderSpace = neogeo_sprite_3d_makeRenderSpace();

    if (!SDL_GL_GetCurrentContext()) {
        return 0;
    }
    if (self->bounds.w <= 0 || self->bounds.h <= 0) {
        return 0;
    }
    SDL_GetRendererOutputSize(ctx->renderer, &outputW, &outputH);
    if (outputW <= 0 || outputH <= 0) {
        return 0;
    }
    bodyRect = (SDL_Rect){ self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    clipEnabled = SDL_RenderIsClipEnabled(ctx->renderer);
    if (clipEnabled) {
        SDL_RenderGetClipRect(ctx->renderer, &clipRect);
    } else {
        clipRect = (SDL_Rect){ 0, 0, outputW, outputH };
    }
    if (!SDL_IntersectRect(&bodyRect, &clipRect, &visibleRect)) {
        return 1;
    }

    if (!neogeo_sprite_3d_glUseProgram) {
        neogeo_sprite_3d_glUseProgram =
            (neogeo_sprite_3d_gl_use_program_fn)SDL_GL_GetProcAddress("glUseProgram");
    }

    SDL_RenderFlush(ctx->renderer);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
#ifdef GL_CURRENT_PROGRAM
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
#endif
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glMatrixMode(GL_TEXTURE);
    glPushMatrix();

    if (neogeo_sprite_3d_glUseProgram) {
        neogeo_sprite_3d_glUseProgram(0);
    }

    scissorX = visibleRect.x;
    scissorY = outputH - (visibleRect.y + visibleRect.h);
    viewportY = outputH - (self->bounds.y + self->bounds.h);
    glViewport(self->bounds.x, viewportY, self->bounds.w, self->bounds.h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(scissorX, scissorY, visibleRect.w, visibleRect.h);
    glClearColor(0.03f, 0.04f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glShadeModel(GL_SMOOTH);

    aspect = (float)self->bounds.w / (float)self->bounds.h;
    viewH = ((float)renderSpace.h + 220.0f) * (100.0f / neogeo_sprite_3dState.zoomPercent);
    viewW = viewH * aspect;
    if (renderSpace.fullCoordinateSpace) {
        float minViewW = ((float)renderSpace.w + 220.0f) *
            (100.0f / neogeo_sprite_3dState.zoomPercent);
        if (viewW < minViewW) {
            viewW = minViewW;
            viewH = viewW / aspect;
        }
    }
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-viewW * 0.5f, viewW * 0.5f,
            -viewH * 0.5f, viewH * 0.5f,
            -5000.0f, 5000.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    neogeo_sprite_3d_drawPlane(&renderSpace);
    neogeo_sprite_3d_drawSprites();

    glMatrixMode(GL_TEXTURE);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glPopAttrib();
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (neogeo_sprite_3d_glUseProgram) {
        neogeo_sprite_3d_glUseProgram((GLuint)prevProgram);
    }
    return 1;
}
#endif

static void
neogeo_sprite_3d_bodyRender(e9ui_component_t *self, e9ui_context_t *ctx)
{
    int rendered = 0;

#if E9K_ENABLE_GL_COMPOSITE
    rendered = neogeo_sprite_3d_renderOpenGl(self, ctx);
#endif
    if (!rendered) {
        SDL_Rect rect = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
        SDL_SetRenderDrawColor(ctx->renderer, 18, 20, 26, 255);
        SDL_RenderFillRect(ctx->renderer, &rect);
        SDL_SetRenderDrawColor(ctx->renderer, 120, 80, 32, 255);
        SDL_RenderDrawLine(ctx->renderer, rect.x, rect.y, rect.x + rect.w, rect.y + rect.h);
        SDL_RenderDrawLine(ctx->renderer, rect.x + rect.w, rect.y, rect.x, rect.y + rect.h);
    }
}

static e9ui_component_t *
neogeo_sprite_3d_makeBody(void)
{
    e9ui_component_t *body = (e9ui_component_t *)alloc_calloc(1, sizeof(*body));
    if (!body) {
        return NULL;
    }
    body->name = "neogeo_sprite_3d_body";
    body->layout = neogeo_sprite_3d_bodyLayout;
    body->render = neogeo_sprite_3d_bodyRender;
    return body;
}

static e9ui_component_t *
neogeo_sprite_3d_makeSlider(const char *label,
                            neogeo_sprite_3d_slider_binding_t *binding)
{
    e9ui_component_t *bar = NULL;
    e9ui_component_t *slider = e9ui_slider_make(label, 78, 8, 3, 12, 8, &bar);
    if (!slider) {
        return NULL;
    }
    if (bar) {
        e9ui_seek_bar_setPercent(bar,
                                 neogeo_sprite_3d_sliderPercent(neogeo_sprite_3d_getSliderValue(binding),
                                                                binding));
        e9ui_seek_bar_setCallback(bar, neogeo_sprite_3d_sliderChanged, binding);
        e9ui_seek_bar_setTooltipCallback(bar, neogeo_sprite_3d_sliderTooltip, binding);
    }
    return slider;
}

static e9ui_component_t *
neogeo_sprite_3d_makeControls(void)
{
    e9ui_component_t *controls = e9ui_stack_makeVertical();
    if (!controls) {
        return NULL;
    }

    e9ui_component_t *checkboxRow = e9ui_hstack_make();
    if (checkboxRow) {
        int gap = e9ui_scale_px(&e9ui->ctx, 8);
        int outlinesW = 96;
        int contentW = 96;
        int clipW = 72;
        e9ui_component_t *checkbox = e9ui_checkbox_make("Outlines",
                                                        neogeo_sprite_3dState.showOutlines,
                                                        neogeo_sprite_3d_checkboxChanged,
                                                        &neogeo_sprite_3d_checkboxOutlines);
        if (checkbox) {
            e9ui_checkbox_measure(checkbox, &e9ui->ctx, &outlinesW, NULL);
            e9ui_hstack_addFixed(checkboxRow, checkbox, outlinesW);
        }
        e9ui_hstack_addFixed(checkboxRow, e9ui_spacer_make(gap), gap);
        checkbox = e9ui_checkbox_make("Content",
                                      neogeo_sprite_3dState.showContent,
                                      neogeo_sprite_3d_checkboxChanged,
                                      &neogeo_sprite_3d_checkboxContent);
        if (checkbox) {
            e9ui_checkbox_measure(checkbox, &e9ui->ctx, &contentW, NULL);
            e9ui_hstack_addFixed(checkboxRow, checkbox, contentW);
        }
        e9ui_hstack_addFixed(checkboxRow, e9ui_spacer_make(gap), gap);
        checkbox = e9ui_checkbox_make("Clip",
                                      neogeo_sprite_3dState.clipToVisibleArea,
                                      neogeo_sprite_3d_checkboxChanged,
                                      &neogeo_sprite_3d_checkboxClipToVisibleArea);
        if (checkbox) {
            e9ui_checkbox_measure(checkbox, &e9ui->ctx, &clipW, NULL);
            e9ui_hstack_addFixed(checkboxRow, checkbox, clipW);
        }
        e9ui_hstack_addFlex(checkboxRow, e9ui_spacer_make(1));
        e9ui_stack_addFixed(controls, checkboxRow);
    }

    e9ui_component_t *slider = neogeo_sprite_3d_makeSlider("Yaw",
                                                           &neogeo_sprite_3d_sliderBindings[0]);
    if (slider) {
        e9ui_stack_addFixed(controls, slider);
    }
    slider = neogeo_sprite_3d_makeSlider("Pitch",
                                         &neogeo_sprite_3d_sliderBindings[1]);
    if (slider) {
        e9ui_stack_addFixed(controls, slider);
    }
    slider = neogeo_sprite_3d_makeSlider("Zoom",
                                         &neogeo_sprite_3d_sliderBindings[2]);
    if (slider) {
        e9ui_stack_addFixed(controls, slider);
    }
    slider = neogeo_sprite_3d_makeSlider("Z Sep",
                                         &neogeo_sprite_3d_sliderBindings[3]);
    if (slider) {
        e9ui_stack_addFixed(controls, slider);
    }
    slider = neogeo_sprite_3d_makeSlider("Opacity",
                                         &neogeo_sprite_3d_sliderBindings[4]);
    if (slider) {
        e9ui_stack_addFixed(controls, slider);
    }
    return controls;
}

static e9ui_component_t *
neogeo_sprite_3d_makeRoot(void)
{
    e9ui_component_t *root = e9ui_stack_makeVertical();
    if (!root) {
        return NULL;
    }

    neogeo_sprite_3dState.controls = neogeo_sprite_3d_makeControls();
    neogeo_sprite_3dState.body = neogeo_sprite_3d_makeBody();
    if (neogeo_sprite_3dState.controls) {
        e9ui_component_t *controlsBox = e9ui_box_make(neogeo_sprite_3dState.controls);
        if (controlsBox) {
            e9ui_box_setPadding(controlsBox, 6);
            e9ui_box_setBorder(controlsBox, E9UI_BORDER_BOTTOM, (SDL_Color){ 70, 70, 70, 255 }, 1);
            e9ui_stack_addFixed(root, controlsBox);
        } else {
            e9ui_stack_addFixed(root, neogeo_sprite_3dState.controls);
        }
    }
    if (neogeo_sprite_3dState.body) {
        neogeo_sprite_3dState.scroll = e9ui_scroll_make(neogeo_sprite_3dState.body);
        if (neogeo_sprite_3dState.scroll) {
            e9ui_scroll_setContentWidthPx(neogeo_sprite_3dState.scroll,
                                          e9ui_scale_px(&e9ui->ctx,
                                                        NEOGEO_SPRITE_3D_SCROLL_CONTENT_W));
            e9ui_scroll_setContentHeightPx(neogeo_sprite_3dState.scroll,
                                           e9ui_scale_px(&e9ui->ctx,
                                                         NEOGEO_SPRITE_3D_SCROLL_CONTENT_H));
            e9ui_scroll_setPersistKey(neogeo_sprite_3dState.scroll,
                                      "comp.sprite_debug.gl_view");
            if (neogeo_sprite_3dState.hasSavedScroll) {
                e9ui_scroll_loadPersistedPx(neogeo_sprite_3dState.scroll,
                                            neogeo_sprite_3dState.savedScrollX,
                                            neogeo_sprite_3dState.savedScrollY);
            }
            e9ui_stack_addFlex(root, neogeo_sprite_3dState.scroll);
        } else {
            e9ui_stack_addFlex(root, neogeo_sprite_3dState.body);
        }
    }
    return root;
}

void
neogeo_sprite_3d_close(void)
{
    if (!neogeo_sprite_3dState.windowState.open) {
        return;
    }
    if (neogeo_sprite_3dState.scroll) {
        e9ui_scroll_getScrollPx(neogeo_sprite_3dState.scroll,
                                &neogeo_sprite_3dState.savedScrollX,
                                &neogeo_sprite_3dState.savedScrollY);
        neogeo_sprite_3dState.hasSavedScroll = 1;
    }
    (void)e9ui_windowCaptureStateRectSnapshot(&neogeo_sprite_3dState.windowState,
                                              &e9ui->ctx);
    config_saveConfig();
    if (neogeo_sprite_3dState.windowState.windowHost) {
        e9ui_windowDestroy(neogeo_sprite_3dState.windowState.windowHost);
        neogeo_sprite_3dState.windowState.windowHost = NULL;
    }
    neogeo_sprite_3dState.root = NULL;
    neogeo_sprite_3dState.controls = NULL;
    neogeo_sprite_3dState.scroll = NULL;
    neogeo_sprite_3dState.body = NULL;
    neogeo_sprite_3dState.windowState.open = 0;
}

static void
neogeo_sprite_3d_windowCloseRequested(e9ui_window_t *window, void *user)
{
    (void)window;
    (void)user;
    neogeo_sprite_3d_close();
}

void
neogeo_sprite_3d_toggle(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    (void)user;

    if (neogeo_sprite_3dState.windowState.open) {
        neogeo_sprite_3d_close();
        return;
    }

    neogeo_sprite_3dState.windowState.windowHost =
        e9ui_windowCreate(neogeo_sprite_3d_windowBackend());
    if (!neogeo_sprite_3dState.windowState.windowHost) {
        return;
    }
    neogeo_sprite_3dState.root = neogeo_sprite_3d_makeRoot();
    if (!neogeo_sprite_3dState.root || !neogeo_sprite_3dState.body) {
        e9ui_windowDestroy(neogeo_sprite_3dState.windowState.windowHost);
        neogeo_sprite_3dState.windowState.windowHost = NULL;
        neogeo_sprite_3dState.root = NULL;
        neogeo_sprite_3dState.controls = NULL;
        neogeo_sprite_3dState.scroll = NULL;
        neogeo_sprite_3dState.body = NULL;
        return;
    }

    e9ui_rect_t rect = e9ui_windowResolveStateOpenRect(&e9ui->ctx,
                                                       neogeo_sprite_3d_windowDefaultRect(&e9ui->ctx),
                                                       &neogeo_sprite_3dState.windowState);
    e9ui_windowOpen(neogeo_sprite_3dState.windowState.windowHost,
                    "SPRITES 3D",
                    rect,
                    neogeo_sprite_3dState.root,
                    neogeo_sprite_3d_windowCloseRequested,
                    NULL,
                    &e9ui->ctx);
    neogeo_sprite_3dState.windowState.open = 1;
}

void
neogeo_sprite_3d_setSource(const neogeo_sprite_3d_source_t *source)
{
    if (source) {
        neogeo_sprite_3dSource = *source;
    } else {
        memset(&neogeo_sprite_3dSource, 0, sizeof(neogeo_sprite_3dSource));
    }
}

void
neogeo_sprite_3d_persistConfig(FILE *file)
{
    if (!file) {
        return;
    }
    if (neogeo_sprite_3dState.windowState.open) {
        (void)e9ui_windowCaptureStateRectSnapshot(&neogeo_sprite_3dState.windowState,
                                                  &e9ui->ctx);
        if (neogeo_sprite_3dState.scroll) {
            e9ui_scroll_getScrollPx(neogeo_sprite_3dState.scroll,
                                    &neogeo_sprite_3dState.savedScrollX,
                                    &neogeo_sprite_3dState.savedScrollY);
            neogeo_sprite_3dState.hasSavedScroll = 1;
        }
    }
    neogeo_sprite_3dState.windowState.winHasSaved =
        e9ui_windowHasSavedPosition(neogeo_sprite_3dState.windowState.winX,
                                    neogeo_sprite_3dState.windowState.winY);
    if (neogeo_sprite_3dState.windowState.winHasSaved &&
        e9ui_windowHasSavedSize(neogeo_sprite_3dState.windowState.winW,
                                neogeo_sprite_3dState.windowState.winH)) {
        fprintf(file, "comp.sprite_debug.sprite_3d_win_x=%d\n",
                neogeo_sprite_3dState.windowState.winX);
        fprintf(file, "comp.sprite_debug.sprite_3d_win_y=%d\n",
                neogeo_sprite_3dState.windowState.winY);
        fprintf(file, "comp.sprite_debug.sprite_3d_win_w=%d\n",
                neogeo_sprite_3dState.windowState.winW);
        fprintf(file, "comp.sprite_debug.sprite_3d_win_h=%d\n",
                neogeo_sprite_3dState.windowState.winH);
    }
    fprintf(file, "comp.sprite_debug.gl_view_yaw=%.3f\n",
            neogeo_sprite_3dState.yawDegrees);
    fprintf(file, "comp.sprite_debug.gl_view_pitch=%.3f\n",
            neogeo_sprite_3dState.pitchDegrees);
    fprintf(file, "comp.sprite_debug.gl_view_zoom=%.3f\n",
            neogeo_sprite_3dState.zoomPercent);
    fprintf(file, "comp.sprite_debug.gl_view_z_separation=%.3f\n",
            neogeo_sprite_3dState.zSeparation);
    fprintf(file, "comp.sprite_debug.gl_view_content_opacity=%.3f\n",
            neogeo_sprite_3dState.contentOpacityPercent);
    fprintf(file, "comp.sprite_debug.gl_view_outlines=%d\n",
            neogeo_sprite_3dState.showOutlines ? 1 : 0);
    fprintf(file, "comp.sprite_debug.gl_view_content=%d\n",
            neogeo_sprite_3dState.showContent ? 1 : 0);
    fprintf(file, "comp.sprite_debug.gl_view_clip=%d\n",
            neogeo_sprite_3dState.clipToVisibleArea ? 1 : 0);
    if (neogeo_sprite_3dState.scroll) {
        e9ui_scroll_persistConfig(file, neogeo_sprite_3dState.scroll);
    } else if (neogeo_sprite_3dState.hasSavedScroll) {
        fprintf(file, "comp.sprite_debug.gl_view.scroll_x=%d\n",
                neogeo_sprite_3dState.savedScrollX);
        fprintf(file, "comp.sprite_debug.gl_view.scroll_y=%d\n",
                neogeo_sprite_3dState.savedScrollY);
    }
}

int
neogeo_sprite_3d_loadConfigProperty(const char *prop, const char *value)
{
    static const struct {
        const char *prop;
        int *target;
    } rectProps[] = {
        { "sprite_3d_win_x", &neogeo_sprite_3dState.windowState.winX },
        { "sprite_3d_win_y", &neogeo_sprite_3dState.windowState.winY },
        { "sprite_3d_win_w", &neogeo_sprite_3dState.windowState.winW },
        { "sprite_3d_win_h", &neogeo_sprite_3dState.windowState.winH },
    };
    static const struct {
        const char *prop;
        float *target;
        float minValue;
        float maxValue;
    } viewProps[] = {
        { "gl_view_yaw", &neogeo_sprite_3dState.yawDegrees, -90.0f, 90.0f },
        { "gl_view_pitch", &neogeo_sprite_3dState.pitchDegrees, -90.0f, 90.0f },
        { "gl_view_zoom", &neogeo_sprite_3dState.zoomPercent, 50.0f, 250.0f },
        { "gl_view_z_separation", &neogeo_sprite_3dState.zSeparation, 0.0f, 64.0f },
        { "gl_view_content_opacity", &neogeo_sprite_3dState.contentOpacityPercent, 0.0f, 100.0f },
    };
    int intValue = 0;
    float floatValue = 0.0f;

    if (!prop || !value) {
        return 0;
    }
    for (int i = 0; i < (int)(sizeof(rectProps) / sizeof(rectProps[0])); ++i) {
        if (strcmp(prop, rectProps[i].prop) == 0) {
            if (!neogeo_sprite_3d_parseInt(value, &intValue)) {
                return 0;
            }
            *rectProps[i].target = intValue;
            neogeo_sprite_3dState.windowState.winHasSaved =
                e9ui_windowHasSavedPosition(neogeo_sprite_3dState.windowState.winX,
                                            neogeo_sprite_3dState.windowState.winY);
            return 1;
        }
    }
    for (int i = 0; i < (int)(sizeof(viewProps) / sizeof(viewProps[0])); ++i) {
        if (strcmp(prop, viewProps[i].prop) == 0) {
            if (!neogeo_sprite_3d_parseFloat(value, &floatValue)) {
                return 0;
            }
            if (floatValue < viewProps[i].minValue) {
                floatValue = viewProps[i].minValue;
            }
            if (floatValue > viewProps[i].maxValue) {
                floatValue = viewProps[i].maxValue;
            }
            *viewProps[i].target = floatValue;
            return 1;
        }
    }
    if (strcmp(prop, "gl_view.scroll_x") == 0) {
        if (!neogeo_sprite_3d_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_sprite_3dState.savedScrollX = intValue;
        neogeo_sprite_3dState.hasSavedScroll = 1;
        if (neogeo_sprite_3dState.scroll) {
            e9ui_scroll_loadPersistedPx(neogeo_sprite_3dState.scroll,
                                        neogeo_sprite_3dState.savedScrollX,
                                        neogeo_sprite_3dState.savedScrollY);
        }
        return 1;
    }
    if (strcmp(prop, "gl_view.scroll_y") == 0) {
        if (!neogeo_sprite_3d_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_sprite_3dState.savedScrollY = intValue;
        neogeo_sprite_3dState.hasSavedScroll = 1;
        if (neogeo_sprite_3dState.scroll) {
            e9ui_scroll_loadPersistedPx(neogeo_sprite_3dState.scroll,
                                        neogeo_sprite_3dState.savedScrollX,
                                        neogeo_sprite_3dState.savedScrollY);
        }
        return 1;
    }
    if (strcmp(prop, "gl_view_outlines") == 0) {
        if (!neogeo_sprite_3d_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_sprite_3dState.showOutlines = intValue ? 1 : 0;
        return 1;
    }
    if (strcmp(prop, "gl_view_content") == 0) {
        if (!neogeo_sprite_3d_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_sprite_3dState.showContent = intValue ? 1 : 0;
        return 1;
    }
    if (strcmp(prop, "gl_view_clip") == 0) {
        if (!neogeo_sprite_3d_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_sprite_3dState.clipToVisibleArea = intValue ? 1 : 0;
        return 1;
    }
    if (strcmp(prop, "gl_view_full_coordinate_space") == 0) {
        if (!neogeo_sprite_3d_parseInt(value, &intValue)) {
            return 0;
        }
        neogeo_sprite_3dState.clipToVisibleArea = intValue ? 0 : 1;
        return 1;
    }
    return 0;
}

static int
neogeo_sprite_3d_parseInt(const char *value, int *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (!end || end == value) {
        return 0;
    }
    *out = (int)parsed;
    return 1;
}

static int
neogeo_sprite_3d_parseFloat(const char *value, float *out)
{
    if (!value || !out) {
        return 0;
    }
    char *end = NULL;
    float parsed = strtof(value, &end);
    if (!end || end == value) {
        return 0;
    }
    *out = parsed;
    return 1;
}
