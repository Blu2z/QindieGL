/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter.
// Kind of DOF. Something like that...
/////////////////////////////////////////////////


uniform	 samplerRECT scr:TEXUNIT0;
uniform	 samplerRECT blury_scr:TEXUNIT1;

uniform float scr_res_x;
uniform float scr_res_y;
uniform float intensivity;


float4 main(	float2 TEX0 : TEXCOORD0,
		        float2 TEX1 : TEXCOORD1
	 	     ):COLOR
{

    float4 out_color; 

    float4 screen = texRECT(scr, TEX0);
    float4 blury_screen = texRECT(blury_scr, TEX1);

	float2 center;
	
	center.x = scr_res_x/2.0;
	center.y = scr_res_y/2.0;
	
	float len=length(TEX0-center);
	
	len = (len/440.0) * intensivity;
	
	out_color.xyz = lerp(screen.xyz, blury_screen.xyz, len);                
	
		
	return out_color;
}

