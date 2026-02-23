/////////////////////////////////////////////////////////
//  Shader (bluring image). 4 samples
//  from source quad texture
/////////////////////////////////////////////////////////


uniform	 samplerRECT scr_image:TEXUNIT0;

float4 main(  float2 TEX0 : TEXCOORD0,
			  float2 TEX1 : TEXCOORD1,
			  float2 TEX2 : TEXCOORD2,
			  float2 TEX3 : TEXCOORD3
	  
	 	     ):COLOR
{

	float4 out_color;



	
	out_color=texRECT(scr_image,TEX0)+
			  texRECT(scr_image,TEX1)+
			  texRECT(scr_image,TEX2)+
			  texRECT(scr_image,TEX3);

	out_color*=0.25;
	

	return out_color;

}

