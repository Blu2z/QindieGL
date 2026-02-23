//////////////////////////////////////////////////////////////////////
//		Sample the luminance of the source image using a kernal of sample
//      points, and return a scaled image containing the log() of averages
//	    The per-color weighting to be used for luminance calculations in RGB order.
//////////////////////////////////////////////////////////////////////

uniform	 sampler2D scr_image:TEXUNIT0;

//static const float3 LUMINANCE_VECTOR  = float3(0.2125f, 0.7154f, 0.0721f);
static const float3 LUMINANCE_VECTOR  = float3(0.33f, 0.33f, 0.33f);

float4 main(  float2 TEX0 : TEXCOORD0,
			  float2 TEX1 : TEXCOORD1,
			  float2 TEX2 : TEXCOORD2,
			  float2 TEX3 : TEXCOORD3
  	 	     ):COLOR
{

	float4 out_color;

	float log_lum_sum=0;
	
	
	log_lum_sum+=log(dot(tex2D(scr_image,TEX0).xyz,LUMINANCE_VECTOR)+0.0001f);
	log_lum_sum+=log(dot(tex2D(scr_image,TEX1).xyz,LUMINANCE_VECTOR)+0.0001f);
	log_lum_sum+=log(dot(tex2D(scr_image,TEX2).xyz,LUMINANCE_VECTOR)+0.0001f);
	log_lum_sum+=log(dot(tex2D(scr_image,TEX3).xyz,LUMINANCE_VECTOR)+0.0001f);
	
	/*
	//just small test ...
	log_lum_sum+=(dot(tex2D(scr_image,TEX0).xyz,LUMINANCE_VECTOR)+0.0001f);
	log_lum_sum+=(dot(tex2D(scr_image,TEX1).xyz,LUMINANCE_VECTOR)+0.0001f);
	log_lum_sum+=(dot(tex2D(scr_image,TEX2).xyz,LUMINANCE_VECTOR)+0.0001f);
	log_lum_sum+=(dot(tex2D(scr_image,TEX3).xyz,LUMINANCE_VECTOR)+0.0001f);
	*/
	
	out_color=(log_lum_sum*.25);
	
	return out_color;
}

