/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Blured image.Rectangle image.
/////////////////////////////////////////////////


uniform	 samplerRECT image:TEXUNIT0;

float4 main(
		        float2 TEX0 : TEXCOORD0,
		        float2 TEX1 : TEXCOORD1,
		        float2 TEX2 : TEXCOORD2,
		        float2 TEX3 : TEXCOORD3,
		        float2 TEX4 : TEXCOORD4
		        
		        
		        
		        
	 	     ):COLOR
{

	float4 out_color;
	
	//unweighted blur
	//out_color=(texRECT( image, TEX0)+texRECT( image, TEX1)+texRECT( image, TEX2)+texRECT( image, TEX3)+texRECT( image, TEX4))/5.0;	

//weighted gaussian blur	
	out_color=(texRECT( image, TEX0)+texRECT( image, TEX1)+texRECT( image, TEX2)+texRECT( image, TEX3)+texRECT( image, TEX4)*4.0)/8.0;	

//edge detection blur
//	out_color=(texRECT( image, TEX0)*(-1.0)+texRECT( image, TEX1)*(-1.0)+texRECT( image, TEX2)*(-1.0)+texRECT( image, TEX3)*(-1.0)+texRECT( image, TEX4)*5.0)/5.0;	
	
	return out_color;
}

