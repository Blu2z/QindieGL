/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Environment Mapped Bump Mapping
// Pixel shader for static vertex light 
// Combine env_cubemap + diffuse_texture
/////////////////////////////////////////////////



struct outdata
{
    float4 color : COLOR;
};


outdata main(float3 TEX0		: TEXCOORD0, //transformed eye to vert
             float2 TEX1		: TEXCOORD1, //normalmap / diffuse UV mapping
             
			 float3 TANGENT		: TEXCOORD2,
			 float3 BINORMAL	: TEXCOORD3,
			 float3 NORMAL		: TEXCOORD4,
             
             float4 COLOR: COLOR0,


			uniform samplerCUBE env_cubemap:TEXUNIT0,
			uniform sampler2D	 normalmap:TEXUNIT1,        
			uniform sampler2D	 diffuse:TEXUNIT2
	     )
{
   outdata OUT;
   
    float3 normal=tex2D(normalmap,TEX1).xyz;
    
	normal=(normal-0.5f)*2.f; //expand normal from normalmap
 


//matrix of transformation from tangent space to object space
//this matrix are inverted tangent space matrix
//NOTE: inversion of rotation matrix(3x3) are just transposed matrix!
	float3x3 tangent_to_object=float3x3(TANGENT,
								  	    BINORMAL,
									    NORMAL);
								   


	//invert(transpose) matrix
	normal=normalize(normal);
	tangent_to_object=transpose(tangent_to_object);

	//get normal in object space
	float3 tr_normal=mul(tangent_to_object,normal);	


	float3 refl_vec=reflect(TEX0,tr_normal);

	OUT.color=texCUBE(env_cubemap,refl_vec)*COLOR*2.0;
	
  
  return OUT;
									
}


