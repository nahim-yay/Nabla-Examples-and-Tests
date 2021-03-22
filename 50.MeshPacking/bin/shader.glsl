#version 460 core

	#define NBL_IMPL_GL_NV_viewport_array2
	#define NBL_IMPL_GL_NV_stereo_view_rendering
	#define NBL_IMPL_GL_NV_sample_mask_override_coverage
	#define NBL_IMPL_GL_NV_geometry_shader_passthrough
	#define NBL_IMPL_GL_NV_shader_subgroup_partitioned
	#define NBL_IMPL_GL_ARB_shading_language_include
	#define NBL_IMPL_GL_ARB_enhanced_layouts
	#define NBL_IMPL_GL_ARB_bindless_texture
	#define NBL_IMPL_GL_ARB_shader_draw_parameters
	#define NBL_IMPL_GL_ARB_shader_group_vote
	#define NBL_IMPL_GL_ARB_cull_distance
	#define NBL_IMPL_GL_ARB_derivative_control
	#define NBL_IMPL_GL_ARB_shader_texture_image_samples
	#define NBL_IMPL_GL_KHR_blend_equation_advanced
	#define NBL_IMPL_GL_KHR_blend_equation_advanced_coherent
	#define NBL_IMPL_GL_ARB_fragment_shader_interlock
	#define NBL_IMPL_GL_ARB_gpu_shader_int64
	#define NBL_IMPL_GL_ARB_post_depth_coverage
	#define NBL_IMPL_GL_ARB_shader_ballot
	#define NBL_IMPL_GL_ARB_shader_clock
	#define NBL_IMPL_GL_ARB_shader_viewport_layer_array
	#define NBL_IMPL_GL_ARB_sparse_texture2
	#define NBL_IMPL_GL_ARB_sparse_texture_clamp
	#define NBL_IMPL_GL_ARB_gl_spirv
	#define NBL_IMPL_GL_ARB_spirv_extensions
	#define NBL_IMPL_GL_AMD_vertex_shader_viewport_index
	#define NBL_IMPL_GL_AMD_vertex_shader_layer
	#define NBL_IMPL_GL_NV_bindless_texture
	#define NBL_IMPL_GL_NV_shader_atomic_float
	#define NBL_IMPL_GL_EXT_shader_integer_mix
	#define NBL_IMPL_GL_NV_shader_thread_group
	#define NBL_IMPL_GL_NV_shader_thread_shuffle
	#define NBL_IMPL_GL_EXT_shader_image_load_formatted
	#define NBL_IMPL_GL_NV_shader_atomic_int64
	#define NBL_IMPL_GL_EXT_post_depth_coverage
	#define NBL_IMPL_GL_EXT_sparse_texture2
	#define NBL_IMPL_GL_NV_fragment_shader_interlock
	#define NBL_IMPL_GL_NV_sample_locations
	#define NBL_IMPL_GL_NV_shader_atomic_fp16_vector
	#define NBL_IMPL_GL_NV_command_list
	#define NBL_IMPL_GL_OVR_multiview
	#define NBL_IMPL_GL_OVR_multiview2
	#define NBL_IMPL_GL_NV_shader_atomic_float64
	#define NBL_IMPL_GL_NV_gpu_shader5

#ifdef NBL_IMPL_GL_AMD_gpu_shader_half_float
#define NBL_GL_EXT_shader_explicit_arithmetic_types_float16
#endif

#ifdef NBL_IMPL_GL_NV_gpu_shader5
#define NBL_GL_EXT_shader_explicit_arithmetic_types_float16
#define NBL_GL_EXT_nonuniform_qualifier
#define NBL_GL_KHR_shader_subgroup_vote_subgroup_any_all_equal_bool
#endif

#ifdef NBL_IMPL_GL_AMD_gpu_shader_int16
#define NBL_GL_EXT_shader_explicit_arithmetic_types_int16
#endif

#ifdef NBL_IMPL_GL_NV_shader_thread_group
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_mask
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_size
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_invocation_id
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_ballot
#define NBL_GL_KHR_shader_subgroup_ballot_inverse_ballot_bit_count
#endif

#if defined(NBL_IMPL_GL_ARB_shader_ballot) && defined(NBL_IMPL_GL_ARB_shader_int64)
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_mask
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_size
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_invocation_id
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_broadcast_first
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_ballot
#define NBL_GL_KHR_shader_subgroup_ballot_inverse_ballot_bit_count
#endif

#if defined(NBL_IMPL_GL_AMD_gcn_shader) && (defined(NBL_IMPL_GL_AMD_gpu_shader_int64) || defined(NBL_IMPL_GL_NV_gpu_shader5))
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_size
#define NBL_GL_KHR_shader_subgroup_vote_subgroup_any_all_equal_bool
#endif

#ifdef NBL_IMPL_GL_NV_shader_thread_shuffle
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_broadcast_first
#endif

#ifdef NBL_IMPL_GL_ARB_shader_group_vote
#define NBL_GL_KHR_shader_subgroup_vote_subgroup_any_all_equal_bool
#endif

#if defined(NBL_GL_KHR_shader_subgroup_ballot_subgroup_broadcast_first) && defined(NBL_GL_KHR_shader_subgroup_vote_subgroup_any_all_equal_bool)
#define NBL_GL_KHR_shader_subgroup_vote_subgroup_all_equal_T
#endif

#if defined(NBL_GL_KHR_shader_subgroup_ballot_subgroup_ballot) && defined(NBL_GL_KHR_shader_subgroup_basic_subgroup_invocation_id)
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_elect
#endif

#ifdef NBL_GL_KHR_shader_subgroup_ballot_subgroup_mask
#define NBL_GL_KHR_shader_subgroup_ballot_inverse_ballot
#define NBL_GL_KHR_shader_subgroup_ballot_inclusive_bit_count
#define NBL_GL_KHR_shader_subgroup_ballot_exclusive_bit_count
#endif

#ifdef NBL_GL_KHR_shader_subgroup_ballot_subgroup_ballot
#define NBL_GL_KHR_shader_subgroup_ballot_bit_count
#endif

// the natural extensions TODO: @Crisspl implement support for https://www.khronos.org/registry/OpenGL/extensions/KHR/KHR_shader_subgroup.txt
#ifdef NBL_IMPL_GL_KHR_shader_subgroup_basic
#define NBL_GL_KHR_shader_subgroup_basic
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_size
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_invocation_id
#define NBL_GL_KHR_shader_subgroup_basic_subgroup_elect
#endif

#ifdef NBL_IMPL_GL_KHR_shader_subgroup_vote
#define NBL_GL_KHR_shader_subgroup_vote
#define NBL_GL_KHR_shader_subgroup_vote_subgroup_any_all_equal_bool
#define NBL_GL_KHR_shader_subgroup_vote_subgroup_all_equal_T
#endif

#ifdef NBL_IMPL_GL_KHR_shader_subgroup_ballot
#define NBL_GL_KHR_shader_subgroup_ballot
#define NBL_GL_KHR_shader_subgroup_ballot_bit_count
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_mask
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_ballot
#define NBL_GL_KHR_shader_subgroup_ballot_inclusive_bit_count
#define NBL_GL_KHR_shader_subgroup_ballot_exclusive_bit_count
#define NBL_GL_KHR_shader_subgroup_ballot_inverse_ballot_bit_count
#define NBL_GL_KHR_shader_subgroup_ballot_subgroup_broadcast_first
#endif

// TODO: do a SPIR-V Cross contribution to do all the fallbacks (later)
#ifdef NBL_IMPL_GL_KHR_shader_subgroup_shuffle
#define NBL_GL_KHR_shader_subgroup_shuffle
#endif

#ifdef NBL_IMPL_GL_KHR_shader_subgroup_shuffle_relative
#define NBL_GL_KHR_shader_subgroup_shuffle_relative
#endif

#ifdef NBL_IMPL_GL_KHR_shader_subgroup_arithmetic
#define NBL_GL_KHR_shader_subgroup_arithmetic
#endif

#ifdef NBL_IMPL_GL_KHR_shader_subgroup_clustered
#define NBL_GL_KHR_shader_subgroup_clustered
#endif

#ifdef NBL_IMPL_GL_KHR_shader_subgroup_quad
#define NBL_GL_KHR_shader_subgroup_quad
#endif
#line 2

layout(location = 0) in vec3 normal;
layout(location = 0) out vec4 color;

void main()
{
    vec3 colorTmp = clamp(dot(vec3(0.0, 1.0, 0.0), normal), 0, 1) * vec3(1.0) + vec3(0.2);
    color = vec4(colorTmp, 1.0);

    //color = vec4(1.0);
} 