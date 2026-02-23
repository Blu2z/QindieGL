


struct outdata
{
    float4 color : COLOR;
};

uniform float Time;

outdata main( float2 TEX0 : TEXCOORD0,
 	    uniform sampler2D	 diffuse:TEXUNIT0):COLOR
{

	outdata OUT;


	

//	gl_TexCoord[2].x = s_attribute_0.x / 20.0 + sin(t) * 0.2;
//	gl_TexCoord[2].y = s_attribute_0.y / 20.0 - cos(t) * 0.2;

	float2	tex_offset;

	tex_offset.x= + sin(Time/3000.0) * 0.2;
	tex_offset.y= - cos(Time/3000.0) * 0.2;



	
	OUT.color=tex2D(diffuse,TEX0+tex_offset)*2.0;


//	OUT.color=tex2D(diffuse,TEX0+float2(Time/1000.0,-Time/1000.0));

//	OUT.color=float4(1.0,0.0,1.0,1.0);

//	return tex2D(diffuse,TEX0);
//	return float4(1.0,0.0,0.1,1.0); //big o/verbright

	return OUT;
}
