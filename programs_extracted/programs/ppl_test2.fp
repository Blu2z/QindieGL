//
// Car Paint Shader
// Copyright (c) NVIDIA Corporation. All rights reserved.
//
// NOTE:
// This shader is based on the Time Machine temporal rust shader.
// Car paint data was measured by Cornell University from samples 
// provided by the Ford Motor Company.
//


struct VS_OUTPUT {
  float4 HPosition : POSITION;  // coord position in window
  float2 uv        : TEXCOORD0; // wavy or fleck map texture coordinates
  float3 light     : TEXCOORD1; // position of light relative to point
  float4 halfangle : TEXCOORD2; // Blinn halfangle
  float3 reflection: TEXCOORD3; // Reflection vector (per-vertex)
  float4 view      : TEXCOORD4; // position of viewer relative to point
  float3 tangent   : TEXCOORD5; // view-tangent matrix
  float3 binormal  : TEXCOORD6; // ...
  float3 normal    : TEXCOORD7; // ...
  float  fresn     : COLOR0;
};



// PIXEL SHADER
float4 main( VS_OUTPUT vert,
             uniform sampler2D WavyMap          : TEXUNIT0,
             uniform samplerCUBE EnvironmentMap : TEXUNIT1,
             uniform sampler2D PaintMap         : TEXUNIT2,
             uniform sampler2D FleckMap         : TEXUNIT3
             /*,uniform float Ambient */) : COLOR
{
   float Ambient = 0.1;

  // NEWPAINTSPEC      = { UNUSED, SPEC POWER, FRESNEL GLOSSINESS, FLECK SPEC POWER }
  float4 NewPaintSpec  = { 0.0f, 64.0f, 3.8f, 8.0f };
  float3 ClearCoat     = { 0.299f,0.587f, 0.114f };
  float3 FleckColor    = { 0.9, 1.05, 1.0 };
  float3 WavyScale     = { 0.2, -0.2, 1.0 };


  // Tangent space LIGHT vector
  float3 L = normalize(vert.light);

  // Tangent space HALF-ANGLE vector
  float3 H = normalize(vert.halfangle.xyz);

  // Tangent space VIEW vector
  float3 V = normalize(vert.view.xyz);
  float v_dist = vert.view.w;

  // Tangent space WAVY_NORMAL
  float3 wavyN = (float3)tex2D(WavyMap, vert.uv)*2-1;
  wavyN = normalize(wavyN*WavyScale);


  // PAINT
  // A normal map map could be loaded here instead if we wanted more detail
  // In this case we have a uniform tangent space normal (0,0,1)
  float n_d_l = L.z;
  float n_d_h = H.z;
  float3 paint_color = (float3)tex2D(PaintMap, float2(n_d_l, n_d_h));


  // SPECULAR POWER - use a saturated diffuse term to clamp the backlighting
  n_d_h = saturate(n_d_l*4)*pow(n_d_h, NewPaintSpec.y);


  // REFLECTION ENVIRONMENT
  // Reflect view vector about wavy normal and bring to view space
  float3 R = reflect(-V, wavyN);
  R = R.x*vert.tangent + R.y*vert.binormal + R.z*vert.normal;
  float3 reflect_color = (float3)texCUBE(EnvironmentMap, R);


  // FLECKS
  // Load random 3-vector flecks from fleck_map
  // Reduce tiling artifacts by sampling at different frequencies
  float3 fleckN      = (float3)tex2D(FleckMap, vert.uv*37)*2-1;
  fleckN             = ((float3)tex2D(FleckMap, vert.uv*23)*2-1)/2 + fleckN/2;
  float  fleck_n_d_h = saturate(dot(fleckN, H));
  float3 fleck_color = FleckColor*pow(fleck_n_d_h,lerp(NewPaintSpec.y, NewPaintSpec.w, v_dist));
  // Control the ambient fleckiness and also attenuate with distance
  fleck_color = fleck_color*Ambient*vert.halfangle.w;


  // DIFFUSE
  float k_d = saturate(n_d_l*1.2);
  float3 paintResult = lerp(Ambient*paint_color, paint_color, k_d);


  // FRESNEL
  float Fresnel = saturate(dot(ClearCoat, reflect_color));
  Fresnel = pow(Fresnel, NewPaintSpec.z);
  Fresnel = saturate(vert.fresn*Fresnel);       // This helps make the clear coat less omnipresent -- only the really (perceptually) bright areas reflect the most.
  // Show more of the specular reflection environment when in fresnel zones
  // diffuse * (1-fresnel) + environment * (fresnel)
  paintResult = lerp(paintResult, reflect_color, Fresnel);


  // SPECULAR
  // diffuse + specular + flecks
  paintResult = paintResult + n_d_h + fleck_color;


  // OUTPUT
  return paintResult.xyzz;
}

