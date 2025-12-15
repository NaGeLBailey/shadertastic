#ifndef TEXTURE_UTIL_H
#define TEXTURE_UTIL_H

#include <obs-module.h>

void render_texture(gs_texture_t *final_tex, uint32_t cx, uint32_t cy, bool use_copy);

#endif // TEXTURE_UTIL_H