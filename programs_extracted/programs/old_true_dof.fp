//////////////////////////////////////////////////////
// True Depth Of Filed.
//////////////////////////////////////////////////////




uniform	 sampler2D scr_depth:TEXUNIT0;
uniform  sampler2D scr_image_sharp:TEXUNIT1;
uniform  sampler2D scr_image_blured:TEXUNIT2;


uniform  float DEPTH_SCALE;
uniform  float DEPTH_BIAS;

float4 main(  float2 TEX0 : TEXCOORD0
	 	     ):COLOR
{


	


	float4 scr_image_sharp_color=tex2D(scr_image_sharp,TEX0);	

	float4  scr_image_depth_color=tex2D(scr_depth,TEX0);
	
	float4 scr_image_blured_color=tex2D(scr_image_blured,TEX0);	


//	scr_image_depth_color=scr_image_depth_color*10.0-9.0;

	scr_image_depth_color=scr_image_depth_color*DEPTH_SCALE+DEPTH_BIAS;
	

	scr_image_depth_color.r=max(scr_image_depth_color.r,0.0);
	scr_image_depth_color.r=min(scr_image_depth_color.r,1.0);
	

	if(scr_image_depth_color.r<0.6)
	{
		return float4(scr_image_depth_color.r,scr_image_depth_color.r,scr_image_depth_color.r,scr_image_depth_color.r);
	}
	else
	{
		return float4(scr_image_depth_color.r,0.0,0.0,scr_image_depth_color.r);
	}
	
	return lerp(scr_image_sharp_color,scr_image_blured_color,scr_image_depth_color.r);
	
	//return scr_image_sharp_color*(1.0-scr_image_depth_color);//*0.5+scr_image_blured_color*0.5;

	/*

	float4 out_color;

	float4 scr_image_col=tex2D(scr_image,TEX0);
	float4 scr_image_blured1_col=tex2D(scr_image_blured1,TEX0);
	float4 scr_image_blured2_col=tex2D(scr_image_blured2,TEX0);
	float4 scr_depth_col=tex2D(scr_depth,TEX0);
	
	
	
	float cur_lerp_pos=min(scr_depth_col.a-0.2,1.0);
	cur_lerp_pos=max(cur_lerp_pos,0.0);
	
    out_color=lerp(scr_image_col,scr_image_blured2_col,cur_lerp_pos);
	

	return out_color;

*/

/*
	float FOCUS_DEPTH=0.98;
	float CUR_DEPTH=scr_depth_col.x;

	float factor=step(FOCUS_DEPTH,scr_depth_col.x);


	float dist1=FOCUS_DEPTH-CUR_DEPTH;
	float4 col_lerp1=lerp(scr_image_col,scr_image_blured1_col,dist1+0.5);
	
		
	float dist2=(1.0-FOCUS_DEPTH)/(1.0-CUR_DEPTH-FOCUS_DEPTH);
	float4 col_lerp2=lerp(scr_image_col,scr_image_blured2_col,dist2);
	
	
	//return scr_image_blured2_col*factor+scr_image_blured1_col*(1.0-factor);
	
	return scr_image_col*factor+col_lerp1*(1.0-factor);
*/	
}

