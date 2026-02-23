#include "oe_common.cg"


struct SInData
{
	float2 tex0		: TEXCOORD0; //diffuse texture
};

//HACK! as shadow color !!!!
uniform float3	up_direction;

////////////////////////////////////////////

float4 main(  SInData in_data ) :COLOR			
{
	return float4(0.3, 0.3, 0.3, 1.0f);
}	
