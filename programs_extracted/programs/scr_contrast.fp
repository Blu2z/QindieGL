/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Contrast
/////////////////////////////////////////////////


uniform	 samplerRECT scr0:TEXUNIT0;

uniform float contrast;

float4 main(float2 TEX0 : TEXCOORD0 ):COLOR
{

	float4 out_color; 
	 
	 
	out_color = texRECT(scr0, TEX0);
	 
	out_color-=float4(0.5,0.5,0.5,0.0);
	out_color*=contrast;
	out_color+=float4(0.5,0.5,0.5,0.0);

	return out_color;

}
