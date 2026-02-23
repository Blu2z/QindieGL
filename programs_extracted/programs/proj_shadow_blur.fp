/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// blured projective shadow
/////////////////////////////////////////////////



struct outdata
{
    float4 color : COLOR;
};


float4 offset_lookup(sampler2D map,
					 float4	   loc,
					 float2	 offset)
{
	return tex2Dproj(map,float4(loc.xy+offset*(1.0/128.0)*loc.w,loc.z,loc.w));
}

outdata main(float4 TEX0 : TEXCOORD0, //shadow image
             float2 TEX1 : TEXCOORD1, //attenuation texture
             float4 COL  : COLOR0, //shadow intensivity factor


		 uniform sampler2D	 shadow_image:TEXUNIT0,        
		 uniform sampler1D	 attenuation:TEXUNIT1
	     )
{
   outdata OUT;
   

/*   
   float3 tex1,tex2,tex3,tex4;
   
   tex1=TEX0.xyw;
   tex2=tex1;
   tex3=tex1;
   
   tex1.x-=0.007;
   tex2.x+=0.007;
   tex3.y-=0.007;
   
   float rez=tex2Dproj(shadow_image,tex1).r+tex2Dproj(shadow_image,tex2).r+tex2Dproj(shadow_image,tex3).r;
   rez*=0.333;
   OUT.color.rgba=rez;//(tex2Dproj(shadow_image,tex1)+tex2Dproj(shadow_image,tex2)+tex2Dproj(shadow_image,tex3));
   OUT.color.a=1.0;
   //OUT.color*=0.333;
     
  */ 
  
   //OUT.color=tex2Dproj(shadow_image,TEX0)+tex2Dproj(shadow_image,TEX1)+tex2Dproj(shadow_image,TEX2)+tex2Dproj(shadow_image,TEX3);
   
//OUT.color=tex2Dproj(shadow_image,TEX0);
//   OUT.color*=0.25;



 

  
   // WORKS, BUT TOO SLOW!

   /*
   float4 sum=0;

   float x,y;

   for(y=-1.5;y<=1.5;y+=1.5)
//	   for(x=-1.5;x<=1.5;x+=1.5)
		   sum+=offset_lookup(shadow_image,TEX0,float2(x,y));

   OUT.color=(sum/3.0);
   OUT.color.rgb+=tex1D(attenuation,TEX1.x).rgb+COL.rgb;
   
   //float4 sum = 0;

   */

   // FASTER! BETTER!
   /*
   float4 sum=0;


    sum+=offset_lookup(shadow_image,TEX0,float2(-1.0 , 0.3));
    sum+=offset_lookup(shadow_image,TEX0,float2(0.5,   0.1));
    sum+=offset_lookup(shadow_image,TEX0,float2(1.0,   -1.0));
    sum+=offset_lookup(shadow_image,TEX0,float2(-0.2,  -0.6));
    sum+=offset_lookup(shadow_image,TEX0,float2(0.0,  0.7));

   OUT.color=(sum/5.0);
   OUT.color.rgb+=tex1D(attenuation,TEX1.x).rgb+COL.rgb;
   */


   //GOOD enough
   float4 sum = 0;


    //use random samping pattern to acheve good results with fewer samples
    sum += offset_lookup(shadow_image,TEX0,float2(-1.0 , 0.3));
    sum += offset_lookup(shadow_image,TEX0,float2(0.5,   0.1));
    sum += offset_lookup(shadow_image,TEX0,float2(-0.2,  -0.6));
    sum += offset_lookup(shadow_image,TEX0,float2(0.0,  0.7));

   OUT.color = (sum/4.0);
   OUT.color.rgb += tex1D(attenuation,TEX1.x).rgb+COL.rgb;

   /*
   float4 sample0, sample1, sample_res;

//   for(y=-1.5;y<=1.5;y+=1.5)
//	   for(x=-1.5;x<=1.5;x+=1.5)
//		   sum+=offset_lookup(shadow_image,TEX0,float2(x,y));

   sample0 = offset_lookup(shadow_image,TEX0,float2( - 1.0 , - 1.0));
   sample1 = offset_lookup(shadow_image,TEX0,float2( + 1.0 , + 1.0));
   sample_res = lerp(sample0,sample1,0.5);

   OUT.color = sample_res;//(sum/9.0);
   OUT.color.rgb+=tex1D(attenuation,TEX1.x).rgb+COL.rgb;
   */
   

   
  

   //OUT.color=tex1D(attenuation,TEX1.x);

   //float2 offset=(float)(frac( position.xy * 0.5 ) > 0.25 );
   //offset.y+=offset.x;

  // if( offset.y > 1.1 )
//	   offset.y = 0.0;

   /*

   float2 offset;
   
   offset.x=0.0;
   offset.y=0.0;

   float3 shadowCoeff=(offset_lookup(shadow_image,TEX0,offset+float2(-1.5,0.5))+
					   offset_lookup(shadow_image,TEX0,offset+float2(0.5,0.5))+
					   offset_lookup(shadow_image,TEX0,offset+float2(-1.5,-1.5))+
					   offset_lookup(shadow_image,TEX0,offset+float2(0.5,-1.5)))*0.25;

   OUT.color.xyz=shadowCoeff;

   OUT.color.a=1.0;
   */


  


return OUT;
}


