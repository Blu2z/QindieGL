/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
/////////////////////////////////////////////////



struct outdata
{
    float4 color : COLOR;
};


outdata main(float2 TEX0   : TEXCOORD0, //DIFFUSE
             float3 TEX1   : TEXCOORD1, //light direction in tangent space
			 float3 TEX2 : TEXCOORD2, //half - angle
			 float4 COLOR  : COLOR0,
			 uniform sampler2D	 diffuse:TEXUNIT1,
			 uniform sampler2D	 normalmap:TEXUNIT2
  
	     )
{
   outdata OUT;


    float3 normal=tex2D(normalmap,TEX0).xyz;
    
	normal=(normal-0.5f)*2.f; //expand normal


   float diffuse_int=saturate(dot(normal,TEX1));

   float spec_int=saturate(dot(normal,TEX2));

   float spec_int2=spec_int*spec_int;
   float spec_int4=spec_int2*spec_int2;
   float spec_int8=spec_int4*spec_int4;
   float spec_int16=spec_int8*spec_int8;


   OUT.color=tex2D(diffuse,TEX0)*diffuse_int*COLOR+spec_int16*0.5*COLOR;


 //  OUT.color=COLOR;
   //OUT.color*=float4(1.0,0.4,0.1,1.0);
   
   return OUT;
}


