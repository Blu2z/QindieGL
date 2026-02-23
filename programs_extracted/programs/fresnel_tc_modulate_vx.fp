/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Fresnel fragment shader.For vertex lighted surfaces
// Textures combine: diffuse*fresnel*vertex_colors
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
             float3 TEX3 : TEXCOORD3, //vertex colors
             
             float3 COL  : COLOR0, //fresnel

	     uniform samplerCUBE cube:TEXUNIT0,
		 uniform sampler2D diffuse:TEXUNIT1        
     
	     )
{
   outdata OUT;

    float3 refractColor = (texCUBE(cube, TEX2).rgb)*fresnel_refract_intens;
    
    float3 reflectColor = (texCUBE(cube, TEX1).rgb)*fresnel_reflect_intens; 

    float3 reflectRefract = lerp(refractColor, reflectColor, COL);
    

    
    
    OUT.color.rgb=reflectRefract*tex2D(diffuse,TEX0)*TEX3*2.0;
    OUT.color.a=COL.x; //alpha - fresnel intensivity
    
   return OUT;
}
////////////////////////////////////////////////

