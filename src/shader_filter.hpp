/******************************************************************************
    Copyright (C) 2023 by xurei <xureilab@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#ifndef SHADERTASTIC_SHADER_FILTER_HPP
#define SHADERTASTIC_SHADER_FILTER_HPP

// ReSharper disable CppNonInlineFunctionDefinitionInHeaderFile
// ReSharper disable CppDFAConstantParameter

#include <obs-module.h>
#include <QApplication>
#include "effect.h"
#include "face_tracking/face_tracking.h"
#include "settings.h"
#include "shadertastic.hpp"
#include "shadertastic_common.hpp"
#include "util/time_util.hpp"

obs_properties_t *shadertastic_filter_properties(void *data);
//----------------------------------------------------------------------------------------------------------------------

static shadertastic_filter *shadertastic_no_filter = nullptr;

static void *shadertastic_filter_create(obs_data_t *settings, obs_source_t *source);
//----------------------------------------------------------------------------------------------------------------------

static shadertastic_filter* shadertastic_filter_cast(void *data) {
    if (data == nullptr) {
        if (shadertastic_no_filter == nullptr) {
            obs_data_t *no_filter_settings = obs_data_create();
            shadertastic_no_filter = static_cast<shadertastic_filter *>(shadertastic_filter_create(no_filter_settings, nullptr));
            obs_data_release(no_filter_settings);
        }
        return shadertastic_no_filter;
    }
    return static_cast<shadertastic_filter*>(data);
}
//----------------------------------------------------------------------------------------------------------------------

static void *shadertastic_filter_create(obs_data_t *settings, obs_source_t *source) {
    shadertastic_filter *s = static_cast<shadertastic_filter*>(bzalloc(sizeof(struct shadertastic_filter)));
    s->source = source;
    s->effects = new shadertastic_effects_map_t();
    s->rand_seed = static_cast<float>(rand()) / static_cast<float>(RAND_MAX); // NOLINT(*-msc50-cpp)
    s->frame_index = 0;

    // FIXME getting the root source doesn't work here :( it would be great for debugging, but obs_filter_get_parent() is not valid outside of video_render, filter_audio, filter_video, and filter_remove callbacks.
    //#ifdef DEV_MODE
    //    obs_source_t *root_source = obs_filter_get_parent(source);
    //    debug("%s", obs_source_get_name(root_source));
    //    while (obs_source_get_type(root_source) != OBS_SOURCE_TYPE_INPUT) {
    //        debug("%s", obs_source_get_name(root_source));
    //        root_source = obs_filter_get_parent(source);
    //    }
    //    debug("FILTER %s ON %s Settings : %s", obs_source_get_name(source), obs_source_get_name(root_source), obs_data_get_json(settings));
    //#endif
    debug("FILTER %s Settings : %s", (source==nullptr) ? "null" : obs_source_get_name(source), obs_data_get_json(settings));

    obs_enter_graphics();
    s->interm_texrender[0] = gs_texrender_create(GS_RGBA16, GS_ZS_NONE);
    s->interm_texrender[1] = gs_texrender_create(GS_RGBA16, GS_ZS_NONE);
    obs_leave_graphics();

    char *filters_dir_ = obs_module_file("effects");
    std::string filters_dir(filters_dir_);
    bfree(filters_dir_);

    load_effects(s, settings, filters_dir, "filter");
    auto effects_paths = shadertastic_settings().effects_paths;
    for (auto &effect_path : effects_paths) {
        load_effects(s, settings, effect_path, "filter");
    }

    // Set defaults for each effect
    for (auto& [effect_name, effect] : *(s->effects)) {
        // LEGACY - input_time is deprecated. Migrating it as a parameter
        if (effect.legacy_input_time) {
            obs_data_set_default_double(settings, get_full_param_name_static(effect_name, "speed").c_str(), 0.1);
            double speed = obs_data_get_double(settings, get_full_param_name_static(effect_name, "speed").c_str());
            obs_data_set_default_double(settings, get_full_param_name_static(effect_name, "time_speed").c_str(), speed);

            obs_data_set_default_bool(settings, get_full_param_name_static(effect_name, "reset_time_on_show").c_str(), false);
            bool reset_time_on_show = obs_data_get_bool(settings, get_full_param_name_static(effect_name, "reset_time_on_show").c_str());
            obs_data_set_default_bool(settings, get_full_param_name_static(effect_name, "time_reset_time_on_show").c_str(), reset_time_on_show);
        }
    }

    if (source != nullptr) {
        obs_source_update(source, settings);
    }

    return s;
}
//----------------------------------------------------------------------------------------------------------------------

static void shadertastic_filter_destroy(void *data) {
    shadertastic_filter *s = shadertastic_filter_cast(data);

    obs_enter_graphics();
    gs_texrender_destroy(s->interm_texrender[0]);
    gs_texrender_destroy(s->interm_texrender[1]);
    obs_leave_graphics();
    face_tracking_destroy(s->face_tracking);
    s->release();
    bfree(data);
}
//----------------------------------------------------------------------------------------------------------------------

inline uint32_t shadertastic_filter_getwidth(void *data) {
    const shadertastic_filter *s = shadertastic_filter_cast(data);
    return s->width;
}

inline uint32_t shadertastic_filter_getheight(void *data) {
    const shadertastic_filter *s = shadertastic_filter_cast(data);
    return s->height;
}
//----------------------------------------------------------------------------------------------------------------------

inline void shadertastic_filter_update(void *data, obs_data_t *settings) {
    shadertastic_filter *s = shadertastic_filter_cast(data);
    //debug("Update : %s", obs_data_get_json(settings));

    if (s->should_reload) {
        s->should_reload = false;
        obs_source_update_properties(s->source);
    }

    const char *selected_effect_name = obs_data_get_string(settings, "effect");
    auto selected_effect_it = s->effects->find(selected_effect_name);
    if (selected_effect_it != s->effects->end()) {
        s->selected_effect = &(selected_effect_it->second);
    }

    if (s->selected_effect != nullptr) {
        obs_data_set_string(settings, (std::string(selected_effect_name) + "__compile_error").c_str(), s->selected_effect->error_str.c_str());
        //debug("Selected Effect: %s", selected_effect_name);
        for (auto param: s->selected_effect->effect_params) {
            std::string full_param_name = param->get_full_param_name(selected_effect_name);
            param->set_data_from_settings(settings, full_param_name.c_str());
            //info("Assigned value:  %s %lu", full_param_name, param.data_size);
        }
    }
}
//----------------------------------------------------------------------------------------------------------------------

void render_texture(gs_texture_t *final_tex, const uint32_t cx, const uint32_t cy) {
    gs_blend_state_push();
    //gs_blend_function(GS_BLEND_ONE, is_last_filter ? GS_BLEND_INVSRCALPHA : GS_BLEND_ZERO);
    // gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

    gs_blend_function_separate(
        GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA,
        GS_BLEND_ONE, GS_BLEND_INVSRCALPHA
    );

    gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
    gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
    gs_effect_set_float(gs_effect_get_param_by_name(default_effect, "multiplier"), 1.0);

    const bool linear_srgb = gs_get_linear_srgb();
    const bool previous = gs_framebuffer_srgb_enabled();
    gs_enable_framebuffer_srgb(linear_srgb);
    if (linear_srgb) {
        gs_effect_set_texture_srgb(image, final_tex);
    }
    else {
        gs_effect_set_texture_srgb(image, final_tex);
        //gs_effect_set_texture(image, final_tex);
    }

    gs_technique_t *tech = gs_effect_get_technique(default_effect, "Draw");
    if (gs_technique_begin(tech)) {
        if (gs_technique_begin_pass(tech, 0)) {
            gs_draw_sprite(nullptr, 0, cx, cy);
            gs_technique_end_pass(tech);
        }
        gs_technique_end(tech);
    }

    gs_enable_framebuffer_srgb(previous);
    gs_blend_state_pop();
}
//----------------------------------------------------------------------------------------------------------------------

static void shadertastic_filter_tick(void *data, float deltatime_seconds) {
    shadertastic_filter *s = shadertastic_filter_cast(data);

    obs_source_t *target = obs_filter_get_parent(s->source);
    s->width = obs_source_get_base_width(target);
    s->height = obs_source_get_base_height(target);

    bool is_enabled = obs_source_enabled(s->source) && s->selected_effect != nullptr;

    if (s->selected_effect && s->selected_effect->use_facetracking) {
        if (s->face_tracking == nullptr) {
            face_tracking_create(s->face_tracking);
        }
    }
    if (is_enabled) {
        s->time += deltatime_seconds;
        s->delta_time = deltatime_seconds;

        for (effect_parameter* param : s->selected_effect->effect_params) {
            if (param) {
                param->tick(s);
            }
        }
        if (!s->was_enabled) {
            s->frame_index = 0;
            if (s->selected_effect != nullptr) {
                for (auto *prev_frame_to_keep : s->selected_effect->prev_frames_to_keep) {
                    if (prev_frame_to_keep != nullptr) {
                        prev_frame_to_keep->reset();
                    }
                }
            }
        }
        /*else {
            s->frame_index++;
        }*/
    }
    if (is_enabled != s->was_enabled) {
        s->was_enabled = is_enabled;
    }
}
//----------------------------------------------------------------------------------------------------------------------

void shadertastic_filter_video_render(void *data, gs_effect_t *effect) {
    //debug("-----------------------------------------");
    UNUSED_PARAMETER(effect);
    shadertastic_filter *s = shadertastic_filter_cast(data);
    float filter_time = (float)s->time;

    constexpr gs_color_space preferred_spaces[] = {
        GS_CS_SRGB,
        GS_CS_SRGB_16F,
        GS_CS_709_EXTENDED,
    };
    obs_source_t *target_source = obs_filter_get_target(s->source);

    const uint32_t cx = s->width;
    const uint32_t cy = s->height;

    const gs_color_space source_space = obs_source_get_color_space(target_source, OBS_COUNTOF(preferred_spaces), preferred_spaces);
    const gs_color_format format = gs_get_format_from_space(source_space);

    shadertastic_effect_t *selected_effect = s->selected_effect;
    if (selected_effect == nullptr || selected_effect->main_shader == nullptr) {
        //debug("%s : No effect selected", obs_source_get_name(s->source));
        obs_source_skip_video_filter(s->source);
        return;
    }

    if (selected_effect->use_facetracking && s->face_tracking != nullptr) {
        #ifdef DEV_MODE
        unsigned long tic = get_time_us();
        #endif
        face_tracking_tick(s->face_tracking.get(), target_source, s->delta_time);

        debug_trace("Facetracking time %lu", get_time_us()-tic);
    }
    gs_texture_t *interm_texture = shadertastic_transparent_texture;

    gs_blend_state_push();
    gs_blend_function_separate(
        GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA,
        GS_BLEND_ONE, GS_BLEND_INVSRCALPHA
    );
    constexpr vec4 clear_color{0,0,0,0};

    bool render_ok = true;
    for (int current_step=0; current_step < selected_effect->nb_steps; ++current_step) {
        bool texrender_ok = true;
        bool is_final_step = current_step == selected_effect->nb_steps - 1;

        s->interm_texrender_buffer = s->interm_texrender_buffer ^ 1;
        gs_texrender_reset(s->interm_texrender[s->interm_texrender_buffer]);
        texrender_ok = gs_texrender_begin_with_color_space(s->interm_texrender[s->interm_texrender_buffer], cx, cy, source_space);

        if (!texrender_ok) {
            render_ok = false;
            break;
        }

        gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
        gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f); // This line took me A WHOLE WEEK to figure out

        // if (texrender_ok && current_step < (int)selected_effect->prev_frames_to_keep.size() && selected_effect->prev_frames_to_keep[current_step]) {
        //     texrender_ok = selected_effect->prev_frames_to_keep[current_step]->attach(cx, cy, source_space);
        // }

        selected_effect->set_step_params(current_step, interm_texture);
        // You CANNOT put it above the for loop. Textures need to be rebinded every time (not that costful actually)
        selected_effect->set_params(nullptr, nullptr, s->frame_index, false, filter_time, s->delta_time, cx, cy, s->rand_seed);

        selected_effect->main_shader->render(s->source, cx, cy);

        // if (selected_effect->prev_frames_to_keep[current_step]) {
        //     selected_effect->prev_frames_to_keep[current_step]->detach(s->source, cx, cy, false);
        // }
        gs_texrender_end(s->interm_texrender[s->interm_texrender_buffer]);
        interm_texture = gs_texrender_get_texture(s->interm_texrender[s->interm_texrender_buffer]);

        if (current_step < (int)selected_effect->prev_frames_to_keep.size() && selected_effect->prev_frames_to_keep[current_step]) {
            if (selected_effect->prev_frames_to_keep[current_step]->attach(cx, cy, source_space)) {
                render_texture(interm_texture, cx, cy);
                selected_effect->prev_frames_to_keep[current_step]->detach();

                if (s->frame_index < 20) {
                    // __debug_save_texture_png(selected_effect->prev_frames_to_keep[current_step]->prev_texture, cx, cy, (
                    //     std::string("/home/olivier/obs-plugins/obs-shadertastic/plugin/lab/debug_images/")
                    //     + s->selected_effect->name + "-prev-" + std::to_string(s->frame_index) + ".png"
                    // ).c_str());
                }
            }
        }
    }

    if (s->frame_index < 20) {
        // __debug_save_texture_png(interm_texture, cx, cy, (
        //     std::string("/home/olivier/obs-plugins/obs-shadertastic/plugin/lab/debug_images/")
        //     + s->selected_effect->name + "-" + std::to_string(s->frame_index) + ".png"
        // ).c_str());
    }

    s->frame_index++;

    if (render_ok) {
        if (obs_source_process_filter_begin_with_color_space(s->source, format, source_space, OBS_NO_DIRECT_RENDERING)) {
            gs_blend_function_separate(
                GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA,
                GS_BLEND_ONE, GS_BLEND_INVSRCALPHA
            );

            gs_texture_t *final_tex = interm_texture;
            render_texture(final_tex, cx, cy);
            // gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
            // gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
            // //gs_effect_set_float(gs_effect_get_param_by_name(default_effect, "multiplier"), 1.0f);
            // gs_effect_set_texture(image, final_tex);
            //
            // gs_technique_t *tech = gs_effect_get_technique(default_effect, "Draw");
            // if (gs_technique_begin(tech)) {
            //     if (gs_technique_begin_pass(tech, 0)) {
            //         gs_draw_sprite(final_tex, 0, cx, cy);
            //         gs_technique_end_pass(tech);
            //     }
            //     gs_technique_end(tech);
            // }

            //obs_source_process_filter_end(s->source, default_effect, cx, cy);
            //obs_source_process_filter_end(s->source, nullptr, cx, cy);
        }
    }

    gs_blend_state_pop();
}
//----------------------------------------------------------------------------------------------------------------------

bool shadertastic_filter_properties_change_effect_callback(void *priv, obs_properties_t *props, obs_property_t *p, obs_data_t *data) {
    UNUSED_PARAMETER(p);
    shadertastic_filter *s = shadertastic_filter_cast(priv);

    if (s->selected_effect != nullptr) {
        obs_property_set_visible(obs_properties_get(props, (s->selected_effect->name + "__params").c_str()), false);
        obs_property_set_visible(obs_properties_get(props, (s->selected_effect->name + "__warning").c_str()), false);
    }

    //shadertastic_filter_properties(priv);
    const char *select_effect_name = obs_data_get_string(data, "effect");
    debug("CALLBACK : %s", select_effect_name);
    auto selected_effect = s->effects->find(std::string(select_effect_name));
    if (selected_effect != s->effects->end()) {
        debug("CALLBACK : %s -> %s", select_effect_name, selected_effect->second.name.c_str());
        obs_property_set_visible(obs_properties_get(props, (selected_effect->second.name + "__params").c_str()), true);
        obs_property_set_visible(obs_properties_get(props, (selected_effect->second.name + "__warning").c_str()), true);
    }

    return true;
}

bool shadertastic_filter_reload_button_click(obs_properties_t *props, obs_property_t *property, void *data) {
    UNUSED_PARAMETER(props);
    UNUSED_PARAMETER(property);
    shadertastic_filter *s = shadertastic_filter_cast(data);

    if (s->selected_effect != nullptr) {
        s->selected_effect->reload();
    }
    s->should_reload = true;
    s->rand_seed = (float)rand() / (float)RAND_MAX;
    s->was_enabled = false;
    s->frame_index = 0;
    obs_source_update(s->source, nullptr);
    return true;
}

obs_properties_t *shadertastic_filter_properties(void *data) {
    shadertastic_filter *s = shadertastic_filter_cast(data);
    obs_properties_t *props = obs_properties_create();

    obs_property_t *p;

    // Dev mode settings
    if (shadertastic_settings().dev_mode_enabled) {
        obs_properties_add_button(props, "reload_btn", "Reload", shadertastic_filter_reload_button_click);
    }

    // Shader mode
    p = obs_properties_add_list(props, "effect", "Effect", OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);
    obs_property_list_add_string(p, "(Choose an effect)", "");
    for (auto& [effect_name, effect] : *(s->effects)) {
        const char *effect_label = effect.label.c_str();
        obs_property_list_add_string(p, effect_label, effect_name.c_str());
    }
    obs_property_set_modified_callback2(p, shadertastic_filter_properties_change_effect_callback, data);

    for (auto& [effect_name, effect] : *(s->effects)) {
        const char *effect_label = effect.label.c_str();
        obs_properties_t *effect_group = obs_properties_create();
        //obs_properties_add_text(effect_group, "", effect_name, OBS_TEXT_INFO);

        if (!effect.error_str.empty()) {
            obs_properties_t *error_group = obs_properties_create();
            auto prop = obs_properties_add_text(
                error_group,
                (effect_name + "__compile_error").c_str(),
                effect.error_str.c_str(),
                OBS_TEXT_MULTILINE
            );
            obs_property_text_set_monospace(prop, true);
            obs_property_text_set_info_type(prop, OBS_TEXT_INFO_ERROR);

            obs_properties_add_group(props, (effect_name + "__warning").c_str(), "⚠ Shader error", OBS_GROUP_NORMAL, error_group);
            if (s->selected_effect != &effect) {
                obs_property_set_visible(obs_properties_get(props, (effect_name + "__warning").c_str()), false);
            }
        }

        for (auto param: effect.effect_params) {
            std::string full_param_name = param->get_full_param_name(effect_name);
            if (!param->is_dev_mode() || shadertastic_settings().dev_mode_enabled) {
                param->render_property_ui(full_param_name.c_str(), effect_group);
            }
        }
        obs_properties_add_group(props, (effect_name + "__params").c_str(), effect_label, OBS_GROUP_NORMAL, effect_group);
        obs_property_set_visible(obs_properties_get(props, (effect_name + "__params").c_str()), false);
    }
    if (s->selected_effect != nullptr) {
        obs_property_set_visible(obs_properties_get(props, (s->selected_effect->name + "__params").c_str()), true);
        obs_property_set_visible(obs_properties_get(props, (s->selected_effect->name + "__warning").c_str()), true);
    }

    about_property(props);

    return props;
}
//----------------------------------------------------------------------------------------------------------------------

void shadertastic_filter_get_defaults(obs_data_t *settings) {
    if (shadertastic_no_filter == nullptr) {
        shadertastic_no_filter = static_cast<shadertastic_filter *>(shadertastic_filter_create(settings, nullptr));
    }
    for (auto effect : *shadertastic_no_filter->effects) {
        shadertastic_effect_set_defaults(settings, &effect.second);
    }
}

static gs_color_space shadertastic_filter_get_color_space(void *data, size_t count, const enum gs_color_space *preferred_spaces) {
    shadertastic_filter *s = shadertastic_filter_cast(data);
    const enum gs_color_space source_space = obs_source_get_color_space(
        obs_filter_get_target(s->source),
        count, preferred_spaces
    );

    return source_space;
}
//----------------------------------------------------------------------------------------------------------------------

void shadertastic_filter_show(void *data) {
    shadertastic_filter *s = shadertastic_filter_cast(data);
    shadertastic_effect_t *selected_effect = s->selected_effect;
    if (selected_effect != nullptr) {
        selected_effect->show();
    }
}
//----------------------------------------------------------------------------------------------------------------------

void shadertastic_filter_hide(void *data) {
    shadertastic_filter *s = shadertastic_filter_cast(data);
    shadertastic_effect_t *selected_effect = s->selected_effect;
    if (selected_effect != nullptr) {
        selected_effect->hide();
    }
}
//----------------------------------------------------------------------------------------------------------------------

void shadertastic_filter_unload() {
    shadertastic_filter_destroy(shadertastic_no_filter);
}

#endif // SHADERTASTIC_SHADER_FILTER_HPP
