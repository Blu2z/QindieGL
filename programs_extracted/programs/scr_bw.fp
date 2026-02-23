/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Black and White
/////////////////////////////////////////////////


uniform	 samplerRECT scr0:TEXUNIT0;

float4 main(
		        float2 TEX0 : TEXCOORD0
	 	     ):COLOR
{

 float4 out_color; 
 float3 scr_color=texRECT( scr0, TEX0 );
 
 
 //this version is better
out_color=dot(scr_color,float3(0.30,0.59,0.11));

//out_color=dot(scr_color,float3(0.33,0.33,0.33));

return out_color;

}
