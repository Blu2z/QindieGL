/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Fresnel fragment shader.For lightmapped surfaces
// Textures combine: diffuse*fresnel*lightmap
/////////////////////////////////////////////////

struct outdata
{
    float4 color : COLOR;
};


uniform float fresnel_refract_intens;
uniform float fresnel_reflect_intens;

outdata main(
             
			 float2 TEX0 : TEXCOORD0, //diffuse texture
			 float3 TEX1 : TEXCOORD1, //Reflection
			 float3 TEX2 : TEXCOORD2, //Refractiom
             float2 TEX3 : TEXCOORD3, //lightmap
             
             float3 COL  : COLOR0, //fresnel

	     uniform samplerCUBE cube:TEXUNIT0,
		 uniform sampler2D diffuse:TEXUNIT1,        
		 uniform sampler2D lightmap:TEXUNIT2        
     
	     )
{
   outdata OUT;

    float3 refractColor = (texCUBE(cube, TEX2).rgb)*fresnel_refract_intens;
    
    float3 reflectColor = (texCUBE(cube, TEX1).rgb)*fresnel_reflect_intens; 

    float3 reflectRefract = lerp(refractColor, reflectColor, COL);
    

    
    
    OUT.color.rgb=reflectRefract*tex2D(diffuse,TEX0)*tex2D(lightmap,TEX3)*2.0;
    OUT.color.a=COL.x; //alpha - fresnel intensivity
    
   return OUT;
}
////////////////////////////////////////////////

