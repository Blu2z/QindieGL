/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Sepia
// Color Space Conversion 
// Sepia Tone via YIQ space
/////////////////////////////////////////////////


uniform	 samplerRECT scr0:TEXUNIT0;


uniform  float3  filter;

float4 main( float2 TEX0 : TEXCOORD0 ):COLOR
{

 float4 out_color;
   
	float4 c = .5;
	float4 currFrameSample;
	float4 currFrameSampleYIQ;
	float4x4 YIQMatrix = { 0.299, 0.587, 0.114,0,
						    0.596, -0.275, -0.321,0,
							0.212, -0.523, 0.311,0,
							0,0,0,0};
							
float4x4 inverseYIQ ={ 1.0000000000000000000, .95568806036115671171, .61985809445637075388, 0,
						1.0000000000000000000, -.27158179694405859326, -.64687381613840131330, 0,
						1.0000000000000000000, -1.1081773266826619523, 1.7050645599191817149, 0,
						0,0,0,0 };

// get sample
currFrameSample = texRECT( scr0, TEX0);

// convert to YIQ space

currFrameSampleYIQ = mul(YIQMatrix , currFrameSample);
currFrameSampleYIQ.y = 0.2; // convert YIQ color to sepia tone
currFrameSampleYIQ.z = 0.0;

// convert back to RGB
out_color = mul( inverseYIQ, currFrameSampleYIQ);

out_color.x *= filter.x;
out_color.y *= filter.y;
out_color.z *= filter.z;

return out_color;
}
