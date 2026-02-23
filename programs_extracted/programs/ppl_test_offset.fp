/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
/////////////////////////////////////////////////



struct outdata
{
    float4 color : COLOR;
};


outdata main(float2 TEX0   : TEXCOORD0, //DIFFUSE
             float3 TEX1   : TEXCOORD1, //light direction in tangent space
			 float3 TEX2 : TEXCOORD2, //view dir in tangent space
			 float4 COLOR  : COLOR0,
			 uniform sampler2D	 diffuse:TEXUNIT1,
			 uniform sampler2D	 normalmap:TEXUNIT2,
			 uniform sampler2D	 height_tex:TEXUNIT3
  
	     )
{
   outdata OUT;



	float3 view_dir=normalize(TEX2);

	float height = tex2D(height_tex,TEX0).z;

//	OUT.color=tex2D(height_tex,TEX0);//height;
//	return OUT;

	float2 newTexCoord = TEX0 + view_dir.xy * (height * 2.0 - 1.0) * 0.04;


    float3 normal=tex2D(normalmap,newTexCoord).xyz;
   	normal=(normal-0.5f)*2.f; //expand normal


   float diffuse_int=saturate(dot(normal,TEX1));




   OUT.color=tex2D(diffuse,newTexCoord)*diffuse_int;


 //  OUT.color=COLOR;
   //OUT.color*=float4(1.0,0.4,0.1,1.0);
   
   return OUT;
}


