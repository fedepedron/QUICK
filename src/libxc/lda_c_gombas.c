/* 
 Copyright (C) 2006-2007 M.A.L. Marques 
 
 This Source Code Form is subject to the terms of the Mozilla Public 
 License, v. 2.0. If a copy of the MPL was not distributed with this 
 file, You can obtain one at http://mozilla.org/MPL/2.0/. 
*/ 
 
#include "util.h" 
 
/************************************************************************ 
 Wigner's parametrization from the low density limit 
************************************************************************/ 
 
#define XC_LDA_C_GOMBAS  24   /* Gombas parametrization       */ 
 
#ifndef DEVICE 
#include "maple2c/lda_c_gombas.c" 
#endif 
 
#define func maple2c_func 
#include "work_lda.c" 
 
const xc_func_info_type xc_func_info_lda_c_gombas = { 
  XC_LDA_C_GOMBAS, 
  XC_CORRELATION, 
  "Gombas", 
  XC_FAMILY_LDA, 
  {&xc_ref_Gombas1967, NULL, NULL, NULL, NULL}, 
  XC_FLAGS_3D | XC_FLAGS_HAVE_EXC | XC_FLAGS_HAVE_VXC | XC_FLAGS_HAVE_FXC | XC_FLAGS_HAVE_KXC, 
  1e-32, 
  0, NULL, NULL, 
  NULL,     /* init */ 
  NULL,     /* end  */ 
  work_lda, /* lda  */ 
  NULL, 
  NULL 
}; 
