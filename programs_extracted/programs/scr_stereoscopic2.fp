//////////////////////////////////////////////////////
//Stereoscopic render. We filter Blue channel.
// Using just red for drawning , or green
//////////////////////////////////////////////////////

uniform	 samplerRECT scr0_r:TEXUNIT0;
uniform	 samplerRECT scr0_g:TEXUNIT0;

uniform  float2  filter;
uniform  float   offset;

float4 main(float2 TEX0 : TEXCOORD0):COLOR
{

 float4 out_color; 


 float4 sample_color_r,sample_color_g;
 
  sample_color_r= texRECT( scr0_r, TEX0 );
  sample_color_g= texRECT( scr0_g, TEX0 );
  
  float val;
  
  offset=0;

  val=ceil((TEX0.x+offset)/2.0)-((TEX0.x+offset)/2.0);

  if(val>=0.5) 
 {
 
	out_color.r=1.0;//(sample_color_r.r+sample_color_r.b/2.0);
	out_color.g=0.0;
	out_color.b=0.0; //No blue!
	out_color.a=0.0;
 }
 else
 {
	out_color.r=0.0;
	out_color.g=1.0;//(sample_color_g.g+sample_color_g.b/2.0);
	out_color.b=0.0; //No blue!
	out_color.a=0.0;
 }


 return out_color;

}







