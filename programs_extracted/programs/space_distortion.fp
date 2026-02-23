/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Space distortion effect
/////////////////////////////////////////////////

struct outdata
{
    float4 color : COLOR;
};


uniform float distortion_intensivity;
uniform float distortion_offset_intensivity;
uniform float diffuse_intensivity;

outdata main(
             float4 TEX0 : TEXCOORD0, 
             float2 TEX1 : TEXCOORD1,
             float2 TEX2 : TEXCOORD2,
	             
			 uniform samplerRECT screen_tex:TEXUNIT0,
			 uniform sampler2D   distortion_texture:TEXUNIT1,         //distortion texture are normalmap
			 uniform sampler2D   diffuse_texture:TEXUNIT2,
			 
			 uniform float4   EyePos,
		     uniform float4x4 ModelView
	     
	     )
{

   outdata OUT;

   
   
   float2 scr_tex_coords;
  
   float4 distortion_texture_color=tex2D(distortion_texture,TEX1);
   
   scr_tex_coords.x=(TEX0.x/TEX0.w)+distortion_texture_color.r*distortion_offset_intensivity;
   scr_tex_coords.y=(TEX0.y/TEX0.w)+distortion_texture_color.g*distortion_offset_intensivity;
   
   OUT.color=texRECT(screen_tex,scr_tex_coords)*distortion_intensivity+tex2D(distortion_texture,TEX1)*diffuse_intensivity;
   
   
   return OUT;

}
