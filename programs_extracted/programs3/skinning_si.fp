#include "oe_common.cg"


#define DISPLACE_AMOUNT  0.06


struct SInData
{
	float2 tex0		: TEXCOORD0; //diffuse texture
	float3 pos		: TEXCOORD1;
    float3 norm		: TEXCOORD2;
    float3 tang		: TEXCOORD3;
    float3 binorm	: TEXCOORD4;

};

////////////////////////////////////////////

uniform sampler2D	 tex_diffuse : TEXUNIT1;
uniform sampler2D	 tex_normalmap : TEXUNIT3;
uniform samplerCUBE	 tex_cube : TEXUNIT4;

uniform float3		 cam_pos;		//camera position
uniform float2		 param_float2;		//camera position
uniform float		 param_float;		//camera position



#include"oe_illumination.cg"

////////////////////////////////////////////

float4 main(  SInData in_data ) :COLOR			
{
	float3 res_color;

	float3	c_diffuse = tex2D( tex_diffuse , in_data.tex0).xyz; 

	SIlluminationResult ir = illuminate(in_data.pos, expand(in_data.norm));

	res_color = c_diffuse * ir.diffuse + ir.specular;

	return float4(res_color, 1.0f);

	/*
	float3  light_dir = float3(0.333, 0.333, 0.333);	//use fake light direction
	float   shininess = 23.0;

	float4 res_color;

	in_data.norm = expand(in_data.norm);
	in_data.norm = normalize( in_data.norm );

	in_data.tang = expand(in_data.tang);
	in_data.tang = normalize( in_data.tang );

	in_data.binorm = expand(in_data.binorm);
	in_data.binorm = normalize(in_data.binorm);


	
 	float3 V = normalize(cam_pos - in_data.pos); //view vector

	// view vector from eye to position
	//float3 view_vec = camera_position - IN.pos.xyz;
	//float2	offset		= view_vec.xy * (dheight * DISPLACE_AMOUNT - DISPLACE_AMOUNT * 0.5 );
	//tc					= IN.tex0 + offset;
	//

	
	float dheight = 0.3;
	float2 offset  = V.xy * ( dheight * DISPLACE_AMOUNT - DISPLACE_AMOUNT * 0.5 );
	float2 tc = in_data.tex0 + offset;


	float3 normal = tex2D( tex_normalmap , tc ).xyz;
	normal = expand( normal );

	 float3x3 rotation = float3x3(in_data.tang,
        	                      in_data.binorm,
                	              in_data.norm);

	
	normal = mul( normal, rotation );


	//transofrm by tangent space

	float  light_i = max( dot( normal, light_dir) , 0 );	//light intensivity


	// Compute the specular term

	float3 H = normalize(light_dir + V); //half - angle
	float specular_i = pow(max(dot(H, normal), 0), shininess);

	if (light_i <= 0) 
		specular_i = 0;


	res_color = tex2D( tex_diffuse , in_data.tex0  );// * light_i + specular_i;

	return res_color;
	*/
}
