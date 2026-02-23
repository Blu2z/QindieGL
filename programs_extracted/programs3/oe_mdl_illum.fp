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

uniform float3		 cam_pos;			//camera position
uniform float2		 param_float2;		//camera position
uniform float		 param_float;		//camera position



#include"oe_illumination.cg"

////////////////////////////////////////////

float4 main(  SInData in_data ) :COLOR			
{
	float3 res_color;

	float4	c_diffuse = tex2D( tex_diffuse , in_data.tex0); 

	float3 normal;

#ifdef USE_NORMALMAP

	//get normal from normalmap...
	normal = tex2D( tex_normalmap , in_data.tex0 ).xyz;
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

	SIlluminationResult ir = illuminate(in_data.pos, normal);

	
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

	res_color = c_diffuse.xyz * ir.diffuse;

	//res_color.xyz = lerp(c_diffuse.xyz, res_color.xyz, c_diffuse.w);

#ifdef USE_SPECULAR
	res_color += ir.specular;	//adding specular term
#endif

#ifdef USE_FOG_INTERACTION
	return apply_fog(float4(res_color, 1.0f), in_data.to_eye);
#else
	return float4(res_color, 1.0f);
#endif

}
