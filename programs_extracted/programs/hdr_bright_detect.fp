///////////////////////////////////////////////////////////////////////////////
// Bright dectection pass.Detecting bright regions. Source for Bloom filter
//
///////////////////////////////////////////////////////////////////////////////


uniform float  BRIGHT_PASS_THRESHOLD  ;	 //.3f;  // Threshold for BrightPass filter
uniform float  BRIGHT_PASS_OFFSET     ; //.8 Offset for BrightPass filter




uniform  float  MIDDLE_GRAY;
uniform  float  SCREEN_LUMINANCE;

uniform	 sampler2D scr_image:TEXUNIT0;

float4 main(  float2 TEX0 : TEXCOORD0,
	          float4 COLOR: COLOR0 //scene luminance
	 	     ):COLOR
{

	float4 scr_color = tex2D( scr_image,TEX0);
	
	
	// Determine what the pixel's value will be after tone-mapping occurs
	scr_color.rgb *= (MIDDLE_GRAY/(SCREEN_LUMINANCE + 0.001f));

	// Subtract out dark pixels
	scr_color.rgb -= BRIGHT_PASS_THRESHOLD;
	
	// Clamp to 0
	scr_color = max(scr_color, 0.0f);
	
	// Map the resulting value into the 0 to 1 range. Higher values for
	// BRIGHT_PASS_OFFSET will isolate lights from illuminated scene 
	// objects.
	scr_color.rgb /= (BRIGHT_PASS_OFFSET+scr_color);
	   
	return scr_color;

}

