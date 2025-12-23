#include "texture_util.h"

void render_texture(gs_texture_t *final_tex, const bool use_copy, const bool compensate_alpha) {
    const uint32_t cx = gs_texture_get_width(final_tex);
    const uint32_t cy = gs_texture_get_height(final_tex);
    gs_blend_state_push();
    //gs_blend_function(GS_BLEND_ONE, is_last_filter ? GS_BLEND_INVSRCALPHA : GS_BLEND_ZERO);
    // gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

    if (use_copy) {
        gs_blend_function_separate(
            GS_BLEND_ONE, GS_BLEND_ZERO,
            GS_BLEND_ONE, GS_BLEND_ZERO
        );
    }
    else {
        gs_blend_function_separate(
            GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA,
            GS_BLEND_ONE, GS_BLEND_INVSRCALPHA
        );
    }

    gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
    //gs_effect_set_float(gs_effect_get_param_by_name(default_effect, "multiplier"), 1.0);

    const bool linear_srgb = gs_get_linear_srgb();
    const bool previous = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(linear_srgb);
    if (linear_srgb) {
        gs_effect_set_texture_srgb(image, final_tex);
    }
    else {
        gs_effect_set_texture(image, final_tex);
    }

    gs_technique_t *tech = gs_effect_get_technique(default_effect, compensate_alpha ? "DrawAlphaDivide" : "Draw");
    if (gs_technique_begin(tech)) {
        if (gs_technique_begin_pass(tech, 0)) {
            gs_draw_sprite(final_tex, 0, cx, cy);
            gs_technique_end_pass(tech);
        }
        gs_technique_end(tech);
    }

    gs_enable_framebuffer_srgb(previous);
    gs_blend_state_pop();
}
