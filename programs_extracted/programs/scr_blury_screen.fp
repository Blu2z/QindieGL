/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
// Blury screen
/////////////////////////////////////////////////


uniform	 samplerRECT scr:TEXUNIT0;
uniform	 samplerRECT blury_scr:TEXUNIT1;

float4 main(
		        float2 TEX0 : TEXCOORD0,
		        float2 TEX1 : TEXCOORD1

	 	     ):COLOR
{

 float4 out_color; 

	float4 blury_params;
	
	blury_params.x=0.99;
	blury_params.y=0.99;
	blury_params.z=0.99;
	blury_params.w=0.26;
	
	
	float4 scr_color		= texRECT(scr, TEX0);
	float4 blury_scr_color	= texRECT(blury_scr, TEX1);

	float tmp = dot(float3(0.33, 0.59, 0.11), scr_color.xyz);
 
	float3 desaturated= tmp*blury_params.xyz; 



	// finally add them all together	
	out_color.xyz = lerp(blury_scr_color.xyz, desaturated*desaturated*4, blury_params.w);                

	out_color.w = 1.0f;
 
 
	return out_color;




/*
//some kind of edge detection..
//Prikolnie effecti poluchayutsja!
    float4 screen = texRECT(scr, TEX0);
    float4 blury_screen = texRECT(blury_scr, TEX1);

    out_color.xyz = (screen.xyz-blury_screen.xyz)*10;//lerp(screen.xyz, blury_screen.xyz, 0.5);
    
    float rez=out_color.x*0.33+out_color.y*0.59+out_color.z*0.11;
    
    out_color.xyz=screen.xyz+rez*0.3; //tut menjat!
    out_color.a = 1.0;

	return out_color;

//*/



}




