//////////////////////////////////////////////////////
//Stereoscopic render. We filter Blue channel.
// Using just red for drawning , or green
//////////////////////////////////////////////////////

uniform	 samplerRECT scr0:TEXUNIT0;

uniform  float2  filter;

float4 main(float2 TEX0 : TEXCOORD0):COLOR
{

 float4 out_color; 


 float4 sample_color;
 
  sample_color= texRECT( scr0, TEX0 );


 out_color.r=(sample_color.r+sample_color.b/2.0)*filter.x;
 out_color.g=(sample_color.g+sample_color.b/2.0)*filter.y;
 out_color.b=0.0; //No blue!
 out_color.a=0.0;


 return out_color;

}







