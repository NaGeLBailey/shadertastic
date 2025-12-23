#ifndef TEXTURE_UTIL_H
#define TEXTURE_UTIL_H

#include <obs-module.h>

void render_texture(gs_texture_t *final_tex, bool use_copy, bool compensate_alpha);

#endif // TEXTURE_UTIL_H