#ifndef SHADERTASTIC_COMMON_HPP
#define SHADERTASTIC_COMMON_HPP

#include <map>
#include <string>
#include <obs-module.h>

struct shadertastic_effect_t;
typedef std::map<std::string, shadertastic_effect_t> shadertastic_effects_map_t;
extern gs_texture_t *shadertastic_transparent_texture;

struct shadertastic_common {
    shadertastic_effects_map_t *effects{};
    shadertastic_effect_t *selected_effect{};
    obs_source_t *source{};
    int frame_index = 0;
    float time = 0.0;
    float delta_time = 0.0;

    // Filter previous state (enabled/disabled)
    bool was_enabled = false;
};
//----------------------------------------------------------------------------------------------------------------------

#endif // SHADERTASTIC_COMMON_HPP
