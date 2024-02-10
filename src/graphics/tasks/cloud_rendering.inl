#pragma once
#define DAXA_ENABLE_SHADER_NO_NAMESPACE 1
#include <daxa/daxa.inl>
#include <daxa/utils/task_graph.inl>
#include "../shared.inl"

#define WORKGROUP_SIZE 8

#if __cplusplus || defined(CloudRendering_SHADER)

DAXA_DECL_TASK_USES_BEGIN(CloudRendering, 2)
DAXA_TASK_USE_IMAGE(u_target_image, REGULAR_2D, COMPUTE_SHADER_STORAGE_WRITE_ONLY)
DAXA_TASK_USE_IMAGE(u_depth_image, REGULAR_2D, COMPUTE_SHADER_SAMPLED)
DAXA_DECL_TASK_USES_END()

struct CloudRenderingPush {
    TextureId noise_texture;
};

#endif

#if __cplusplus
#include "../../context.hpp"
#include "../texture.hpp"


struct CloudRenderingTask {
    DAXA_USE_TASK_HEADER(CloudRendering)

    inline static const daxa::ComputePipelineCompileInfo PIPELINE_COMPILE_INFO = {
        .shader_info = daxa::ShaderCompileInfo{
            .source = daxa::ShaderFile{"src/graphics/tasks/cloud_rendering.inl"},
            .compile_options = { .defines = { { std::string{CloudRenderingTask::NAME} + "_SHADER", "1" } } }
        },
        .push_constant_size = sizeof(CloudRenderingPush),
        .name = std::string{CloudRenderingTask::NAME}
    };

    Context* context = {};
    Texture* noise_texture = {};
    
    void callback(daxa::TaskInterface ti) {
        auto cmd = ti.get_command_list();
        context->gpu_metrics[name]->start(cmd);
        cmd.set_uniform_buffer(context->shader_globals_set_info);
        cmd.set_uniform_buffer(ti.uses.get_uniform_buffer_info());
        cmd.set_pipeline(*context->compute_pipelines.at(PIPELINE_COMPILE_INFO.name));
        cmd.push_constant(CloudRenderingPush { .noise_texture = noise_texture->get_texture_id() });

        auto size = ti.get_device().info_image(uses.u_target_image.image()).size;
        cmd.dispatch((size.x + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, (size.y + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1);
        context->gpu_metrics[name]->end(cmd);
    }
};
#endif

#if defined(CloudRendering_SHADER)
#extension GL_EXT_debug_printf : enable
#include "../shared.inl"

DAXA_DECL_PUSH_CONSTANT(CloudRenderingPush, push)

layout(local_size_x = WORKGROUP_SIZE, local_size_y = WORKGROUP_SIZE) in;

#define VOLUMETRIC_LIGHT
//#define SPHERICAL_PROJECTION

#define cameraMode 2 

#define cloudSpeed 0.02
#define cloudHeight 1600.0
#define cloudThickness 500.0
#define cloudDensity 0.03

#define fogDensity 0.00003

#define volumetricCloudSteps 24			//Higher is a better result with rendering of clouds.
#define volumetricLightSteps 10			//Higher is a better result with rendering of volumetric light.

#define cloudShadowingSteps 10			//Higher is a better result with shading on clouds.
#define volumetricLightShadowSteps 2	//Higher is a better result with shading on volumetric light from clouds

#define rayleighCoeff (vec3(0.27, 0.5, 1.0) * 1e-5)	//Not really correct
#define mieCoeff vec3(0.5e-6)						//Not really correct

const f32 sunBrightness = 3.0;

#define earthRadius 6371000.0

//////////////////////////////////////////////////////////////////

f32 bayer2(vec2 a){
    a = floor(a);
    return fract( dot(a, vec2(.5, a.y * .75)) );
}

vec2 rsi(vec3 position, vec3 direction, f32 radius) {
    f32 PoD = dot(position, direction);
    f32 radiusSquared = radius * radius;

    f32 delta = PoD * PoD + radiusSquared - dot(position, position);
    if (delta < 0.0) return vec2(-1.0);
    delta = sqrt(delta);

    return -PoD + vec2(-delta, delta);
}

#define bayer4(a)   (bayer2( .5*(a))*.25+bayer2(a))
#define bayer8(a)   (bayer4( .5*(a))*.25+bayer2(a))
#define bayer16(a)  (bayer8( .5*(a))*.25+bayer2(a))
#define bayer32(a)  (bayer16(.5*(a))*.25+bayer2(a))
#define bayer64(a)  (bayer32(.5*(a))*.25+bayer2(a))
#define bayer128(a) (bayer64(.5*(a))*.25+bayer2(a))

//////////////////////////////////////////////////////////////////

#define cloudMinHeight cloudHeight
#define cloudMaxHeight (cloudThickness + cloudMinHeight)

#define sunPosition vec3(1.0, 1.0, 0.0)

const f32 pi = acos(-1.0);
const f32 rPi = 1.0 / pi;
const f32 hPi = pi * 0.5;
const f32 tau = pi * 2.0;
const f32 rLOG2 = 1.0 / log(2.0);

struct Ray {
    vec3 world_position;
    vec3 ray_direction;
    vec3 sun_direction;
};

///////////////////////////////////////////////////////////////////////////////////

#define d0(x) (abs(x) + 1e-8)
#define d02(x) (abs(x) + 1e-3)

const vec3 totalCoeff = rayleighCoeff + mieCoeff;

vec3 scatter(vec3 coeff, f32 depth) {
	return coeff * depth;
}

vec3 absorb(vec3 coeff, f32 depth) {
	return exp(scatter(coeff, -depth));
}

f32 calculate_particle_thickness(f32 depth) {
    depth = depth * 2.0;
    depth = max(depth + 0.01, 0.01);
    depth = 1.0 / depth;
    
	return 100000.0 * depth;   
}

f32 calculate_particle_thickness_h(f32 depth) {
    depth = depth * 2.0 + 0.1;
    depth = max(depth + 0.01, 0.01);
    depth = 1.0 / depth;
    
	return 100000.0 * depth;   
}

f32 calculate_particle_thickness_const(const f32 depth) {
	return 100000.0 / max(depth * 2.0 - 0.01, 0.01);   
}

f32 henyey_greenstein_phase(f32 x, f32 g) {
    f32 g2 = g*g;
	return 0.25 * ((1.0 - g2) * pow(1.0 + g2 - 2.0*g*x, -1.5));
    //return (1.0 / (4.0 * 3.1415))  * ((1.0 - g * g) / pow(1.0 + g*g - 2.0*g*costh, 1.5));
}

f32 powder(f32 od) {
	return 1.0 - exp(-od * 2.0);
}

f32 calculate_scatter_intergral(f32 opticalDepth, f32 coeff) {
    f32 a = -coeff * rLOG2;
    f32 b = -1.0 / coeff;
    f32 c =  1.0 / coeff;

    return exp(a * opticalDepth) * b + c;
}

vec3 calculate_scatter_intergral(f32 opticalDepth, vec3 coeff) {
    vec3 a = -coeff * rLOG2;
    vec3 b = -1.0 / coeff;
    vec3 c =  1.0 / coeff;

    return exp(a * opticalDepth) * b + c;
}

vec3 calculate_atmospheric_scattering_top(Ray pos) {
    const f32 ln2 = log(2.0);
    
    f32 lDotU = dot(pos.sun_direction, vec3(0.0, 1.0, 0.0));
    
	f32 opticalDepth = calculate_particle_thickness_const(1.0);
    f32 opticalDepthLight = calculate_particle_thickness(lDotU);
    
    vec3 scatterView = scatter(totalCoeff, opticalDepth);
    vec3 absorbView = absorb(totalCoeff, opticalDepth);
    
    vec3 scatterLight = scatter(totalCoeff, opticalDepthLight);
    vec3 absorbLight = absorb(totalCoeff, opticalDepthLight);
    
    vec3 absorbSun = d02(absorbLight - absorbView) / d02((scatterLight - scatterView) * ln2);
    
    vec3 mieScatter = scatter(mieCoeff, opticalDepth) * 0.25;
    vec3 rayleighScatter = scatter(rayleighCoeff, opticalDepth) * 0.375;
    
    vec3 scatterSun = mieScatter + rayleighScatter;
    
    return (scatterSun * absorbSun) * sunBrightness;
}

f32 get_3d_noise(vec3 pos) {
    f32 p = floor(pos.z);
    f32 f = pos.z - p;
    
    const f32 invNoiseRes = 1.0 / 64.0;
    
    f32 zStretch = 17.0 * invNoiseRes;
    
    vec2 coord = pos.xy * invNoiseRes + (p * zStretch);
    
    vec2 noise = vec2(sample_texture(push.noise_texture, coord).x,
					  sample_texture(push.noise_texture, coord + zStretch).x);
    
    return mix(noise.x, noise.y, f);
}

f32 get_clouds(vec3 p) {
    p = vec3(p.x, length(p + vec3(0.0, earthRadius, 0.0)) - earthRadius, p.z);
    p.xz += globals.camera_position.xz;
    
    if (p.y < cloudMinHeight || p.y > cloudMaxHeight)
        return 0.0;
    
    f32 time = -1.0 * cloudSpeed * globals.elapsed_time;
    vec3 movement = vec3(time, 0.0, time);
    
    vec3 cloudCoord = (p * 0.001) + movement;
    
	f32 noise = get_3d_noise(cloudCoord) * 0.5;
    noise += get_3d_noise(cloudCoord * 2.0 + movement) * 0.25;
    noise += get_3d_noise(cloudCoord * 7.0 - movement) * 0.125;
    noise += get_3d_noise((cloudCoord + movement) * 16.0) * 0.0625;
    
    const f32 top = 0.004;
    const f32 bottom = 0.01;
    
    f32 horizonHeight = p.y - cloudMinHeight;
    f32 treshHold = (1.0 - exp(-bottom * horizonHeight)) * exp(-top * horizonHeight);
    
    f32 clouds = smoothstep(0.55, 0.6, noise);
          clouds *= treshHold;
    
    return clouds * cloudDensity;
}

f32 getSunVisibility(vec3 p, Ray pos) {
	const int steps = cloudShadowingSteps;
    const f32 rSteps = cloudThickness / f32(steps);
    
    vec3 increment = pos.sun_direction * rSteps;
    vec3 position = increment * 0.5 + p;
    
    f32 transmittance = 0.0;
    
    for (int i = 0; i < steps; i++, position += increment) {
		transmittance += get_clouds(position);
    }
    
    return exp(-transmittance * rSteps);
}

f32 phase_two_lobes(f32 x) {
    const f32 m = 0.5;
    const f32 gm = 0.8;
    
	f32 lobe1 = henyey_greenstein_phase(x, 0.8 * gm);
    f32 lobe2 = henyey_greenstein_phase(x, -0.5 * gm);
    
    return mix(lobe2, lobe1, m);
}

vec3 get_volumetric_cloud_scattering(f32 opticalDepth, f32 phase, vec3 p, vec3 sunColor, vec3 skyLight, Ray pos) {
    f32 intergal = calculate_scatter_intergral(opticalDepth, 1.11);
    
    f32 beersPowder = powder(opticalDepth * log(2.0));
    
	vec3 sunlighting = (sunColor * getSunVisibility(p, pos) * beersPowder) * phase * hPi * sunBrightness;
    vec3 skylighting = skyLight * 0.25 * rPi;
    
    return (sunlighting + skylighting) * intergal * pi;
}

f32 get_fog(f32 height) {
	const f32 falloff = 0.001;
    
    return exp(-height * falloff) * fogDensity;
}

vec3 calculate_volumetric_clouds(Ray pos, vec3 color, f32 dither, vec3 sunColor) {
	const int steps = volumetricCloudSteps;
    const f32 iSteps = 1.0 / f32(steps);
    
    if (pos.ray_direction.y < 0.0)
       return color;
    
    f32 bottomSphere = rsi(vec3(0.0, 1.0, 0.0) * earthRadius, pos.ray_direction, earthRadius + cloudMinHeight).y;
    f32 topSphere = rsi(vec3(0.0, 1.0, 0.0) * earthRadius, pos.ray_direction, earthRadius + cloudMaxHeight).y;
    
    vec3 startPosition = pos.ray_direction * bottomSphere;
    vec3 endPosition = pos.ray_direction * topSphere;
    
    vec3 increment = (endPosition - startPosition) * iSteps;
    vec3 cloudPosition = increment * dither + startPosition;
    
    f32 stepLength = length(increment);
    
    vec3 scattering = vec3(0.0);
    f32 transmittance = 1.0;
    
    f32 lDotW = dot(pos.sun_direction, pos.ray_direction);
    f32 phase = phase_two_lobes(lDotW);
    
    vec3 skyLight = calculate_atmospheric_scattering_top(pos);
    
    for (int i = 0; i < steps; i++, cloudPosition += increment) {
        f32 opticalDepth = get_clouds(cloudPosition) * stepLength;
        
        if (opticalDepth <= 0.0) { continue; }
        //return color;
        
        //if(opticalDepth <= 0.0 && i > -1) { return color; }
        //if(i == 0) { return color; }
        
		scattering += get_volumetric_cloud_scattering(opticalDepth, phase, cloudPosition, sunColor, skyLight, pos) * transmittance;
        transmittance *= exp(-opticalDepth);
    }
    
    return mix(color * transmittance + scattering, color, clamp(length(startPosition) * 0.00001 * 2.5, 0.0, 1.0));
}

#define PI 3.141592
#define iSteps 16
#define jSteps 8

vec3 atmosphere(vec3 r, vec3 r0, vec3 pSun, float iSun, float rPlanet, float rAtmos, vec3 kRlh, float kMie, float shRlh, float shMie, float g) {
    //pSun = normalize(pSun);
    r = normalize(r);

    // Calculate the step size of the primary ray.
    vec2 p = rsi(r0, r, rAtmos);
    if (p.x > p.y) return vec3(0,0,0);
    p.y = min(p.y, rsi(r0, r, rPlanet).x);
    float iStepSize = (p.y - p.x) / float(iSteps);

    // Initialize the primary ray time.
    float iTime = globals.elapsed_time;

    // Initialize accumulators for Rayleigh and Mie scattering.
    vec3 totalRlh = vec3(0,0,0);
    vec3 totalMie = vec3(0,0,0);

    // Initialize optical depth accumulators for the primary ray.
    float iOdRlh = 0.0;
    float iOdMie = 0.0;

    // Calculate the Rayleigh and Mie phases.
    float mu = dot(r, pSun);
    float mumu = mu * mu;
    float gg = g * g;
    float pRlh = 3.0 / (16.0 * PI) * (1.0 + mumu);
    float pMie = 3.0 / (8.0 * PI) * ((1.0 - gg) * (mumu + 1.0)) / (pow(1.0 + gg - 2.0 * mu * g, 1.5) * (2.0 + gg));

    // Sample the primary ray.
    for (int i = 0; i < iSteps; i++) {

        // Calculate the primary ray sample position.
        vec3 iPos = r0 + r * (iTime + iStepSize * 0.5);

        // Calculate the height of the sample.
        float iHeight = length(iPos) - rPlanet;

        // Calculate the optical depth of the Rayleigh and Mie scattering for this step.
        float odStepRlh = exp(-iHeight / shRlh) * iStepSize;
        float odStepMie = exp(-iHeight / shMie) * iStepSize;

        // Accumulate optical depth.
        iOdRlh += odStepRlh;
        iOdMie += odStepMie;

        // Calculate the step size of the secondary ray.
        float jStepSize = rsi(iPos, pSun, rAtmos).y / float(jSteps);

        // Initialize the secondary ray time.
        float jTime = 0.0;

        // Initialize optical depth accumulators for the secondary ray.
        float jOdRlh = 0.0;
        float jOdMie = 0.0;

        // Sample the secondary ray.
        for (int j = 0; j < jSteps; j++) {

            // Calculate the secondary ray sample position.
            vec3 jPos = iPos + pSun * (jTime + jStepSize * 0.5);

            // Calculate the height of the sample.
            float jHeight = length(jPos) - rPlanet;

            // Accumulate the optical depth.
            jOdRlh += exp(-jHeight / shRlh) * jStepSize;
            jOdMie += exp(-jHeight / shMie) * jStepSize;

            // Increment the secondary ray time.
            jTime += jStepSize;
        }

        // Calculate attenuation.
        vec3 attn = exp(-(kMie * (iOdMie + jOdMie) + kRlh * (iOdRlh + jOdRlh)));

        // Accumulate scattering.
        totalRlh += odStepRlh * attn;
        totalMie += odStepMie * attn;

        // Increment the primary ray time.
        iTime += iStepSize;

    }

    // Calculate and return the final color.
    return iSun * (pRlh * kRlh * totalRlh + pMie * kMie * totalMie);
}

void main() {
    u32vec3 pixel = gl_GlobalInvocationID.xyz;
    if(any(greaterThanEqual(pixel.xy, globals.resolution))) { return; }

    vec2 ray_uv = f32vec2(pixel.xy) / f32vec2(globals.resolution.xy - 1.0.xx);
    vec2 ray_ndc = ray_uv * 2.0 - 1.0;
    vec4 ray_view_space = globals.camera_inverse_projection_matrix * vec4(ray_ndc, -1.0, 0.0);
    vec4 ray_world_space = globals.camera_inverse_view_matrix * vec4(ray_view_space.xy, -1.0, 0.0);

    Ray pos;
    pos.world_position = ray_world_space.xyz;
    pos.ray_direction = normalize(ray_world_space.xyz);
    pos.sun_direction = -globals.sun_info.direction;

    vec3 color = vec3(0.2f, 0.4f, 1.0f);
    //vec3 color = vec3(0.5f, 1.0f, 1.0f);

    f32 depth = textureLod(daxa_sampler2D(u_depth_image, globals.linear_sampler), ray_uv, 0).r;

    if(depth == 1.0f) {
        f32 dither = bayer16(f32vec2(pixel.xy));
        vec3 lightAbsorb = vec3(0.8);
        color = atmosphere(
            pos.ray_direction,         
            vec3(0,6372e3,0) + globals.camera_position,     
            pos.sun_direction,                        
            22.0,                           
            6371e3,  
            6471e3, 
            vec3(5.5e-6, 13.0e-6, 22.4e-6), 
            21e-6,                          
            8e3,                           
            1.2e3,                          
            0.758                           
        );
        color = calculate_volumetric_clouds(pos, color, dither, lightAbsorb);
        color *= max(min(abs(pos.sun_direction.x), abs(pos.sun_direction.z)) + pos.sun_direction.y, 0.0);
    }

    imageStore(daxa_image2D(u_target_image), i32vec2(pixel.xy),f32vec4(color, 1.0));
}

#endif
#undef WORKGROUP_SIZE