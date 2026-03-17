/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "debugger.h"
#include "settings.h"

int
settings_validateUaeConfig(e9ui_context_t *ctx, e9ui_component_t *comp, const char *text, void *user)
{
    (void)ctx;
    (void)comp;
    (void)user;
    return settings_pathHasUaeExtension(text) ? 1 : 0;
}


int
settings_pathHasUaeExtension(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
    size_t len = strlen(path);
    if (len < 4) {
        return 0;
    }
    const char *ext = path + len - 4;
    if (ext[0] != '.') {
        return 0;
    }
    char a = ext[1];
    char b = ext[2];
    char c = ext[3];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return (a == 'u' && b == 'a' && c == 'e') ? 1 : 0;
}


static int
settings_shouldShowUaeExtensionWarning(void)
{
  // TODO
  if (debugger.settingsEdit.target != target_amiga()) {
        return 0;
    }
    const char *uaePath = debugger.settingsEdit.amiga.libretro.romPath;
    if (!uaePath || !uaePath[0]) {
        return 0;
    }
    if (settings_pathHasUaeExtension(uaePath)) {
        return 0;
    }
    return 1;
}

typedef struct settings_uae_extension_warning_state {
    SDL_Color color;
} settings_uae_extension_warning_state_t;

static int
settings_uaeExtensionWarning_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    if (!settings_shouldShowUaeExtensionWarning()) {
        return 0;
    }
    TTF_Font *font = ctx ? ctx->font : NULL;
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    int pad = ctx ? e9ui_scale_px(ctx, 4) : 4;
    return lh + pad * 2;
}

static void
settings_uaeExtensionWarning_layout(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
settings_uaeExtensionWarning_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    if (!settings_shouldShowUaeExtensionWarning()) {
        return;
    }
    settings_uae_extension_warning_state_t *st = (settings_uae_extension_warning_state_t*)self->state;
    if (!st) {
        return;
    }
    TTF_Font *font = ctx->font;
    if (!font) {
        return;
    }
    const char *msg = "UAE CONFIG filename must end with .uae";
    int tw = 0;
    int th = 0;
    SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, msg, st->color, &tw, &th);
    if (!tex) {
        return;
    }
    int x = self->bounds.x + self->bounds.w - tw;
    int y = self->bounds.y + (self->bounds.h - th) / 2;
    SDL_Rect dst = { x, y, tw, th };
    SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
}

static void
settings_uaeExtensionWarning_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
    (void)ctx;
    if (!self || !self->state) {
        return;
    }
    alloc_free(self->state);
    self->state = NULL;
}

//static
e9ui_component_t *
settings_uaeExtensionWarning_make(void)
{
    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
    if (!c) {
        return NULL;
    }
    settings_uae_extension_warning_state_t *st = (settings_uae_extension_warning_state_t*)alloc_calloc(1, sizeof(*st));
    if (!st) {
        alloc_free(c);
        return NULL;
    }
    st->color = (SDL_Color){255, 80, 80, 255};
    c->name = "settings_uae_extension_warning";
    c->state = st;
    c->preferredHeight = settings_uaeExtensionWarning_preferredHeight;
    c->layout = settings_uaeExtensionWarning_layout;
    c->render = settings_uaeExtensionWarning_render;
    c->dtor = settings_uaeExtensionWarning_dtor;
    return c;
}
