/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Blur
/////////////////////////////////////////////////
uniform	 samplerRECT scr0:TEXUNIT0;
uniform  float3  filter;
uniform  float  blur_kernel;
float4 main( float2 TEX0 : TEXCOORD ):COLOR
{
 	float4 	out_color;
 	out_color = 0;
	float x,y;
	for ( y=-1; y <= 1; y++ )
	{
	  for ( x=-1; x <= 1; x++ )
	  {
	    float2 filterPosition = TEX0;
	    filterPosition += float2( x * blur_kernel, y * blur_kernel);
	    out_color += texRECT( scr0, filterPosition );
	  }
	}
	out_color *= 1.0/9.0;

	out_color.x *= filter.x;
	out_color.y *= filter.y;
	out_color.z *= filter.z;

return out_color;
}
