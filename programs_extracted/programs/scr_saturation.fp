/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Saturation / Contrast
/////////////////////////////////////////////////


uniform	 samplerRECT scr:TEXUNIT0;



uniform float  saturation;
uniform float  contrast;

float4 main(
		        float2 TEX0 : TEXCOORD0
	 	     ):COLOR
{

	float4 out_color; 
	
 
	float4 scr_color = texRECT(scr, TEX0);
	float3 scr_color_bw = dot(float3(0.33, 0.59, 0.11), scr_color.xyz);

	out_color.xyz=lerp(scr_color_bw,scr_color.xyz,saturation);

	out_color-=float4(0.5,0.5,0.5,0.0);
	out_color*=contrast;
	out_color+=float4(0.5,0.5,0.5,0.0);

	return out_color;
		
}




