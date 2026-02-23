/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Screen flash
/////////////////////////////////////////////////


uniform	 samplerRECT scr:TEXUNIT0;
uniform	 samplerRECT blury_scr:TEXUNIT1;

uniform float  intensivity;
uniform float3 filter;

float4 main(   float2 TEX0 : TEXCOORD0,  float2 TEX1 : TEXCOORD1  ):COLOR
{

	float4 out_color; 
 
    float4 screen_color = texRECT(scr, TEX0);
    float4 blury_screen_color = texRECT(blury_scr, TEX1);

	float flash_bang_w = intensivity;

    float3 color = (blury_screen_color.xyz * 4) + flash_bang_w;
    
    out_color.xyz = lerp(screen_color.xyz, color.xyz, flash_bang_w);
    out_color.a = saturate(1-flash_bang_w)*0.75;

	out_color.x *= filter.x;
	out_color.y *= filter.y;
	out_color.z *= filter.z;



	return out_color;
}




