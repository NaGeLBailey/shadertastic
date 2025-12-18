/******************************************************************************
	Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>
	Copyright (C) 2025 by xurei <xureilab@gmail.com>

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

	Modifications:
		- 2025-12-18: Adapted for Shadertastic
******************************************************************************/

#include <obs-module.h>

#include "shadertastic.hpp"

/**
 * This file contains modified version of the OBS source pipeline, for a faster integration.
 */
static inline void render_filter_tex(gs_texture_t *tex, gs_effect_t *effect, uint32_t width, uint32_t height,
					 const char *tech_name)
{
	gs_technique_t *tech = gs_effect_get_technique(effect, tech_name);
	gs_eparam_t *image = gs_effect_get_param_by_name(effect, "image");
	size_t passes, i;

	const bool linear_srgb = gs_get_linear_srgb();

	const bool previous = gs_framebuffer_srgb_enabled();
	gs_enable_framebuffer_srgb(linear_srgb);

	if (linear_srgb)
		gs_effect_set_texture_srgb(image, tex);
	else
		gs_effect_set_texture(image, tex);

	passes = gs_technique_begin(tech);
	for (i = 0; i < passes; i++) {
		gs_technique_begin_pass(tech, i);
		gs_draw_sprite(tex, 0, width, height);
		gs_technique_end_pass(tech);
	}
	gs_technique_end(tech);

	gs_enable_framebuffer_srgb(previous);
}

inline void obs_source_default_render(obs_source_t *source)
{
	if (source->context.data) {
		gs_effect_t *effect = obs->video.default_effect;
		gs_technique_t *tech = gs_effect_get_technique(effect, "Draw");
		size_t passes, i;

		passes = gs_technique_begin(tech);
		for (i = 0; i < passes; i++) {
			gs_technique_begin_pass(tech, i);
			source_render(source, effect);
			gs_technique_end_pass(tech);
		}
		gs_technique_end(tech);
	}
}

bool obs_source_process_filter_begin_with_color_space(obs_source_t *filter, enum gs_color_format format,
						      enum gs_color_space space,
						      enum obs_allow_direct_render allow_direct)
{
	obs_source_t *target, *parent;
	uint32_t filter_flags, parent_flags;
	int cx, cy;

	if (!obs_ptr_valid(filter, "obs_source_process_filter_begin_with_color_space"))
		return false;

	filter->filter_bypass_active = false;

	target = obs_filter_get_target(filter);
	parent = obs_filter_get_parent(filter);

	if (!target) {
		blog(LOG_INFO, "filter '%s' being processed with no target!", filter->context.name);
		return false;
	}
	if (!parent) {
		blog(LOG_INFO, "filter '%s' being processed with no parent!", filter->context.name);
		return false;
	}

	filter_flags = filter->info.output_flags;
	parent_flags = parent->info.output_flags;
	cx = get_base_width(target);
	cy = get_base_height(target);

	filter->allow_direct = allow_direct;

	/* if the parent does not use any custom effects, and this is the last
	 * filter in the chain for the parent, then render the parent directly
	 * using the filter effect instead of rendering to texture to reduce
	 * the total number of passes */
	if (can_bypass(target, parent, filter_flags, parent_flags, allow_direct, space)) {
		filter->filter_bypass_active = true;
		return true;
	}

	if (!cx || !cy) {
		obs_source_skip_video_filter(filter);
		return false;
	}

	if (filter->filter_texrender && (gs_texrender_get_format(filter->filter_texrender) != format)) {
		gs_texrender_destroy(filter->filter_texrender);
		filter->filter_texrender = NULL;
	}

	if (!filter->filter_texrender) {
		filter->filter_texrender = gs_texrender_create(format, GS_ZS_NONE);
	}

	if (gs_texrender_begin_with_color_space(filter->filter_texrender, cx, cy, space)) {
		gs_blend_state_push();
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

		bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
		bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;
		struct vec4 clear_color;

		vec4_zero(&clear_color);
		gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		gs_ortho(0.0f, (float)cx, 0.0f, (float)cy, -100.0f, 100.0f);

		if (target == parent && !custom_draw && !async)
			obs_source_default_render(target);
		else
			obs_source_video_render(target);

		gs_blend_state_pop();

		gs_texrender_end(filter->filter_texrender);
	}
	return true;
}

void obs_source_process_filter_tech_end(obs_source_t *filter, gs_effect_t *effect, uint32_t width, uint32_t height,
					const char *tech_name)
{
	obs_source_t *target, *parent;
	gs_texture_t *texture;
	uint32_t filter_flags;

	if (!filter)
		return;

	const bool filter_bypass_active = filter->filter_bypass_active;
	filter->filter_bypass_active = false;

	target = obs_filter_get_target(filter);
	parent = obs_filter_get_parent(filter);

	if (!target || !parent)
		return;

	filter_flags = filter->info.output_flags;

	const bool previous = gs_set_linear_srgb((filter_flags & OBS_SOURCE_SRGB) != 0);

	const char *tech = tech_name ? tech_name : "Draw";

	if (filter_bypass_active) {
		render_filter_bypass(target, effect, tech);
	} else {
		texture = gs_texrender_get_texture(filter->filter_texrender);
		if (texture) {
			render_filter_tex(texture, effect, width, height, tech);
		}
	}

	gs_set_linear_srgb(previous);
}

void obs_source_process_filter_end(obs_source_t *filter, gs_effect_t *effect, uint32_t width, uint32_t height)
{
	if (!obs_ptr_valid(filter, "obs_source_process_filter_end"))
		return;

	obs_source_process_filter_tech_end(filter, effect, width, height, "Draw");
}
