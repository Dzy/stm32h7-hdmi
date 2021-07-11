#include <stdint.h>
#include "i2c.h"
#include "ltdc.h"

extern LTDC_HandleTypeDef hltdc;

#define REG(page, addr) (((page) << 8) | (addr))
#define REG2ADDR(reg)   ((reg) & 0xff)
#define REG2PAGE(reg)   (((reg) >> 8) & 0xff)

#define BIT(x) 1<<x

#define REG_CURPAGE               0xff                /* write */

#define I2C_ADDRESS_TDA19988_CEC     (0x34)<<1
#define I2C_ADDRESS_TDA19988_HDMI    (0x70)<<1
#define I2C_ADDRESS_TDA (0x70<<1)

/* Page 00h: General Control */
#define REG_VERSION_LSB           REG(0x00, 0x00)     /* read */
#define REG_MAIN_CNTRL0           REG(0x00, 0x01)     /* read/write */
# define MAIN_CNTRL0_SR           (1 << 0)
# define MAIN_CNTRL0_DECS         (1 << 1)
# define MAIN_CNTRL0_DEHS         (1 << 2)
# define MAIN_CNTRL0_CECS         (1 << 3)
# define MAIN_CNTRL0_CEHS         (1 << 4)
# define MAIN_CNTRL0_SCALER       (1 << 7)

#define REG_VERSION_MSB           REG(0x00, 0x02)     /* read */
#define REG_SOFTRESET             REG(0x00, 0x0a)     /* write */
# define SOFTRESET_AUDIO          (1 << 0)
# define SOFTRESET_I2C_MASTER     (1 << 1)

#define REG_DDC_DISABLE           REG(0x00, 0x0b)     /* read/write */
#define REG_CCLK_ON               REG(0x00, 0x0c)     /* read/write */
#define REG_I2C_MASTER            REG(0x00, 0x0d)     /* read/write */
# define I2C_MASTER_DIS_MM        (1 << 0)
# define I2C_MASTER_DIS_FILT      (1 << 1)
# define I2C_MASTER_APP_STRT_LAT  (1 << 2)

#define REG_FEAT_POWERDOWN        REG(0x00, 0x0e)     /* read/write */
# define FEAT_POWERDOWN_PREFILT   BIT(0)
# define FEAT_POWERDOWN_CSC       BIT(1)
# define FEAT_POWERDOWN_SPDIF     (1 << 3)

#define REG_INT_FLAGS_0           REG(0x00, 0x0f)     /* read/write */
#define REG_INT_FLAGS_1           REG(0x00, 0x10)     /* read/write */
#define REG_INT_FLAGS_2           REG(0x00, 0x11)     /* read/write */
# define INT_FLAGS_2_EDID_BLK_RD  (1 << 1)

#define REG_ENA_ACLK              REG(0x00, 0x16)     /* read/write */
#define REG_ENA_VP_0              REG(0x00, 0x18)     /* read/write */
#define REG_ENA_VP_1              REG(0x00, 0x19)     /* read/write */
#define REG_ENA_VP_2              REG(0x00, 0x1a)     /* read/write */
#define REG_ENA_AP                REG(0x00, 0x1e)     /* read/write */
#define REG_VIP_CNTRL_0           REG(0x00, 0x20)     /* write */
# define VIP_CNTRL_0_MIRR_A       (1 << 7)
# define VIP_CNTRL_0_SWAP_A(x)    (((x) & 7) << 4)
# define VIP_CNTRL_0_MIRR_B       (1 << 3)
# define VIP_CNTRL_0_SWAP_B(x)    (((x) & 7) << 0)

#define REG_VIP_CNTRL_1           REG(0x00, 0x21)     /* write */
# define VIP_CNTRL_1_MIRR_C       (1 << 7)
# define VIP_CNTRL_1_SWAP_C(x)    (((x) & 7) << 4)
# define VIP_CNTRL_1_MIRR_D       (1 << 3)
# define VIP_CNTRL_1_SWAP_D(x)    (((x) & 7) << 0)

#define REG_VIP_CNTRL_2           REG(0x00, 0x22)     /* write */
# define VIP_CNTRL_2_MIRR_E       (1 << 7)
# define VIP_CNTRL_2_SWAP_E(x)    (((x) & 7) << 4)
# define VIP_CNTRL_2_MIRR_F       (1 << 3)
# define VIP_CNTRL_2_SWAP_F(x)    (((x) & 7) << 0)

#define REG_VIP_CNTRL_3           REG(0x00, 0x23)     /* write */
# define VIP_CNTRL_3_X_TGL        (1 << 0)
# define VIP_CNTRL_3_H_TGL        (1 << 1)
# define VIP_CNTRL_3_V_TGL        (1 << 2)
# define VIP_CNTRL_3_EMB          (1 << 3)
# define VIP_CNTRL_3_SYNC_DE      (1 << 4)
# define VIP_CNTRL_3_SYNC_HS      (1 << 5)
# define VIP_CNTRL_3_DE_INT       (1 << 6)
# define VIP_CNTRL_3_EDGE         (1 << 7)

#define REG_VIP_CNTRL_4           REG(0x00, 0x24)     /* write */
# define VIP_CNTRL_4_BLC(x)       (((x) & 3) << 0)
# define VIP_CNTRL_4_BLANKIT(x)   (((x) & 3) << 2)
# define VIP_CNTRL_4_CCIR656      (1 << 4)
# define VIP_CNTRL_4_656_ALT      (1 << 5)
# define VIP_CNTRL_4_TST_656      (1 << 6)
# define VIP_CNTRL_4_TST_PAT      (1 << 7)

#define REG_VIP_CNTRL_5           REG(0x00, 0x25)     /* write */
# define VIP_CNTRL_5_CKCASE       (1 << 0)
# define VIP_CNTRL_5_SP_CNT(x)    (((x) & 3) << 1)

#define REG_MUX_AP                REG(0x00, 0x26)     /* read/write */
# define MUX_AP_SELECT_I2S    0x64
# define MUX_AP_SELECT_SPDIF      0x40

#define REG_MUX_VP_VIP_OUT        REG(0x00, 0x27)     /* read/write */
#define REG_MAT_CONTRL            REG(0x00, 0x80)     /* write */
# define MAT_CONTRL_MAT_SC(x)     (((x) & 3) << 0)
# define MAT_CONTRL_MAT_BP        (1 << 2)

#define REG_VIDFORMAT             REG(0x00, 0xa0)     /* write */
#define REG_REFPIX_MSB            REG(0x00, 0xa1)     /* write */
#define REG_REFPIX_LSB            REG(0x00, 0xa2)     /* write */
#define REG_REFLINE_MSB           REG(0x00, 0xa3)     /* write */
#define REG_REFLINE_LSB           REG(0x00, 0xa4)     /* write */
#define REG_NPIX_MSB              REG(0x00, 0xa5)     /* write */
#define REG_NPIX_LSB              REG(0x00, 0xa6)     /* write */
#define REG_NLINE_MSB             REG(0x00, 0xa7)     /* write */
#define REG_NLINE_LSB             REG(0x00, 0xa8)     /* write */
#define REG_VS_LINE_STRT_1_MSB    REG(0x00, 0xa9)     /* write */
#define REG_VS_LINE_STRT_1_LSB    REG(0x00, 0xaa)     /* write */
#define REG_VS_PIX_STRT_1_MSB     REG(0x00, 0xab)     /* write */
#define REG_VS_PIX_STRT_1_LSB     REG(0x00, 0xac)     /* write */
#define REG_VS_LINE_END_1_MSB     REG(0x00, 0xad)     /* write */
#define REG_VS_LINE_END_1_LSB     REG(0x00, 0xae)     /* write */
#define REG_VS_PIX_END_1_MSB      REG(0x00, 0xaf)     /* write */
#define REG_VS_PIX_END_1_LSB      REG(0x00, 0xb0)     /* write */
#define REG_VS_LINE_STRT_2_MSB    REG(0x00, 0xb1)     /* write */
#define REG_VS_LINE_STRT_2_LSB    REG(0x00, 0xb2)     /* write */
#define REG_VS_PIX_STRT_2_MSB     REG(0x00, 0xb3)     /* write */
#define REG_VS_PIX_STRT_2_LSB     REG(0x00, 0xb4)     /* write */
#define REG_VS_LINE_END_2_MSB     REG(0x00, 0xb5)     /* write */
#define REG_VS_LINE_END_2_LSB     REG(0x00, 0xb6)     /* write */
#define REG_VS_PIX_END_2_MSB      REG(0x00, 0xb7)     /* write */
#define REG_VS_PIX_END_2_LSB      REG(0x00, 0xb8)     /* write */
#define REG_HS_PIX_START_MSB      REG(0x00, 0xb9)     /* write */
#define REG_HS_PIX_START_LSB      REG(0x00, 0xba)     /* write */
#define REG_HS_PIX_STOP_MSB       REG(0x00, 0xbb)     /* write */
#define REG_HS_PIX_STOP_LSB       REG(0x00, 0xbc)     /* write */
#define REG_VWIN_START_1_MSB      REG(0x00, 0xbd)     /* write */
#define REG_VWIN_START_1_LSB      REG(0x00, 0xbe)     /* write */
#define REG_VWIN_END_1_MSB        REG(0x00, 0xbf)     /* write */
#define REG_VWIN_END_1_LSB        REG(0x00, 0xc0)     /* write */
#define REG_VWIN_START_2_MSB      REG(0x00, 0xc1)     /* write */
#define REG_VWIN_START_2_LSB      REG(0x00, 0xc2)     /* write */
#define REG_VWIN_END_2_MSB        REG(0x00, 0xc3)     /* write */
#define REG_VWIN_END_2_LSB        REG(0x00, 0xc4)     /* write */
#define REG_DE_START_MSB          REG(0x00, 0xc5)     /* write */
#define REG_DE_START_LSB          REG(0x00, 0xc6)     /* write */
#define REG_DE_STOP_MSB           REG(0x00, 0xc7)     /* write */
#define REG_DE_STOP_LSB           REG(0x00, 0xc8)     /* write */
#define REG_TBG_CNTRL_0           REG(0x00, 0xca)     /* write */
# define TBG_CNTRL_0_TOP_TGL      (1 << 0)
# define TBG_CNTRL_0_TOP_SEL      (1 << 1)
# define TBG_CNTRL_0_DE_EXT       (1 << 2)
# define TBG_CNTRL_0_TOP_EXT      (1 << 3)
# define TBG_CNTRL_0_FRAME_DIS    (1 << 5)
# define TBG_CNTRL_0_SYNC_MTHD    (1 << 6)
# define TBG_CNTRL_0_SYNC_ONCE    (1 << 7)

#define REG_TBG_CNTRL_1           REG(0x00, 0xcb)     /* write */
# define TBG_CNTRL_1_H_TGL        (1 << 0)
# define TBG_CNTRL_1_V_TGL        (1 << 1)
# define TBG_CNTRL_1_TGL_EN       (1 << 2)
# define TBG_CNTRL_1_X_EXT        (1 << 3)
# define TBG_CNTRL_1_H_EXT        (1 << 4)
# define TBG_CNTRL_1_V_EXT        (1 << 5)
# define TBG_CNTRL_1_DWIN_DIS     (1 << 6)

#define REG_ENABLE_SPACE          REG(0x00, 0xd6)     /* write */
#define REG_HVF_CNTRL_0           REG(0x00, 0xe4)     /* write */
# define HVF_CNTRL_0_SM           (1 << 7)
# define HVF_CNTRL_0_RWB          (1 << 6)
# define HVF_CNTRL_0_PREFIL(x)    (((x) & 3) << 2)
# define HVF_CNTRL_0_INTPOL(x)    (((x) & 3) << 0)

#define REG_HVF_CNTRL_1           REG(0x00, 0xe5)     /* write */
# define HVF_CNTRL_1_FOR          (1 << 0)
# define HVF_CNTRL_1_YUVBLK       (1 << 1)
# define HVF_CNTRL_1_VQR(x)       (((x) & 3) << 2)
# define HVF_CNTRL_1_PAD(x)       (((x) & 3) << 4)
# define HVF_CNTRL_1_SEMI_PLANAR  (1 << 6)

#define REG_RPT_CNTRL             REG(0x00, 0xf0)     /* write */
# define RPT_CNTRL_REPEAT(x)      ((x) & 15)

#define REG_I2S_FORMAT            REG(0x00, 0xfc)     /* read/write */
# define I2S_FORMAT_PHILIPS       (0 << 0)
# define I2S_FORMAT_LEFT_J        (2 << 0)
# define I2S_FORMAT_RIGHT_J       (3 << 0)

#define REG_AIP_CLKSEL            REG(0x00, 0xfd)     /* write */
# define AIP_CLKSEL_AIP_SPDIF     (0 << 3)
# define AIP_CLKSEL_AIP_I2S   (1 << 3)
# define AIP_CLKSEL_FS_ACLK   (0 << 0)
# define AIP_CLKSEL_FS_MCLK   (1 << 0)
# define AIP_CLKSEL_FS_FS64SPDIF  (2 << 0)

/* Page 02h: PLL settings */
#define REG_PLL_SERIAL_1          REG(0x02, 0x00)     /* read/write */
# define PLL_SERIAL_1_SRL_FDN     (1 << 0)
# define PLL_SERIAL_1_SRL_IZ(x)   (((x) & 3) << 1)
# define PLL_SERIAL_1_SRL_MAN_IZ  (1 << 6)

#define REG_PLL_SERIAL_2          REG(0x02, 0x01)     /* read/write */
# define PLL_SERIAL_2_SRL_NOSC(x) ((x) << 0)
# define PLL_SERIAL_2_SRL_PR(x)   (((x) & 0xf) << 4)

#define REG_PLL_SERIAL_3          REG(0x02, 0x02)     /* read/write */
# define PLL_SERIAL_3_SRL_CCIR    (1 << 0)
# define PLL_SERIAL_3_SRL_DE      (1 << 2)
# define PLL_SERIAL_3_SRL_PXIN_SEL (1 << 4)

#define REG_SERIALIZER            REG(0x02, 0x03)     /* read/write */
#define REG_BUFFER_OUT            REG(0x02, 0x04)     /* read/write */
#define REG_PLL_SCG1              REG(0x02, 0x05)     /* read/write */
#define REG_PLL_SCG2              REG(0x02, 0x06)     /* read/write */
#define REG_PLL_SCGN1             REG(0x02, 0x07)     /* read/write */
#define REG_PLL_SCGN2             REG(0x02, 0x08)     /* read/write */
#define REG_PLL_SCGR1             REG(0x02, 0x09)     /* read/write */
#define REG_PLL_SCGR2             REG(0x02, 0x0a)     /* read/write */
#define REG_AUDIO_DIV             REG(0x02, 0x0e)     /* read/write */
# define AUDIO_DIV_SERCLK_1       0
# define AUDIO_DIV_SERCLK_2       1
# define AUDIO_DIV_SERCLK_4       2
# define AUDIO_DIV_SERCLK_8       3
# define AUDIO_DIV_SERCLK_16      4
# define AUDIO_DIV_SERCLK_32      5

#define REG_SEL_CLK               REG(0x02, 0x11)     /* read/write */
# define SEL_CLK_SEL_CLK1         (1 << 0)
# define SEL_CLK_SEL_VRF_CLK(x)   (((x) & 3) << 1)
# define SEL_CLK_ENA_SC_CLK       (1 << 3)

#define REG_ANA_GENERAL           REG(0x02, 0x12)     /* read/write */

/* Page 09h: EDID Control */
#define REG_EDID_DATA_0           REG(0x09, 0x00)     /* read */
/* next 127 successive registers are the EDID block */
#define REG_EDID_CTRL             REG(0x09, 0xfa)     /* read/write */
#define REG_DDC_ADDR              REG(0x09, 0xfb)     /* read/write */
#define REG_DDC_OFFS              REG(0x09, 0xfc)     /* read/write */
#define REG_DDC_SEGM_ADDR         REG(0x09, 0xfd)     /* read/write */
#define REG_DDC_SEGM              REG(0x09, 0xfe)     /* read/write */

/* Page 10h: information frames and packets */
#define REG_IF1_HB0               REG(0x10, 0x20)     /* read/write */
#define REG_IF2_HB0               REG(0x10, 0x40)     /* read/write */
#define REG_IF3_HB0               REG(0x10, 0x60)     /* read/write */
#define REG_IF4_HB0               REG(0x10, 0x80)     /* read/write */
#define REG_IF5_HB0               REG(0x10, 0xa0)     /* read/write */

/* Page 11h: audio settings and content info packets */
#define REG_AIP_CNTRL_0           REG(0x11, 0x00)     /* read/write */
# define AIP_CNTRL_0_RST_FIFO     (1 << 0)
# define AIP_CNTRL_0_SWAP         (1 << 1)
# define AIP_CNTRL_0_LAYOUT       (1 << 2)
# define AIP_CNTRL_0_ACR_MAN      (1 << 5)
# define AIP_CNTRL_0_RST_CTS      (1 << 6)

#define REG_CA_I2S                REG(0x11, 0x01)     /* read/write */
# define CA_I2S_CA_I2S(x)         (((x) & 31) << 0)
# define CA_I2S_HBR_CHSTAT        (1 << 6)

#define REG_LATENCY_RD            REG(0x11, 0x04)     /* read/write */
#define REG_ACR_CTS_0             REG(0x11, 0x05)     /* read/write */
#define REG_ACR_CTS_1             REG(0x11, 0x06)     /* read/write */
#define REG_ACR_CTS_2             REG(0x11, 0x07)     /* read/write */
#define REG_ACR_N_0               REG(0x11, 0x08)     /* read/write */
#define REG_ACR_N_1               REG(0x11, 0x09)     /* read/write */
#define REG_ACR_N_2               REG(0x11, 0x0a)     /* read/write */
#define REG_CTS_N                 REG(0x11, 0x0c)     /* read/write */
# define CTS_N_K(x)               (((x) & 7) << 0)
# define CTS_N_M(x)               (((x) & 3) << 4)

#define REG_ENC_CNTRL             REG(0x11, 0x0d)     /* read/write */
# define ENC_CNTRL_RST_ENC        (1 << 0)
# define ENC_CNTRL_RST_SEL        (1 << 1)
# define ENC_CNTRL_CTL_CODE(x)    (((x) & 3) << 2)

#define REG_DIP_FLAGS             REG(0x11, 0x0e)     /* read/write */
# define DIP_FLAGS_ACR            (1 << 0)
# define DIP_FLAGS_GC             (1 << 1)

#define REG_DIP_IF_FLAGS          REG(0x11, 0x0f)     /* read/write */
# define DIP_IF_FLAGS_IF1         (1 << 1)
# define DIP_IF_FLAGS_IF2         (1 << 2)
# define DIP_IF_FLAGS_IF3         (1 << 3)
# define DIP_IF_FLAGS_IF4         (1 << 4)
# define DIP_IF_FLAGS_IF5         (1 << 5)

#define REG_CH_STAT_B(x)          REG(0x11, 0x14 + (x)) /* read/write */

/* Page 12h: HDCP and OTP */
#define REG_TX3                   REG(0x12, 0x9a)     /* read/write */
#define REG_TX4                   REG(0x12, 0x9b)     /* read/write */
# define TX4_PD_RAM               (1 << 1)
#define REG_TX33                  REG(0x12, 0xb8)     /* read/write */
# define TX33_HDMI                (1 << 1)

/* Page 13h: Gamut related metadata packets */

/* CEC registers: (not paged)
 */
#define REG_CEC_INTSTATUS     0xee            /* read */
# define CEC_INTSTATUS_CEC    (1 << 0)
# define CEC_INTSTATUS_HDMI   (1 << 1)
#define REG_CEC_CAL_XOSC_CTRL1    0xf2
# define CEC_CAL_XOSC_CTRL1_ENA_CAL BIT(0)
#define REG_CEC_DES_FREQ2         0xf5
# define CEC_DES_FREQ2_DIS_AUTOCAL BIT(7)
#define REG_CEC_CLK               0xf6
# define CEC_CLK_FRO              0x11
#define REG_CEC_FRO_IM_CLK_CTRL   0xfb                /* read/write */
# define CEC_FRO_IM_CLK_CTRL_GHOST_DIS (1 << 7)
# define CEC_FRO_IM_CLK_CTRL_ENA_OTP   (1 << 6)
# define CEC_FRO_IM_CLK_CTRL_IMCLK_SEL (1 << 1)
# define CEC_FRO_IM_CLK_CTRL_FRO_DIV   (1 << 0)
#define REG_CEC_RXSHPDINTENA      0xfc            /* read/write */
#define REG_CEC_RXSHPDINT     0xfd            /* read */
# define CEC_RXSHPDINT_RXSENS     BIT(0)
# define CEC_RXSHPDINT_HPD        BIT(1)
#define REG_CEC_RXSHPDLEV         0xfe                /* read */
# define CEC_RXSHPDLEV_RXSENS     (1 << 0)
# define CEC_RXSHPDLEV_HPD        (1 << 1)

#define REG_CEC_ENAMODS           0xff                /* read/write */
# define CEC_ENAMODS_EN_CEC_CLK   (1 << 7)
# define CEC_ENAMODS_DIS_FRO      (1 << 6)
# define CEC_ENAMODS_DIS_CCLK     (1 << 5)
# define CEC_ENAMODS_EN_RXSENS    (1 << 2)
# define CEC_ENAMODS_EN_HDMI      (1 << 1)
# define CEC_ENAMODS_EN_CEC       (1 << 0)

void tda19988_testmode( void ) {

    uint8_t master_test[3];

    master_test[0] = 0xFF;
    master_test[1] = 0x87;
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA19988_CEC, &master_test, 2, 1000);

    master_test[0] = 0xFF;
    master_test[1] = 0x00;
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA19988_HDMI, &master_test, 2, 1000);

    master_test[0] = 0xA0;
    master_test[1] = 0x07;    // pre-defined video formats
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA19988_HDMI, &master_test, 2, 1000);
    
    master_test[0] = 0xE4;
    master_test[1] = 0xC0;  // generate test pattern
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA19988_HDMI, &master_test, 2, 1000);

    master_test[0] = 0xF0;
    master_test[1] = 0x00;
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA19988_HDMI, &master_test, 2, 1000);

}

void w_reg(uint16_t reg, uint8_t val) {

    uint8_t buf[2];
    buf[0] = REG_CURPAGE;
    buf[1] = REG2PAGE(reg);
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &buf, 2, 1000);

    buf[0] = REG2ADDR(reg);
    buf[1] = val;
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &buf, 2, 1000);

}

void w16_reg(uint16_t reg, uint16_t val) {
    uint8_t buf[2];
    buf[0] = REG_CURPAGE;
    buf[1] = REG2PAGE(reg);
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &buf, 2, 1000);

    buf[0] = REG2ADDR(reg);
    buf[1] = val>>8;
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &buf, 2, 1000);

    buf[0] = REG2ADDR(reg+1);
    buf[1] = val;
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &buf, 2, 1000);
}

uint8_t r_reg(uint16_t reg) {

    uint8_t buf[2];
    buf[0] = REG_CURPAGE;
    buf[1] = REG2PAGE(reg);
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &buf, 2, 1000);

    buf[0] = REG2ADDR(reg);
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &buf, 1, 1000);
    HAL_I2C_Master_Receive(&hi2c1, I2C_ADDRESS_TDA, &buf, 1, 1000);

    return buf[0];
}

uint8_t EDID[256];

uint16_t r_reg_range(uint16_t reg, uint8_t *buf, uint16_t cnt) {

    uint8_t tmp[2];
    tmp[0] = REG_CURPAGE;
    tmp[1] = REG2PAGE(reg);
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &tmp, 2, 1000);

    tmp[0] = REG2ADDR(reg);
    HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDRESS_TDA, &tmp, 1, 1000);
    HAL_I2C_Master_Receive(&hi2c1, I2C_ADDRESS_TDA, buf, cnt, 1000);

    return buf[0];
}

void s_reg(uint16_t reg, uint8_t val){

    uint8_t old_val;

    old_val = r_reg(reg);
    old_val |= val;
    w_reg(reg, old_val);
}

void c_reg(uint16_t reg, uint8_t val){

    uint8_t old_val;

    old_val = r_reg(reg);
    old_val &= ~val;
    w_reg(reg, old_val);
}

void read_edid(void) {
    w_reg(0x00F9, 0x00);
    w_reg(0x00FE, 0xa0);

    HAL_I2C_Mem_Read(&hi2c1, 0xa0, 0, I2C_MEMADD_SIZE_8BIT, &EDID[0], 256, 1000);

}

uint8_t debug[200];
extern LTDC_HandleTypeDef hltdc;

void tda_init(void) {

    read_edid();

    uint8_t reg;
 
    uint8_t div = 148500 / ((LTDCSYNC[LTDC_VID_FORMAT].pll3n/LTDCSYNC[LTDC_VID_FORMAT].pll3r)*1000);
    uint16_t line_clocks, lines;

    div--;
    if (div > 3)
        div = 3;

    /* first disable the video ports */
    w_reg(REG_ENA_VP_0, 0);
    w_reg(REG_ENA_VP_1, 0);
    w_reg(REG_ENA_VP_2, 0);

    /* shutdown audio */
    w_reg(REG_ENA_AP, 0);

    line_clocks = LTDCSYNC[LTDC_VID_FORMAT].totalw+1; 
    lines =       LTDCSYNC[LTDC_VID_FORMAT].totalh+1;

    /* mute the audio FIFO */
    s_reg(REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);
    /* HDMI HDCP: off */
    w_reg(REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
    c_reg(REG_TX33, TX33_HDMI);
    w_reg(REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));

    /* no pre-filter or interpolator */
    w_reg(REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) | HVF_CNTRL_0_INTPOL(0));
    s_reg(REG_FEAT_POWERDOWN,FEAT_POWERDOWN_PREFILT);
    w_reg(REG_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
    w_reg(REG_VIP_CNTRL_4, VIP_CNTRL_4_BLANKIT(0) | VIP_CNTRL_4_BLC(0) | VIP_CNTRL_4_TST_PAT);

    c_reg(REG_PLL_SERIAL_1, PLL_SERIAL_1_SRL_MAN_IZ);
    c_reg(REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_CCIR | PLL_SERIAL_3_SRL_DE);

    w_reg(REG_SERIALIZER, 0);
    w_reg(REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));

    w_reg(REG_RPT_CNTRL, 0);
    w_reg(REG_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) | SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
    w_reg(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(div) | PLL_SERIAL_2_SRL_PR(0));

    /* set color matrix bypass flag: */
    w_reg(REG_MAT_CONTRL, MAT_CONTRL_MAT_BP | MAT_CONTRL_MAT_SC(1));
    s_reg(REG_FEAT_POWERDOWN, FEAT_POWERDOWN_CSC);

    /* set BIAS tmds value: */
    w_reg(REG_ANA_GENERAL, 0x09);


    reg = VIP_CNTRL_3_SYNC_HS;

/*
    if(hltdc.Init.HSPolarity == LTDC_HSPOLARITY_AL) {
        reg |= VIP_CNTRL_3_H_TGL;
    };
    if(hltdc.Init.VSPolarity == LTDC_VSPOLARITY_AL) {
        reg |= VIP_CNTRL_3_V_TGL;    
    };
    if(hltdc.Init.DEPolarity == LTDC_DEPOLARITY_AL) {
        reg |= VIP_CNTRL_3_X_TGL;    
    };
    if(hltdc.Init.PCPolarity == LTDC_PCPOLARITY_IIPC) {
        reg |= VIP_CNTRL_3_EDGE;    
    };
*/
    w_reg(REG_VIP_CNTRL_3, reg);


    w_reg(REG_VIDFORMAT, 0x00);
    w16_reg(REG_REFPIX_MSB, LTDCSYNC[LTDC_VID_FORMAT].hfp); //+3
    w16_reg(REG_REFLINE_MSB, LTDCSYNC[LTDC_VID_FORMAT].vfp); //+1
    w16_reg(REG_NPIX_MSB, line_clocks);
    w16_reg(REG_NLINE_MSB, lines);
    w16_reg(REG_VS_LINE_STRT_1_MSB, LTDCSYNC[LTDC_VID_FORMAT].vfp);
    w16_reg(REG_VS_PIX_STRT_1_MSB, LTDCSYNC[LTDC_VID_FORMAT].hfp);
    w16_reg(REG_VS_LINE_END_1_MSB, LTDCSYNC[LTDC_VID_FORMAT].vfp + LTDCSYNC[LTDC_VID_FORMAT].vsync);
    w16_reg(REG_VS_PIX_END_1_MSB, LTDCSYNC[LTDC_VID_FORMAT].hfp);
    w16_reg(REG_VS_LINE_STRT_2_MSB, 0);
    w16_reg(REG_VS_PIX_STRT_2_MSB, 0);
    w16_reg(REG_VS_LINE_END_2_MSB, 0);
    w16_reg(REG_VS_PIX_END_2_MSB, 0);
    w16_reg(REG_HS_PIX_START_MSB, LTDCSYNC[LTDC_VID_FORMAT].hfp);
    w16_reg(REG_HS_PIX_STOP_MSB, LTDCSYNC[LTDC_VID_FORMAT].hfp + LTDCSYNC[LTDC_VID_FORMAT].hsync);
    w16_reg(REG_VWIN_START_1_MSB, lines - LTDCSYNC[LTDC_VID_FORMAT].avh); //-1
    w16_reg(REG_VWIN_END_1_MSB, lines); //-1
    w16_reg(REG_VWIN_START_2_MSB, 0);
    w16_reg(REG_VWIN_END_2_MSB, 0);
    w16_reg(REG_DE_START_MSB, line_clocks - LTDCSYNC[LTDC_VID_FORMAT].ahw);
    w16_reg(REG_DE_STOP_MSB, line_clocks);

    /*
     * Always generate sync polarity relative to input sync and
     * revert input stage toggled sync at output stage
     */
    /*
    reg = TBG_CNTRL_1_DWIN_DIS | TBG_CNTRL_1_TGL_EN;
    if (hltdc.Init.HSPolarity == LTDC_HSPOLARITY_AL)
        reg |= TBG_CNTRL_1_H_TGL;
    if (hltdc.Init.VSPolarity == LTDC_VSPOLARITY_AL)
        reg |= TBG_CNTRL_1_V_TGL;
    w_reg(REG_TBG_CNTRL_1, reg);
*/
    /* must be last register set: */
    w_reg(REG_TBG_CNTRL_0, 0);

    /* turn on HDMI HDCP */
    reg &= ~TBG_CNTRL_1_DWIN_DIS;
    w_reg(REG_TBG_CNTRL_1, reg);
    w_reg(REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(1));
    //s_reg(REG_TX33, TX33_HDMI);

    HAL_Delay(400);

    /* enable video ports */
    w_reg(REG_ENA_VP_0, 0xff);
    w_reg(REG_ENA_VP_1, 0xff);
    w_reg(REG_ENA_VP_2, 0xff);
    /* set muxing after enabling ports: */
    w_reg(REG_VIP_CNTRL_0, VIP_CNTRL_0_SWAP_A(2) | VIP_CNTRL_0_SWAP_B(3));
    w_reg(REG_VIP_CNTRL_1, VIP_CNTRL_1_SWAP_C(4) | VIP_CNTRL_1_SWAP_D(5));
    w_reg(REG_VIP_CNTRL_2, VIP_CNTRL_2_SWAP_E(0) | VIP_CNTRL_2_SWAP_F(1));

}

