#include "oe_common.cg"



#ifdef USE_FOG_INTERACTION
	#include "fog.cg"
#endif


#define DISPLACE_AMOUNT  0.06

struct SInData
{
	float2 tex0		: TEXCOORD0; //diffuse texture
	float3 pos		: TEXCOORD1;
    float3 norm		: TEXCOORD2;

#ifdef USE_NORMALMAP
    float3 tang		: TEXCOORD3;
    float3 binorm	: TEXCOORD4;
#endif

#if defined(USE_ENV_CUBEMAP) || defined(USE_FOG_INTERACTION)
	float3 to_eye	: TEXCOORD5;
#endif
};

////////////////////////////////////////////

uniform sampler2D	 tex_diffuse:		TEXUNIT0;
uniform sampler2D	 tex_normalmap:		TEXUNIT1;
uniform samplerCUBE	 tex_cube:			TEXUNIT2;
uniform sampler2D	 tex_opacity:		TEXUNIT3;

uniform float3		 cam_pos;			//camera position
uniform float2		 param_float2;		//camera position
uniform float		 param_float;		//camera position

uniform float		 specular_intensivity;

uniform float		 alpha_value;

#include"oe_illumination.cg"

////////////////////////////////////////////

float4 main(  SInData in_data ) :COLOR			
{
	float4 res_color;

	float4	c_diffuse = tex2D( tex_diffuse , in_data.tex0); 

	float3 normal;

#ifdef USE_NORMALMAP

	//get normal from normalmap...

	float4 c_normalmap = tex2D( tex_normalmap , in_data.tex0 );
	normal = c_normalmap.xyz;
	normal = expand( normal );

	//transform by tangent space matrix
	float3x3 rotation = float3x3(	in_data.tang,
        							in_data.binorm,
				  					in_data.norm);
	
	normal = mul( normal, rotation );

#else
	normal = in_data.norm;
#endif

#ifdef USE_PER_PIXEL_NORMALIZE		
	normal = normalize(normal);	//u+se per pixel normaliation
#endif


float specular_intensivity_modulator = 0.7f;


#ifdef USE_SPECULAR

	#ifdef SPECULAR_INTENSIVITY_ADIFF
		specular_intensivity_modulator = c_diffuse.w;
	#else
		#if defined(USE_NORMALMAP) && defined(SPECULAR_INTENSIVITY_ANORM)
			specular_intensivity_modulator = c_normalmap.w;
		#else
				specular_intensivity_modulator = specular_intensivity; //use external defined specular intensivity
		#endif

	#endif

#endif


SIlluminationResult ir = illuminate(in_data.pos, normal, specular_intensivity_modulator);

	
#ifdef USE_ENV_CUBEMAP

 	float3 V = normalize(in_data.to_eye); 

	float3 refl_vec = reflect( -V, normal );

	float3 refl_color = texCUBE( tex_cube, refl_vec );

	#ifdef ENV_CUBEMAP_COMBINE_ADD_DIFFUSE 
		c_diffuse.xyz += refl_color;
	#endif

	#ifdef ENV_CUBEMAP_COMBINE_MODULATE_DIFFUSE
		c_diffuse.xyz *= refl_color;
	#endif

	#ifdef ENV_CUBEMAP_COMBINE_REPLACE_DIFFUSE
		c_diffuse.xyz = refl_color;
	#endif

	#ifdef ENV_CUBEMAP_COMBINE_BLEND_DIFFUSE
		c_diffuse.xyz = lerp(c_diffuse.xyz, refl_color, c_diffuse.w);
	#endif


#endif


	res_color.xyz = c_diffuse.xyz * ir.diffuse;

#ifdef SELF_ILLUMINATION_ALPHA_DIFFUSE
	res_color.xyz = lerp(c_diffuse.xyz, res_color.xyz, c_diffuse.w);
#endif

#ifdef USE_SPECULAR
	res_color.xyz += ir.specular;	//adding specular term
#endif


//add alpha to result color

	res_color.w = c_diffuse.w; //by default

#ifdef ALPHA_SOURCE_ALPHA_DIFFUSE
	res_color.w = c_diffuse.w;
#endif

#if defined(USE_NORMALMAP) && defined(ALPHA_SOURCE_ALPHA_NORMAL)
	res_color.w = c_diffuse.w;
#endif

#ifdef ALPHA_SOURCE_ALPHA_OPACITYMAP
	float4	c_opacity = tex2D( tex_opacity, in_data.tex0); 
	res_color.w = c_opacity.w;
#endif

#ifdef ALPHA_SOURCE_CONSTANT
	res_color.w = alpha_value;
#endif

//////////////////////////////////


#ifdef USE_FOG_INTERACTION
	return apply_fog(res_color, in_data.to_eye);
#else
	return res_color;
#endif


}
