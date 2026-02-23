/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Black and White
/////////////////////////////////////////////////


uniform	 samplerRECT scr0:TEXUNIT0;
uniform	 sampler2D   diffuse:TEXUNIT1;

uniform float3 filter;

float4 main(float2 TEX0 : TEXCOORD0,float2 TEX1 : TEXCOORD1 ):COLOR
{

	float4 out_color; 
	float3 scr_color=texRECT( scr0, TEX0 ).rgb;
	float3 filter_color=tex2D(diffuse,TEX1).rgb;
 
 
	//this version is better
	out_color.rgb=(dot(scr_color,float3(0.30,0.59,0.11))*filter_color);
	out_color.a=1.0;


	out_color.x *= filter.x;
	out_color.y *= filter.y;
	out_color.z *= filter.z;

	return out_color;

}
