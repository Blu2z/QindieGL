//////////////////////////////////////////////////////
// screen filter. RGB separation
//////////////////////////////////////////////////////

uniform	 samplerRECT scr0:TEXUNIT0;


uniform  float  offset;
uniform  float3 colors_filter;

float4 main(
		        float2 TEX0 : TEXCOORD0
	 	     ):COLOR
{

 float4 out_color; 


 float4 sample_r;
 float4 sample_g;
 float4 sample_b;

 
 sample_r= texRECT( scr0, TEX0 );
 sample_g= texRECT( scr0, TEX0+float2(offset,0.0) );
 sample_b= texRECT( scr0, TEX0-float2(offset,0.0) );

 
 out_color.r=(sample_r.r)*colors_filter.x;
 out_color.g=(sample_g.g)*colors_filter.y;
 out_color.b=(sample_g.b)*colors_filter.z;
 out_color.a=1.0;
 

return out_color;

}







