/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// edge detection
/////////////////////////////////////////////////


uniform	 samplerRECT scr0:TEXUNIT0;

uniform float intensivity;
uniform float kernel;

float4 main(
		        float2 TEX0 : TEXCOORD0
	 	     ):COLOR
{

float4 out_color;
int i =0;
//float4 c = .5;
float2 texCoords;
float4 texSamples[8];
float4 vertGradient;
float4 horzGradient;

					   
float2 sampleOffsets[8]={ {-1.0,-1.0},{0.0,-1.0},{1.0,-1.0},
					      {-1.0,0.0} ,           {1.0,0.0},
					      {-1.0,1.0}, {0.0,1.0},  {1.0,1.0} };
					   
				

for(i =0; i < 8; i++)
{
	texCoords = TEX0 + sampleOffsets[i] * kernel; // add sample offsets stored in c10-c17 (inclusive)
	// take sample
	texSamples[i] = texRECT( scr0, texCoords);
	// convert to b&w
	texSamples[i] = dot(texSamples[i], .333333f);

}

// VERTICAL Gradient
vertGradient = -(texSamples[0] + texSamples[5] + 2*texSamples[3]);
vertGradient += (texSamples[2] + texSamples[7] + 2*texSamples[4]);
// Horizontal Gradient
horzGradient = -(texSamples[0] + texSamples[2] + 2*texSamples[1]);
horzGradient += (texSamples[5] + texSamples[7] + 2*texSamples[6]);

// we could approximate by adding the abs value..but we have the horse power
out_color = sqrt( horzGradient*horzGradient + vertGradient*vertGradient );

return out_color*intensivity;

}
