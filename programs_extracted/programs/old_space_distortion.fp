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

      
   float3 proj_texcoord=TEX0.xyw;
   
   float4 normal_tmp=tex2D(distortion_texture,TEX1);
   float3 normal;
   
   normal.x=normal_tmp.x;
   normal.y=normal_tmp.y;
   normal.z=normal_tmp.z;
   
   
   normal=(normal-0.5f)*2.f; //expand normal

   normal*=285.0;
   
   float3 xformedEye;
  // xformedEye=TEX2;
   
   xformedEye.xyz=mul(-EyePos,ModelView);
   xformedEye=normalize(mul(-EyePos,ModelView)).xyz;
   
   
   // Compute reflection vector
    float3 reflection;
   
	reflection.xyz = dot(normal, xformedEye) * 2 * normal - (dot(normal,normal) * xformedEye);

   
  
   //////////////////// this is projection !!! //////////////////////////////
   
   //works OK!
   float2 rez1;
   
   
   
   
   rez1.x=reflection.x;//*70.0;
   rez1.y=reflection.y;//*70.0;

   //rez1.x+=TEX0.x/TEX0.w;
   //rez1.y+=TEX0.y/TEX0.w;
  
  // OK!  
   rez1.x=(TEX0.x/TEX0.w)+tex2D(distortion_texture,TEX1).r*20.0;
  rez1.y=(TEX0.y/TEX0.w)+tex2D(distortion_texture,TEX1).g*20.0;
   OUT.color=texRECT(screen_tex,rez1)+tex2D(distortion_texture,TEX1)*0.1;


//test blur!

/*
   OUT.color=0;
   rez1.x=(TEX0.x/TEX0.w);
   rez1.y=(TEX0.y/TEX0.w);
   OUT.color+=texRECT(screen_tex,rez1);
   
   rez1.x=(TEX0.x/TEX0.w)+5.0;
   rez1.y=(TEX0.y/TEX0.w)+5.0;
   OUT.color+=texRECT(screen_tex,rez1);
   
   rez1.x=(TEX0.x/TEX0.w)+5.0;
   rez1.y=(TEX0.y/TEX0.w)-5.0;
   OUT.color+=texRECT(screen_tex,rez1);

   rez1.x=(TEX0.x/TEX0.w)-5.0;
   rez1.y=(TEX0.y/TEX0.w)-5.0;
   OUT.color+=texRECT(screen_tex,rez1);

   rez1.x=(TEX0.x/TEX0.w)-5.0;
   rez1.y=(TEX0.y/TEX0.w)+5.0;
   OUT.color+=texRECT(screen_tex,rez1);
   
   OUT.color/=5.0;

   rez1.x=(TEX0.x/TEX0.w);
   rez1.y=(TEX0.y/TEX0.w);
   OUT.color=texRECT(screen_tex,rez1)-OUT.color;
   
   
  // */
   
   
   
   return OUT;

}
