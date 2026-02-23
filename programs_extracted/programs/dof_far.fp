//////////////////////////////////////////////////////
// True Depth Of Filed.
//////////////////////////////////////////////////////




uniform	 sampler2D scr_depth:TEXUNIT0;
uniform  samplerRECT scr_image_sharp:TEXUNIT1;
uniform  sampler2D scr_image_blured:TEXUNIT2;

uniform  float DEPTH_SCALE;
uniform  float DEPTH_BIAS;

uniform  float DOF_FOCAL_PLANE; 
uniform  float DOF_FAR_PLANE_OFFSET;		//distance from focal plane far away where will be full bluriness
uniform  float DOF_NEAR_PLANE_OFFSET;		//distance from focal plane far away where will be full bluriness

uniform  float BLURING_RADIUS;

float4 main(  float2 TEX0 : TEXCOORD0 ,
			  float2 TEX1 : TEXCOORD1
			  ):COLOR
{

	//float4 scr_image_sharp_color=tex2D(scr_image_sharp,TEX0);	

	float4 scr_image_sharp_color=texRECT(scr_image_sharp,TEX1);	
	float4 scr_image_depth_color=tex2D(scr_depth,TEX0);
	float4 scr_image_blured_color=tex2D(scr_image_blured,TEX0);	


	//LOW PERSICTION!!!!
	//calculating object's distance from Z buffer values
	//FIXED far/near!. TODO: make uniform params!

	float zfar=22000.0;
	float znear=10.0;

	float z=scr_image_depth_color.r;



	float objectdistance=-zfar*znear/(z*(zfar-znear)-zfar);


	float dof_focal_plane=DOF_FOCAL_PLANE;//300.0;
	float dof_far_plane=DOF_FOCAL_PLANE+DOF_FAR_PLANE_OFFSET;
	float dof_near_plane=DOF_FOCAL_PLANE-DOF_NEAR_PLANE_OFFSET;

	////////////////////////////////////////
	//new realistic DOF!
	/*


	float4 rezult_color;

	float lerp_factor;

	if(objectdistance<=dof_focal_plane)
	{
		rezult_color=scr_image_sharp_color; //not DOF-ed

	}
	else
	{
		lerp_factor=(objectdistance-dof_focal_plane)/(dof_far_plane-dof_focal_plane);
		//clamp to 0-1
		lerp_factor=min(lerp_factor,1.0);
		lerp_factor=max(lerp_factor,0.0);
		//rezult_color=lerp(scr_image_sharp_color,scr_image_blured_color,lerp_factor);
		//rezult_color=smoothstep(0.0,1.0,lerp_factor);

		//rezult_color=lerp_factor;
		//rezult_color=lerp(scr_image_sharp_color,scr_image_blured_color,smoothstep(0.0,1.0,lerp_factor));


		float MAX_COC=4.0;

		float CURRENT_COC=MAX_COC*lerp_factor;

		float4 blurred_sample_color=0;

		
		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(-1.0*1.0/512.0,-1.0*1.0/512.0)*CURRENT_COC);	
		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(1.0*1.0/512.0,-1.0*1.0/512.0)*CURRENT_COC);	
		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(1.0*1.0/512.0,1.0*1.0/512.0)*CURRENT_COC);	
		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(-1.0*1.0/512.0,1.0*1.0/512.0)*CURRENT_COC);	

		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(-0.6*1.0/512.0,0.3*1.0/512.0)*CURRENT_COC);	
		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(0.4*1.0/512.0,0.7*1.0/512.0)*CURRENT_COC);	
		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(-0.3*1.0/512.0,0.2*1.0/512.0)*CURRENT_COC);	
		blurred_sample_color+=tex2D(scr_image_blured,TEX0+float2(0.6*1.0/512.0,-0.6*1.0/512.0)*CURRENT_COC);	

		blurred_sample_color*=1.0/8.0;

		//rezult_color=blurred_sample_color;
		rezult_color=lerp(scr_image_sharp_color,blurred_sample_color,lerp_factor);
	}

	return rezult_color;
	*/
	////////////////////////////////////////

	/*
	float4 rezult_color=scr_image_blured_color;

	float blur_offset=dof_far_plane-z;//BLURING_RADIUS;//DOF_FAR_PLANE_OFFSET/200.0;
	blur_offset=blur_offset/400.0;

	blur_offset=min(blur_offset,9.0);
	blur_offset=max(blur_offset,0.0);

	//blur_offset=1.0-blur_offset;

	rezult_color+=tex2D(scr_image_blured,TEX0+float2(1.0/512.0,0.0)*blur_offset+(1.0/512.0)*0.4);	
	rezult_color+=tex2D(scr_image_blured,TEX0+float2(-1.0/512.0,0.0)*blur_offset+(1.0/512.0)*0.4);	

	rezult_color+=tex2D(scr_image_blured,TEX0+float2(0.0,1.0/512.0)*blur_offset+(1.0/512.0)*0.4);	
	rezult_color+=tex2D(scr_image_blured,TEX0+float2(0.0,-1.0/512.0)*blur_offset+(1.0/512.0)*0.4);	

	rezult_color*=0.2;

	scr_image_blured_color=rezult_color;


	
	///
	scr_image_blured_color+=tex2D(scr_image_blured,TEX0+float2(1.0/512.0,0.0)*BLURING_RADIUS+(1.0/512.0)*0.4);	
	scr_image_blured_color+=tex2D(scr_image_blured,TEX0+float2(-1.0/512.0,0.0)*BLURING_RADIUS+(1.0/512.0)*0.4);	

	scr_image_blured_color+=tex2D(scr_image_blured,TEX0+float2(0.0,1.0/512.0)*BLURING_RADIUS+(1.0/512.0)*0.4);	
	scr_image_blured_color+=tex2D(scr_image_blured,TEX0+float2(0.0,-1.0/512.0)*BLURING_RADIUS+(1.0/512.0)*0.4);	

	scr_image_blured_color*=0.25;
	*/


	float4 rezult_color;

	rezult_color=0;

	if(objectdistance<=dof_focal_plane)
	{
		float lerp_factor=(dof_focal_plane-objectdistance)/(dof_focal_plane-dof_near_plane);
		lerp_factor=min(lerp_factor,1.0);
		rezult_color=lerp(scr_image_sharp_color,scr_image_blured_color,lerp_factor);

	}
	else
	{
		float lerp_factor=(objectdistance-dof_focal_plane)/(dof_far_plane-dof_focal_plane);
		lerp_factor=min(lerp_factor,1.0);
		rezult_color=lerp(scr_image_sharp_color,scr_image_blured_color,lerp_factor);
		//rezult_color=smoothstep(0.0,1.0,lerp_factor);

		//rezult_color=lerp_factor;
		//rezult_color=lerp(scr_image_sharp_color,scr_image_blured_color,smoothstep(0.0,1.0,lerp_factor));

	}

	return rezult_color;

	//*/


	/*
	
	//OLD style DOF based not on true depth value. It based on virtual "depth_Scale" and "depth_bias"

	scr_image_depth_color=scr_image_depth_color*DEPTH_SCALE+DEPTH_BIAS;
	
	scr_image_depth_color.r=max(scr_image_depth_color.r,0.0);
	scr_image_depth_color.r=min(scr_image_depth_color.r,1.0);
	
	return lerp(scr_image_sharp_color,scr_image_blured_color,abs(scr_image_depth_color.r));
	*/
	

}

