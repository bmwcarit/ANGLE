#if 0
//
// Generated by Microsoft (R) HLSL Shader Compiler 10.1
//
//
// Resource Bindings:
//
// Name                                 Type  Format         Dim      HLSL Bind  Count
// ------------------------------ ---------- ------- ----------- -------------- ------
// Sampler                           sampler      NA          NA             s0      1 
// TextureF                          texture  float4          2d             t0      1 
//
//
//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_POSITION              0   xyzw        0      POS   float       
// TEXCOORD                 0   xy          1     NONE   float   xy  
//
//
// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_TARGET                0   xyzw        0   TARGET   float   xyzw
//
ps_4_0
dcl_sampler s0, mode_default
dcl_resource_texture2d (float,float,float,float) t0
dcl_input_ps linear v1.xy
dcl_output o0.xyzw
dcl_temps 1
sample r0.xyzw, v1.xyxx, t0.xyzw, s0
mul r0.xyz, r0.wwww, r0.xyzx
mul r0.xyz, r0.xyzx, l(31.000000, 63.000000, 31.000000, 0.000000)
round_ne r0.xyz, r0.xyzx
mul o0.xyz, r0.xyzx, l(0.032258, 0.015873, 0.032258, 0.000000)
mov o0.w, l(1.000000)
ret 
// Approximately 7 instruction slots used
#endif

const BYTE g_PS_FtoF_PM_RGB_565_2D[] = {
    68,  88,  66,  67,  107, 29,  4,   204, 154, 16,  239, 118, 140, 65,  255, 118, 32, 171, 247,
    239, 1,   0,   0,   0,   228, 2,   0,   0,   5,   0,   0,   0,   52,  0,   0,   0,  212, 0,
    0,   0,   44,  1,   0,   0,   96,  1,   0,   0,   104, 2,   0,   0,   82,  68,  69, 70,  152,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   28,  0,  0,   0,
    0,   4,   255, 255, 0,   1,   0,   0,   109, 0,   0,   0,   92,  0,   0,   0,   3,  0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  1,   0,
    0,   0,   1,   0,   0,   0,   100, 0,   0,   0,   2,   0,   0,   0,   5,   0,   0,  0,   4,
    0,   0,   0,   255, 255, 255, 255, 0,   0,   0,   0,   1,   0,   0,   0,   13,  0,  0,   0,
    83,  97,  109, 112, 108, 101, 114, 0,   84,  101, 120, 116, 117, 114, 101, 70,  0,  77,  105,
    99,  114, 111, 115, 111, 102, 116, 32,  40,  82,  41,  32,  72,  76,  83,  76,  32, 83,  104,
    97,  100, 101, 114, 32,  67,  111, 109, 112, 105, 108, 101, 114, 32,  49,  48,  46, 49,  0,
    171, 171, 171, 73,  83,  71,  78,  80,  0,   0,   0,   2,   0,   0,   0,   8,   0,  0,   0,
    56,  0,   0,   0,   0,   0,   0,   0,   1,   0,   0,   0,   3,   0,   0,   0,   0,  0,   0,
    0,   15,  0,   0,   0,   68,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  3,   0,
    0,   0,   1,   0,   0,   0,   3,   3,   0,   0,   83,  86,  95,  80,  79,  83,  73, 84,  73,
    79,  78,  0,   84,  69,  88,  67,  79,  79,  82,  68,  0,   171, 171, 171, 79,  83, 71,  78,
    44,  0,   0,   0,   1,   0,   0,   0,   8,   0,   0,   0,   32,  0,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   3,   0,   0,   0,   0,   0,   0,   0,   15,  0,   0,   0,  83,  86,
    95,  84,  65,  82,  71,  69,  84,  0,   171, 171, 83,  72,  68,  82,  0,   1,   0,  0,   64,
    0,   0,   0,   64,  0,   0,   0,   90,  0,   0,   3,   0,   96,  16,  0,   0,   0,  0,   0,
    88,  24,  0,   4,   0,   112, 16,  0,   0,   0,   0,   0,   85,  85,  0,   0,   98, 16,  0,
    3,   50,  16,  16,  0,   1,   0,   0,   0,   101, 0,   0,   3,   242, 32,  16,  0,  0,   0,
    0,   0,   104, 0,   0,   2,   1,   0,   0,   0,   69,  0,   0,   9,   242, 0,   16, 0,   0,
    0,   0,   0,   70,  16,  16,  0,   1,   0,   0,   0,   70,  126, 16,  0,   0,   0,  0,   0,
    0,   96,  16,  0,   0,   0,   0,   0,   56,  0,   0,   7,   114, 0,   16,  0,   0,  0,   0,
    0,   246, 15,  16,  0,   0,   0,   0,   0,   70,  2,   16,  0,   0,   0,   0,   0,  56,  0,
    0,   10,  114, 0,   16,  0,   0,   0,   0,   0,   70,  2,   16,  0,   0,   0,   0,  0,   2,
    64,  0,   0,   0,   0,   248, 65,  0,   0,   124, 66,  0,   0,   248, 65,  0,   0,  0,   0,
    64,  0,   0,   5,   114, 0,   16,  0,   0,   0,   0,   0,   70,  2,   16,  0,   0,  0,   0,
    0,   56,  0,   0,   10,  114, 32,  16,  0,   0,   0,   0,   0,   70,  2,   16,  0,  0,   0,
    0,   0,   2,   64,  0,   0,   8,   33,  4,   61,  33,  8,   130, 60,  8,   33,  4,  61,  0,
    0,   0,   0,   54,  0,   0,   5,   130, 32,  16,  0,   0,   0,   0,   0,   1,   64, 0,   0,
    0,   0,   128, 63,  62,  0,   0,   1,   83,  84,  65,  84,  116, 0,   0,   0,   7,  0,   0,
    0,   1,   0,   0,   0,   0,   0,   0,   0,   2,   0,   0,   0,   4,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,   0,  0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,  0,   0,
    0,   0,   0,   0,   0,   1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0};
