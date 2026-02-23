/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Environment Mapped Bump Mapping
// Pixel shader for static vertex light 
// Combine env_cubemap + diffuse_texture
/////////////////////////////////////////////////
#include "fog.cg"



struct outdata
{
    float4 color : COLOR;
};

uniform samplerCUBE		env_cubemap	:TEXUNIT0;
uniform sampler2D		normalmap	:TEXUNIT1;        
uniform sampler2D		diffuse		:TEXUNIT2;


outdata main(float3 eye_to_vert		: TEXCOORD0, 
             float2 TEX1			: TEXCOORD1, 
			 float3 TANGENT			: TEXCOORD2,
			 float3 BINORMAL		: TEXCOORD3,
			 float3 NORMAL			: TEXCOORD4,
             float4 COLOR			: COLOR0)
{
   outdata OUT;
  
    float3 normal	= tex2D(normalmap,TEX1).xyz;
	normal			= (normal - 0.5f) * 2.f;
	float3x3 tangent_to_object = float3x3(TANGENT, BINORMAL, NORMAL);

	float3 eye_to_vert_normalized = normalize(eye_to_vert);

	//get normal in object space
	float3 tr_normal	=	mul(tangent_to_object,normal);	
	float3 refl_vec		=	reflect(eye_to_vert_normalized, tr_normal);


 	OUT.color=texCUBE(env_cubemap,refl_vec)*COLOR*2.0;
	
     OUT.color = apply_fog(OUT.color, eye_to_vert);

  return OUT;
									
}


