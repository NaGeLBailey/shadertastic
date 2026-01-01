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

#ifndef SHADERTASTIC_PARAMETER_PREV_FRAME_HPP
#define SHADERTASTIC_PARAMETER_PREV_FRAME_HPP

#include <string>
#include "parameter.hpp"
#include "../shadertastic_common.hpp"
#include "../util/string_util.h"
#include "../util/file_util.h"

class effect_parameter_prev_frame : public effect_parameter {
    private:
        int step_{};

        gs_texrender_t *prev_texrender[2]{ nullptr, nullptr };
        int prev_texrender_buffer = 0;

    public:
        gs_texture_t *prev_texture;
        explicit effect_parameter_prev_frame(gs_eparam_t *shader_param)
        : effect_parameter(sizeof(float), shader_param), prev_texture(shadertastic_transparent_texture) {
        }

        ~effect_parameter_prev_frame() override {
            obs_enter_graphics();
            if (prev_texrender[0] != nullptr) {
                gs_texrender_destroy(prev_texrender[0]);
                prev_texrender[0] = nullptr;
            }
            if (prev_texrender[1] != nullptr) {
                gs_texrender_destroy(prev_texrender[1]);
                prev_texrender[1] = nullptr;
            }
            obs_leave_graphics();
        }

        effect_param_datatype type() override {
            return PARAM_DATATYPE_PREV_FRAME;
        }

        [[nodiscard]] int step() const {
            return step_;
        }

        void initialize_params(const effect_shader *shader, obs_data_t *metadata, const std::string &effect_path) override {
            UNUSED_PARAMETER(shader);
            UNUSED_PARAMETER(effect_path);
            obs_data_set_default_int(metadata, "step", -1); // -1 means "the last step". This way by default we take the last step, i.e. the actually rendered frame
            step_ = (int)obs_data_get_int(metadata, "step");

            prev_texrender[0] = gs_texrender_create(GS_RGBA16, GS_ZS_NONE);
            prev_texrender[1] = gs_texrender_create(GS_RGBA16, GS_ZS_NONE);
            reset();
        }

        void set_default(obs_data_t *settings, const char *full_param_name) override {
            UNUSED_PARAMETER(settings);
            UNUSED_PARAMETER(full_param_name);
        }

        void render_property_ui(const char *full_param_name, obs_properties_t *props) override {
            UNUSED_PARAMETER(full_param_name);
            UNUSED_PARAMETER(props);
            /* Automatic parameter, no UI */
        }

        void set_data_from_settings(obs_data_t *settings, const char *full_param_name) override {
            UNUSED_PARAMETER(settings);
            UNUSED_PARAMETER(full_param_name);
            /* Automatic parameter, no UI */
        }

        void try_gs_set_val() override {
            //debug("try_gs_set_val %i", prev_texrender_buffer);
            try_gs_effect_set_texture(name.c_str(), shader_param, gs_texrender_get_texture(prev_texrender[prev_texrender_buffer]));
        }

        void reset() {
            prev_texture = shadertastic_transparent_texture;
        }

        bool attach(const uint32_t cx, const uint32_t cy, const gs_color_space source_space) {
            const int next_texrender_buffer = prev_texrender_buffer ^ 1;
            //debug("attach %i", prev_texrender_buffer);
            gs_texrender_reset(prev_texrender[next_texrender_buffer]);
            bool texrender_ok = gs_texrender_begin_with_color_space(prev_texrender[next_texrender_buffer], cx, cy, source_space);
            if (texrender_ok) {
                constexpr vec4 clear_color{0,0,0,0};
                gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
                gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);
            }
            else {
                debug("effect_parameter_prev_frame: cannot begin texrender");
            }
            return texrender_ok;
        }

        const gs_texrender_t * detach() {
            const int next_texrender_buffer = prev_texrender_buffer ^ 1;
            gs_texrender_end(prev_texrender[next_texrender_buffer]);
            return prev_texrender[next_texrender_buffer];
        }

        void next_frame() {
            prev_texrender_buffer = prev_texrender_buffer ^ 1;
        }

        /*void detach(const uint32_t cx, const uint32_t cy, const bool is_srgb) {
            gs_texrender_end(prev_texrender);
            cur_texture = gs_texrender_get_texture(prev_texrender);
            //obs_source_t *parent_source = obs_filter_get_parent(source);
            //debug("is last: %s", is_last_filter ? "YES" : "NO");

            if (cur_texture) {
                gs_blend_state_push();
                //gs_blend_function(GS_BLEND_ONE, is_last_filter ? GS_BLEND_INVSRCALPHA : GS_BLEND_ZERO);
                gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

                {
                    gs_effect_t *default_effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
                    gs_technique_t *tech = gs_effect_get_technique(default_effect, is_srgb ? "DrawSrgbDecompress" : "Draw");
                    gs_eparam_t *image = gs_effect_get_param_by_name(default_effect, "image");
                    gs_effect_set_texture(image, cur_texture);
                    //gs_effect_set_float(gs_effect_get_param_by_name(default_effect, "multiplier"), 1.0);

                    if (gs_technique_begin(tech)) {
                        if (gs_technique_begin_pass(tech, 0)) {
                            gs_draw_sprite(nullptr, 0, cx, cy);
                            gs_technique_end_pass(tech);
                        }
                        gs_technique_end(tech);
                    }
                }
                gs_blend_state_pop();
            }
        }*/
};

#endif // SHADERTASTIC_PARAMETER_PREV_FRAME_HPP
