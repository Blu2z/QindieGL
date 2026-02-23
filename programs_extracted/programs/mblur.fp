/////////////////////////////////////////////////
// DS2 Engine shader (C) 2004 Dmitry Sytnik
// Screen filter
/////////////////////////////////////////////////


uniform	 samplerRECT scr0:TEXUNIT0;
uniform	 samplerRECT scr1:TEXUNIT1;
uniform	 samplerRECT scr2:TEXUNIT2;
uniform	 samplerRECT scr3:TEXUNIT3;

float4 main(
		        float2 TEX0 : TEXCOORD0
	 	     ):COLOR
{

 float4 out_color; 
 out_color=0;

//coolest blur for NV30

 out_color += texRECT( scr0, TEX0)*0.5;
 out_color += texRECT( scr1, TEX0)*0.3;
 out_color += texRECT( scr2, TEX0)*0.1;
 out_color += texRECT( scr3, TEX0)*0.1;


return out_color;

}







