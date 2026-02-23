/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Environment Mapped Bump Mapping
// Pixel shader for static vertex lighted
// Combine env_cubemap blend diffuse_texture
/////////////////////////////////////////////////

#include "fog.cg"


struct outdata
{
    float4 color : COLOR;
};


outdata main(float3 eye_to_vert : TEXCOORD0, //transformed eye to vert
             float2 TEX1 : TEXCOORD1, //normalmap / diffuse UV mapping
             float4 COLOR: COLOR0,
             
			 float3 TANGENT		: TEXCOORD2,
			 float3 BINORMAL	: TEXCOORD3,
			 float3 NORMAL		: TEXCOORD4,


	     uniform samplerCUBE env_cubemap:TEXUNIT0,
		 uniform sampler2D	 normalmap:TEXUNIT1,        
		 uniform sampler2D	 diffuse:TEXUNIT2
	     	     
	     )
{
   outdata OUT;

    float3 normal	= tex2D(normalmap,TEX1).xyz;
  	 normal			= (normal - 0.5f) * 2.f; //expand normal

	float3x3 tangent_to_object = float3x3(	TANGENT,
									 	    BINORMAL,
											NORMAL);

	 float3 eye_to_vert_normalized = normalize(eye_to_vert);

	//get normal in object space
	float3 tr_normal	=	mul(tangent_to_object,normal);	
	float3 refl_vec		=	reflect(eye_to_vert_normalized, tr_normal);


    
	// calculating blend   
    float4 diffuseColor=tex2D(diffuse,TEX1); 
    float3 blendedColor=( texCUBE(env_cubemap,refl_vec).xyz * ( 1.0 - diffuseColor.a ) ) + ( diffuseColor.rgb * diffuseColor.a);
   
   OUT.color.xyz=blendedColor*COLOR.xyz*2.0;
   OUT.color.w=COLOR.w;
   
   OUT.color = apply_fog(OUT.color, eye_to_vert);

   return OUT;
}


