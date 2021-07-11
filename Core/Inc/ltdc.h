/**
  ******************************************************************************
  * @file    ltdc.h
  * @brief   This file contains all the function prototypes for
  *          the ltdc.c file
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __LTDC_H__
#define __LTDC_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

extern LTDC_HandleTypeDef hltdc;

/* USER CODE BEGIN Private defines */
#define REGVFMT_INVALID 0xFF

enum _eRegVfmtPC {
    E_REGVFMT_640x480p_72Hz     = 0,
    E_REGVFMT_640x480p_75Hz     ,
    E_REGVFMT_640x480p_85Hz     ,
    E_REGVFMT_800x600p_60Hz     ,
    E_REGVFMT_800x600p_72Hz     ,
    E_REGVFMT_800x600p_75Hz     ,
    E_REGVFMT_800x600p_85Hz     ,
    E_REGVFMT_1024x768p_60Hz    ,
    E_REGVFMT_1024x768p_70Hz    ,
    E_REGVFMT_1024x768p_75Hz    ,
    E_REGVFMT_1280x768p_60Hz    ,
    E_REGVFMT_1280x1024p_60Hz   ,
    E_REGVFMT_1360x768p_60Hz    ,
    E_REGVFMT_1400x1050p_60Hz   ,
    E_REGVFMT_1600x1200p_60Hz   ,
    E_REGVFMT_1280x1024p_85Hz
};

typedef enum
{
    HDMITX_VFMT_PC_640x480p_60Hz   = 0,   /**< PC format 128                */
    HDMITX_VFMT_PC_800x600p_60Hz   ,   /**< PC format 129                */
    HDMITX_VFMT_PC_1152x960p_60Hz  ,   /**< PC format 130                */
    HDMITX_VFMT_PC_1024x768p_60Hz  ,   /**< PC format 131                */
    HDMITX_VFMT_PC_1280x768p_60Hz  ,   /**< PC format 132                */
    HDMITX_VFMT_PC_1280x1024p_60Hz ,   /**< PC format 133                */
    HDMITX_VFMT_PC_1360x768p_60Hz  ,   /**< PC format 134                */
    HDMITX_VFMT_PC_1400x1050p_60Hz ,   /**< PC format 135                */
    HDMITX_VFMT_PC_1600x1200p_60Hz ,   /**< PC format 136                */
    HDMITX_VFMT_PC_1024x768p_70Hz  ,   /**< PC format 137                */
    HDMITX_VFMT_PC_640x480p_72Hz   ,   /**< PC format 138                */
    HDMITX_VFMT_PC_800x600p_72Hz   ,   /**< PC format 139                */
    HDMITX_VFMT_PC_640x480p_75Hz   ,   /**< PC format 140                */
    HDMITX_VFMT_PC_1024x768p_75Hz  ,   /**< PC format 141                */
    HDMITX_VFMT_PC_800x600p_75Hz   ,   /**< PC format 142                */
    HDMITX_VFMT_PC_1024x864p_75Hz  ,   /**< PC format 143                */
    HDMITX_VFMT_PC_1280x1024p_75Hz ,   /**< PC format 144                */
    HDMITX_VFMT_PC_640x350p_85Hz   ,   /**< PC format 145                */
    HDMITX_VFMT_PC_640x400p_85Hz   ,   /**< PC format 146                */
    HDMITX_VFMT_PC_720x400p_85Hz   ,   /**< PC format 147                */
    HDMITX_VFMT_PC_640x480p_85Hz   ,   /**< PC format 148                */
    HDMITX_VFMT_PC_800x600p_85Hz   ,   /**< PC format 149                */
    HDMITX_VFMT_PC_1024x768p_85Hz  ,   /**< PC format 150                */
    HDMITX_VFMT_PC_1152x864p_85Hz  ,   /**< PC format 151                */
    HDMITX_VFMT_PC_1280x960p_85Hz  ,   /**< PC format 152                */
    HDMITX_VFMT_PC_1280x1024p_85Hz ,   /**< PC format 153                */
    HDMITX_VFMT_PC_1024x768i_87Hz      /**< PC format 154                */

} tmbslHdmiTxVidFmt_t;

struct vic2reg {
   unsigned char vic;
   unsigned char reg;
};

struct sync_desc {
   uint16_t w,h;
   uint16_t Vs2;
   uint8_t pix_rep;
   uint8_t v_toggle;
   uint8_t h_toggle;
   uint16_t hfp;    /* Output values for Vs/Hs input sync */
   uint16_t vfp;
   uint16_t href; /* Output values for all other input sync sources */
   uint16_t vref;
};

typedef struct _tmHdmiTxVidReg_t
{
    uint16_t  nPix;
    uint16_t  nLine;
    uint8_t   VsLineStart;
    uint16_t  VsPixStart;
    uint8_t   VsLineEnd;
    uint16_t  VsPixEnd;
    uint16_t  HsStart;
    uint16_t  HsEnd;
    uint8_t   ActiveVideoStart;
    uint16_t  ActiveVideoEnd;
    uint16_t  DeStart;
    uint16_t  DeEnd;
    uint16_t  ActiveSpaceStart;
    uint16_t  ActiveSpaceEnd;
} tmHdmiTxVidReg_t;

static const struct sync_desc ref_sync_PC[] =
{
   /*          Vs2     PR Vtg Htg  HFP  VFP HREF VREF */
   {640,480,   0,       0,  1,  1,  25,   2, 195, 32}, /* E_REGVFMT_640x480p_72Hz   */
   {640,480,   0,       0,  1,  1,  17,   2, 203, 20}, /* E_REGVFMT_640x480p_75Hz   */
   {640,480,   0,       0,  1,  1,  57,   2, 195, 29}, /* E_REGVFMT_640x480p_85Hz   */

   {800,600,   0,       0,  0,  0,  41,   2, 259, 28}, /* E_REGVFMT_800x600p_60Hz   */

   {800,600,   0,       0,  0,  0,  57,   2, 243, 30}, /* E_REGVFMT_800x600p_72Hz   */
   {800,600,   0,       0,  0,  0,  17,   2, 259, 25}, /* E_REGVFMT_800x600p_75Hz   */
   {800,600,   0,       0,  0,  0,  33,   2, 251, 31}, /* E_REGVFMT_800x600p_85Hz   */
   {1024,768,  0,       0,  1,  1,  25,   2, 323, 36}, /* E_REGVFMT_1024x768p_60Hz  */
   {1024,768,  0,       0,  1,  1,  25,   2, 307, 36}, /* E_REGVFMT_1024x768p_70Hz  */
   {1024,768,  0,       0,  0,  0,  17,   2, 291, 32}, /* E_REGVFMT_1024x768p_75Hz  */
   {1280,768,  0,       0,  0,  1,  65,   2, 387, 28}, /* E_REGVFMT_1280x768p_60Hz  */
   {1280,1024, 0,       0,  0,  0,  49,   2, 411, 42}, /* E_REGVFMT_1280x1024p_60Hz */
   {1360,768,  0,       0,  0,  0,  65,   2, 435, 25}, /* E_REGVFMT_1360x768p_60Hz  */
   {1400,1050, 0,       0,  0,  1,  89,   2, 467, 37}, /* E_REGVFMT_1400x1050p_60Hz */
   {1600,1200, 0,       0,  0,  0,  65,   2, 563, 50}, /* E_REGVFMT_1600x1200p_60Hz */
   {1280,1024, 0,       0,  0,  0,  65,   2, 451, 48}  /* E_REGVFMT_1280x1024p_85Hz */
};


enum _eLTDCVFMT {
    E_LTDCVFMT_640x480p_72Hz = 0,
    E_LTDCVFMT_800x600p_60Hz,
    E_LTDCVFMT_1024x768p_60Hz
};

typedef struct _LTDCSYNC_t {
   uint16_t pll3n, pll3p, pll3q, pll3r;
   uint16_t ahw, avh;
   uint16_t hfp, hsync, hbp;
   uint16_t vfp, vsync, vbp;
   uint16_t hsw,  ahbp, aaw, totalw;
   uint16_t vsh,  avbp, aah, totalh;
} LTDCSYNC_t;

static const LTDCSYNC_t LTDCSYNC[] = {
 { 252, 2, 2, 8, 640, 480, 24, 40, 128, 9, 3, 28, 39, 167, 807, 831, 2, 30, 510, 519 },     // 0 640x480_72Hz
 { 252, 2, 2, 8, 640, 480, 16, 96, 48, 11, 2, 32, 95, 143, 783, 799, 1, 33, 513, 524 },     // 1 640x480_75Hz
 { 288, 2, 2, 8, 640, 480, 32, 48, 112, 1, 3, 25, 47, 159, 799, 831, 2, 27, 507, 508 },     // 2 640x480_85Hz
 { 320, 2, 2, 8, 800, 600, 40, 128, 88, 1, 4, 23, 127, 215, 1015, 1055, 3, 26, 626, 627 },  // 3 800x600_60Hz
 { 400, 2, 2, 8, 800, 600, 56, 120, 64, 37, 6, 23, 119, 183, 983, 1039, 5, 28, 628, 665 },  // 4 800x600_72Hz
 { 396, 2, 2, 8, 800, 600, 16, 80, 160, 1, 2, 21, 79, 239, 1039, 1055, 1, 22, 622, 623 },   // 5 800x600_75Hz
 { 450, 2, 2, 8, 800, 600, 32, 64, 152, 1, 3, 27, 63, 215, 1015, 1047, 2, 29, 629, 630 },   // 6 800x600_85Hz
 { 265, 4, 4, 4, 1024, 768, 24, 136, 160, 3, 6, 29, 135, 295, 1319, 1343, 5, 34, 802, 805 },// 7 1024x768_60Hz
 { 300, 4, 4, 4, 1024, 768, 24, 136, 144, 3, 6, 29, 135, 279, 1303, 1327, 5, 34, 802, 805 },// 8 1024x768_70Hz
 { 315, 4, 4, 4, 1024, 768, 16, 96, 176, 1, 3, 28, 95, 271, 1295, 1311, 2, 30, 798, 799 },  // 9 1024x768_75Hz
};


static const tmHdmiTxVidReg_t format_param_PC[] =
{
   /*  NPIX    NLINE  VsLineStart  VsPixStart  VsLineEnd   VsPixEnd    HsStart     HsEnd   ActiveVideoStart   ActiveVideoEnd DeStart DeEnd */
   /*  npix    nline  vsl_s1       vsp_s1      vsl_e1      vsp_e1      hs_e        hs_e    vw_s1              vw_e1          de_s    de_e */
    {832,   520,      1,          24,          4,           24,         24,     64,     31,         511,    192,    832, 0, 0},/* E_REGVFMT_640x480p_72Hz   */
    {840,   500,      1,          16,          4,           16,         16,     80,     19,         499,    200,    840, 0, 0},/* E_REGVFMT_640x480p_75Hz   */
    {832,   509,      1,          56,          4,           56,         56,     112,    28,         508,    192,    832, 0, 0},/* E_REGVFMT_640x480p_85Hz   */

    {1056,  628,      1,          40,          5,           40,         40,     168,    27,         627,    256,   1056, 0, 0},/* E_REGVFMT_800x600p_60Hz   */
    {1040,  666,      1,          56,          7,           56,         56,     176,    29,         619,    240,   1040, 0, 0},/* E_REGVFMT_800x600p_72Hz   */
    {1056,  625,      1,          16,          4,           16,         16,     96,     24,         624,    256,   1056, 0, 0},/* E_REGVFMT_800x600p_75Hz   */
    {1048,  631,      1,          32,          4,           32,         32,     96,     30,         630,    248,   1048, 0, 0},/* E_REGVFMT_800x600p_85Hz   */

    {1344,  806,      1,          24,          7,           24,         24,     160,    35,         803,    320,   1344, 0, 0},/* E_REGVFMT_1024x768p_60Hz  */
    {1328,  806,      1,          24,          7,           24,         24,     160,    35,         803,    304,   1328, 0, 0},/* E_REGVFMT_1024x768p_70Hz  */
    {1312,  800,      1,          16,          4,           16,         16,     112,    31,         799,    288,   1312, 0, 0},/* E_REGVFMT_1024x768p_75Hz  */

    {1664,  798,      1,          64,          8,           64,         64,     192,    27,         795,    384,   1664, 0, 0},/* E_REGVFMT_1280x768p_60Hz  */
    {1688,  1066,     1,          48,          4,           48,         48,     160,    41,         1065,   408,   1688, 0, 0},/* E_REGVFMT_1280x1024p_60Hz */
    {1792,  795,      1,          64,          7,           64,         64,     176,    24,         792,    432,   1792, 0, 0},/* E_REGVFMT_1360x768p_60Hz  */
    {1864,  1089,     1,          88,          5,           88,         88,     232,    36,         1086,   464,   1864, 0, 0},/* E_REGVFMT_1400x1050p_60Hz */
    {2160,  1250,     1,          64,          4,           64,         64,     256,    49,         1249,   560,   2160, 0, 0},/* E_REGVFMT_1600x1200p_60Hz */
    {1728,  1072,     1,          64,          4,           64,         64,     224,    47,         1071,   448,   1728, 0, 0} /* E_REGVFMT_1280x1024p_85Hz */
};

static const uint8_t pll[] = {
   2,   /* E_REGVFMT_640x480p_72Hz   */
   2,   /* E_REGVFMT_640x480p_75Hz   */
   2,   /* E_REGVFMT_640x480p_85Hz   */
   1,   /* E_REGVFMT_800x600p_60Hz   */
   1,   /* E_REGVFMT_800x600p_72Hz   */
   1,   /* E_REGVFMT_800x600p_75Hz   */
   1,   /* E_REGVFMT_800x600p_85Hz   */
   1,   /* E_REGVFMT_1024x768p_60Hz  */
   1,   /* E_REGVFMT_1024x768p_70Hz  */
   1,   /* E_REGVFMT_1024x768p_75Hz  */
   0,   /* E_REGVFMT_1280x768p_60Hz  */
   0,   /* E_REGVFMT_1280x1024p_60Hz */
   0,   /* E_REGVFMT_1360x768p_60Hz  */
   0,   /* E_REGVFMT_1400x1050p_60Hz */
   0,   /* E_REGVFMT_1600x1200p_60Hz */
   1    /* E_REGVFMT_1280x1024p_85Hz */
};

/* USER CODE END Private defines */

void MX_LTDC_Init(void);

/* USER CODE BEGIN Prototypes */

/* USER CODE END Prototypes */

#ifdef __cplusplus
}
#endif

#endif /* __LTDC_H__ */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
