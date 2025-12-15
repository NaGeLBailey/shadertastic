// Common parameters for all shaders, as reference. Do not uncomment this (but you can remove it safely).
/*
uniform float time;            // Time since the shader is running. Goes from 0 to 1 for transition effects; goes from 0 to infinity for filter effects
uniform texture2d image;       // Texture of the source (filters only)
uniform texture2d tex_interm;  // Intermediate texture where the previous step will be rendered (for multistep effects)
uniform float upixel;          // Width of a pixel in the UV space
uniform float vpixel;          // Height of a pixel in the UV space
uniform float rand_seed;       // Seed for random functions
uniform int current_step;      // index of current step (for multistep effects)
uniform int nb_steps;          // number of steps (for multistep effects)
*/

// Specific parameters of the shader. They must be defined in the meta.json file next to this one.
uniform texture2d prev_image;
uniform float prev_alpha;
uniform float zoom;
//----------------------------------------------------------------------------------------------------------------------

// These are required objects for the shader to work.
// You don't need to change anything here, unless you know what you are doing
sampler_state textureSampler {
    Filter    = Linear;
    AddressU  = Clamp;
    AddressV  = Clamp;
};

struct VertData {
    float2 uv  : TEXCOORD0;
    float4 pos : POSITION;
};

struct FragData {
    float2 uv  : TEXCOORD0;
};

VertData VSDefault(VertData v_in)
{
    VertData vert_out;
    vert_out.uv  = v_in.uv;
    vert_out.pos = mul(float4(v_in.pos.xyz, 1.0), ViewProj);
    return vert_out;
}
//----------------------------------------------------------------------------------------------------------------------

bool inside_box(float2 v, float2 left_top, float2 right_bottom) {
    float2 s = step(left_top, v) - step(right_bottom, v);
    return s.x * s.y != 0.0;
}

float4 EffectLinear(float2 uv)
{
    /*if (current_step == 0) {
        return image.Sample(textureSampler, uv);
    }
    else {
        return tex_interm.Sample(textureSampler, uv);
    }*/
    if (current_step == 0) {
        if (frame_index < 1) {
            return image.Sample(textureSampler, uv);
        }
        float2 uv2 = (uv - 0.5);

        float zoom_ratio = ( pow(100, (clamp(zoom, 0.0, 1.0))) - 1 ) / (100 - 1);  // Log scale magique : (a^x - 1) / (a - 1)

        uv2 = (uv2*(1.00-zoom_ratio)) + 0.5;

        float r = length(uv2 * float2(vpixel/upixel, 1.0));

        float4 px = image.Sample(textureSampler, uv);
        float4 px_small = image.Sample(textureSampler, uv2);
        float4 prev_px = prev_image.Sample(textureSampler, uv2);

        prev_px.rgb = prev_px.rgb / prev_px[3] / prev_px[3];

        float alpha = px[3];
        alpha = max(alpha, (prev_px[3])*prev_alpha);
        alpha = max(alpha, (px_small[3])*prev_alpha);
        alpha = clamp(alpha, 0.0, 1.0);

        float4 px_out = float4(0.0, 0.0, 0.0, alpha);

        px_out.rgb = lerp(
            prev_px.rgb,
            px.rgb,
            px[3]
        );
        //px_out.a = 0.5;
//        px_out.a = lerp(
//            prev_px.a,
//            px.a,
//            px_small[3]
//        );

        return px_out;
    }
    else {
        //return float4(0.0, 0.0, 0.0, 0.0);
        float4 px_out = tex_interm.Sample(textureSampler, uv);
        //float4 px_out = image.Sample(textureSampler, uv);
        //px_out.a = 0.9;
        return px_out;
    }
}
//----------------------------------------------------------------------------------------------------------------------

// You probably don't want to change anything from this point.

float4 PSEffect(FragData f_in) : TARGET
{
    float4 rgba = EffectLinear(f_in.uv);
    /*if (current_step == nb_steps - 1) {
        rgba.rgb = srgb_nonlinear_to_linear(rgba.rgb);
    }*/
    return rgba;
}

technique Draw
{
    pass
    {
        vertex_shader = VSDefault(v_in);
        pixel_shader = PSEffect(f_in);
    }
}
