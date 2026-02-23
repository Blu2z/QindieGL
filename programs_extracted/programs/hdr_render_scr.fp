///////////////////////////////////////////////////////////////////////////////
//  Final pass of HDR. Tone map the scene, and add post-processed light
//  effects.
// Version Witchout blue shift
///////////////////////////////////////////////////////////////////////////////



// The per-color weighting to be used for luminance calculations in RGB order.
//static const float3 LUMINANCE_VECTOR  = float3(0.2125f, 0.7154f, 0.0721f);
static const float3 LUMINANCE_VECTOR  = float3(0.33f, 0.33f, 0.33f);

// The per-color weighting to be used for blue shift under low light.
static const float3 BLUE_SHIFT_VECTOR = float3(1.05f, 0.97f, 1.27f); 


uniform	 sampler2D scr_image:TEXUNIT0;
uniform  sampler2D scr_bloom:TEXUNIT1;


uniform  float SCREEN_LUMINANCE;
uniform  float MIDDLE_GRAY;
uniform  float BLOOM_SCALE_FACTOR;
uniform  float BASE_SCALE_FACTOR;


float4 main(  float2 TEX0 : TEXCOORD0 ):COLOR
{



	float4 scr_color=tex2D(scr_image,TEX0);
	float4 bloom_color=tex2D(scr_bloom,TEX0);
	

	/*
	//if(enable_blue_shift)
	//{
	
	// For very low light conditions, the rods will dominate the perception
    // of light, and therefore color will be desaturated and shifted
    // towards blue.
    
		// Define a linear blending from -1.5 to 2.6 (log scale) which
		// determines the lerp amount for blue shift
        float fBlueShiftCoefficient = 1.0f - (SCREEN_LUMINANCE + 3.5)/4.1;
        fBlueShiftCoefficient = saturate(fBlueShiftCoefficient);

		// Lerp between current color and blue, desaturated copy
        float3 vRodColor = dot( (float3)scr_color, LUMINANCE_VECTOR ) * BLUE_SHIFT_VECTOR;
        scr_color.rgb = lerp( (float3)scr_color, vRodColor, fBlueShiftCoefficient );
    //*/
	//}
        
    
	
	// Map the high range of color values into a range appropriate for
    // display, taking into account the user's adaptation level, and selected
    // values for for middle gray and white cutoff.

//was.
//scr_color.rgb *= MIDDLE_GRAY/(SCREEN_LUMINANCE + 0.001f);
//scr_color.rgb /= (1.0f+scr_color.rgb);


	/*
	//test vignette from HDR ATI doc's demo
	float2 tc=TEX0;
	tc-=0.5;
	float vignette=1-dot(tc,tc);
	float vignette_rez=vignette*vignette*vignette*vignette;
	*/
	
return (((MIDDLE_GRAY/(SCREEN_LUMINANCE + 0.001f))*scr_color)*BASE_SCALE_FACTOR+bloom_color*BLOOM_SCALE_FACTOR);



}

