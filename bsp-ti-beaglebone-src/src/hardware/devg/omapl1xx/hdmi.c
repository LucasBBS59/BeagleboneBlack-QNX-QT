/*
 * $QNXLicenseC:
 * Copyright 2011, QNX Software Systems.
 * Copyright 2017 Varghese Alphonse
 *
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You
 * may not reproduce, modify or distribute this software except in
 * compliance with the License. You may obtain a copy of the License
 * at: http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTIES OF ANY KIND, either express or implied.
 *
 * This file may contain contributions from others, either as
 * contributors under the License or as licensors under other terms.
 * Please review this entire file for other proprietary rights or license
 * notices, as well as the QNX Development Suite License Guide at
 * http://licensing.qnx.com/license-guide/ for other information.
 * $
 */

#include <stddef.h>
#include <devctl.h>
#include <sys/iofunc.h>
#include <sys/dispatch.h>
#include <fcntl.h>
#include <pthread.h>
#include <hw/i2c.h>

#include "omapl1xx.h"
#include "hdmi.h"

//#define AM335X_HDMI_DEBUG

#define LOG_ERR		stderr

#define HDMI_ADDR	0x39
//#define HDMI_ADDR	0x72

//
// TDA19988
#define I2C_HDMI_ADDR				(0x70)
#define I2C_HDMI_BUS				(1)
#define I2C_CEC_ADDR				(0x34)
#define I2C_CEC_BUS					(1)



#define CECWrite(subAddr, pBuffer, size)		I2CSetSlaveAddress(g_hI2CDevice, (UINT16)I2C_CEC_ADDR); \
													I2CWrite(g_hI2CDevice, subAddr, pBuffer, size)

#define CECRead(subAddr, pBuffer, size)			I2CSetSlaveAddress(g_hI2CDevice, (UINT16)I2C_CEC_ADDR); \
													I2CRead(g_hI2CDevice, subAddr, pBuffer, size)

#define REG(page, addr) (((page) << 8) | (addr))
#define REG2ADDR(reg)   ((reg) & 0xff)
#define REG2PAGE(reg)   (((reg) >> 8) & 0xff)

#define REG_CURPAGE               0xff

typedef  uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef void* HANDLE;
typedef void VOID;
typedef uint32_t UINT;

struct tda998x_encoder_params {
	UINT8 swap_b:3;
	UINT8 mirr_b:1;
	UINT8 swap_a:3;
	UINT8 mirr_a:1;
	UINT8 swap_d:3;
	UINT8 mirr_d:1;
	UINT8 swap_c:3;
	UINT8 mirr_c:1;
	UINT8 swap_f:3;
	UINT8 mirr_f:1;
	UINT8 swap_e:3;
	UINT8 mirr_e:1;

	UINT8 audio_cfg;
	UINT8 audio_clk_cfg;
	UINT8 audio_frame[6];

	enum {
		AFMT_SPDIF,
		AFMT_I2S
	} audio_format;

	unsigned audio_sample_rate;
};


static UINT8	g_current_page;
static HANDLE   g_hI2CDevice = NULL;

//1280x1024 let me try

/* 0x51 - 1366x768@60Hz */
//{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 85500, 1366, 1436,
//	   1579, 1792, 0, 768, 771, 774, 798, 0,
	//   DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC) },
///* 0x56 - 1366x768@60Hz */
//{ DRM_MODE("1366x768", DRM_MODE_TYPE_DRIVER, 72000, 1366, 1380,
//	   1436, 1500, 0, 768, 769, 772, 800, 0,
// not verified
struct drm_display_mode drm_1366x768 = {
	85500,
	1366,
	1436,
	1579,
	1792,
	0x28,		// HSKEW
	768,
	771,
	777,
	806,
	0,
	(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_HSKEW)
};
//1280x720
struct drm_display_mode drm_1280x720 = {
	74250,
	1280,
	1390,
	1430,
	1650,
	0x28,		// HSKEW
	720,
	725,
	730,
	750,
	0,
	(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_HSKEW)
};

//1024x768
struct drm_display_mode drm_1024x768 = {
	65000,
	1024,
	1048,
	1184,
	1344,
	0x86,		// HSKEW
	768,
	771,
	777,
	806,
	0,
	(DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC | DRM_MODE_FLAG_HSKEW)
};

//800x600
struct drm_display_mode drm_800x600 = {
	40000,
	800,
	840,
	968,
	1056, //htotal

	0x80, // HSKEW

	600,
	601,
	605,
	628,//vtotal
	0,
	(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_HSKEW)
};


//800x480
struct drm_display_mode drm_800x480 = {
	40000,
	800,
	840,
	968,
	1056, //htotal

	0x80, // HSKEW

	480,
	482,
	485,
	528,//vtotal
	0,
	(DRM_MODE_FLAG_PHSYNC | DRM_MODE_FLAG_PVSYNC | DRM_MODE_FLAG_HSKEW)
};


struct display_panel panel_initx = {
	LCDC_PANEL_TFT | LCDC_INV_VSYNC | LCDC_INV_HSYNC | LCDC_HSVS_CONTROL,	// config
	DISPC_PIXELFORMAT_RGB16,	// bpp
	0,	// x_res
	0,	// y_res
	0,	// pixel_clock
	0,	// hsw
	0,	// hfp
	0,	// hbp
	0,	// vsw
	0,	// vfb
	0,	// vbp
	0,	// acb
};


static uint8_t tda19988_cec_read (disp_adapter_t *adapter, am335x_hdmi_t *dev, uint32_t addr)
{
   struct send_recv
   {
	    i2c_sendrecv_t hdr;
	    uint8_t buf1;
   } rd_data;

   rd_data.buf1 =addr;
   rd_data.hdr.send_len = 1;
   rd_data.hdr.recv_len = 1;

   rd_data.hdr.slave.addr = I2C_CEC_ADDR;
   rd_data.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
   rd_data.hdr.stop = 1;

	if( devctl( dev->i2c_fd, DCMD_I2C_SENDRECV, &rd_data, sizeof(rd_data), NULL))
	{
	    slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "tda19988_cec_read:HDMI I2C read failed (DCMD_I2C_SENDRECV)");
	    return (-1);
	}
	return rd_data.buf1;
}

static uint8_t tda19988_cec_write (disp_adapter_t *adapter, am335x_hdmi_t *dev, uint32_t addr, uint32_t reg1)
{
	uint16_t	        stat_ret;
    struct {
        i2c_send_t      hdr;
        uint8_t	        reg1;
        uint8_t	        reg2;
    } msgreg;
    uint16_t tmp;

    msgreg.hdr.slave.addr =     I2C_CEC_ADDR;
    msgreg.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
    msgreg.hdr.len        = 2;
    msgreg.hdr.stop       = 1;
    msgreg.reg1  =  addr;
    msgreg.reg2 =  reg1;

    stat_ret= devctl(dev->i2c_fd, DCMD_I2C_SEND, &msgreg, sizeof(msgreg), NULL);
    if(stat_ret)
    {
    	slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "tda19988_cec_write:HDMI I2C write  failed (DCMD_I2C_SEND)");
    	return (-1);
    }

    return (stat_ret);
}


static void
set_page(disp_adapter_t *adapter, am335x_hdmi_t *dev,uint16_t reg)
{
	uint16_t	        stat_ret;
    struct {
        i2c_send_t      hdr;
        uint8_t	        reg1;
        uint8_t	        reg2;
    } msgreg;

	uint8_t  page = REG2PAGE(reg);
	if (page != g_current_page)
	{
		 msgreg.hdr.slave.addr =     I2C_HDMI_ADDR;
		    msgreg.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
		    msgreg.hdr.len        = 2;
		    msgreg.hdr.stop       = 1;
		    msgreg.reg1  =  REG_CURPAGE;
		    msgreg.reg2  =  page;

		    stat_ret= devctl(dev->i2c_fd, DCMD_I2C_SEND, &msgreg, sizeof(msgreg), NULL);
		    if(stat_ret)
		    {
		    	slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "set_page :HDMI I2C write  failed (DCMD_I2C_SEND)");
		    	return (-1);
		    }
		g_current_page = page;
	}

}



static UINT8
reg_read(disp_adapter_t *adapter, am335x_hdmi_t *dev,uint16_t reg)
{
	uint8_t val = 0;
	struct send_recv
	{
	     i2c_sendrecv_t hdr;
	     uint8_t buf1;
	} rd_data;

	set_page(adapter,dev,reg);
	uint8_t page = REG2PAGE(reg);
	rd_data.buf1 =reg;
	rd_data.hdr.send_len = 1;
	rd_data.hdr.recv_len = 1;
	rd_data.hdr.slave.addr = I2C_HDMI_ADDR;
	rd_data.hdr.slave.fmt = I2C_ADDRFMT_7BIT;
	rd_data.hdr.stop = 1;

	if( devctl( dev->i2c_fd, DCMD_I2C_SENDRECV, &rd_data, sizeof(rd_data), NULL))
	{
	     slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "Reg_Read:HDMI I2C read failed (DCMD_I2C_SENDRECV)");
	     return (-1);
	}
	return rd_data.buf1;
}

static void
reg_write(disp_adapter_t *adapter, am335x_hdmi_t *dev,uint16_t reg, uint8_t val)
{
	 struct
	 {
		  i2c_send_t  hdr;
		  uint8_t     reg;
		  uint8_t     val;
	 } msg;
	set_page(adapter,dev,reg);

	msg.hdr.slave.addr = I2C_HDMI_ADDR;
	msg.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
	msg.hdr.len        = 2;
	msg.hdr.stop       = 1;
	msg.reg            = REG2ADDR(reg);
	msg.val            = val;

	if (devctl (dev->i2c_fd, DCMD_I2C_SEND, &msg, sizeof(msg), NULL))
	{
		     slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "reg_write:HDMI I2C write failed (DCMD_I2C_SEND)");
	 }
}

static void
reg_write16(disp_adapter_t *adapter, am335x_hdmi_t *dev,UINT16 reg, UINT16 val)
{
	UINT8 msb =0;
	UINT8 lsb =0;
	struct
	{
		i2c_send_t  hdr;
		uint8_t     reg;
		uint8_t     val;
    } msg;

	msb = (UINT8)(val >> 8);
	lsb = (UINT8)(val & 0xff);
	set_page(adapter,dev,reg);

	msg.hdr.slave.addr = I2C_HDMI_ADDR;
	msg.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
	msg.hdr.len        = 2;
	msg.hdr.stop       = 1;
	msg.reg            = REG2ADDR(reg);
	msg.val            = msb;

	if (devctl (dev->i2c_fd, DCMD_I2C_SEND, &msg, sizeof(msg), NULL))
    {
		slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "reg_write16:HDMI I2C write failed (DCMD_I2C_SEND)");
    }

	msg.hdr.slave.addr = I2C_HDMI_ADDR;
    msg.hdr.slave.fmt  = I2C_ADDRFMT_7BIT;
    msg.hdr.len        = 2;
    msg.hdr.stop       = 1;
    msg.reg            = REG2ADDR(reg+1);
	msg.val            = lsb;

	if (devctl (dev->i2c_fd, DCMD_I2C_SEND, &msg, sizeof(msg), NULL))
	{
		slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "reg_write16:HDMI I2C write failed (DCMD_I2C_SEND)");
	}
}


static void
reg_set(disp_adapter_t *adapter, am335x_hdmi_t *dev, uint16_t reg, uint8_t val)
{
	reg_write(adapter,dev,reg, reg_read(adapter,dev,reg) | val);
}


static void
reg_clear(disp_adapter_t *adapter, am335x_hdmi_t *dev,uint16_t reg, uint8_t val)
{
	reg_write(adapter,dev,reg, reg_read(adapter,dev,reg) & ~val);
}


 tda998x_reset(disp_adapter_t *adapter, am335x_hdmi_t *dev)
{

	/* reset audio and i2c master: */
		reg_set(adapter, dev,REG_SOFTRESET, SOFTRESET_AUDIO | SOFTRESET_I2C_MASTER);
		reg_clear(adapter, dev,REG_SOFTRESET, SOFTRESET_AUDIO | SOFTRESET_I2C_MASTER);

		/* reset transmitter: */
		reg_set(adapter, dev,REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);
		reg_clear(adapter, dev,REG_MAIN_CNTRL0, MAIN_CNTRL0_SR);

		/* PLL registers common configuration */
		reg_write(adapter, dev,REG_PLL_SERIAL_1, 0x00);
		reg_write(adapter, dev,REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));
		reg_write(adapter, dev,REG_PLL_SERIAL_3, 0x00);
		reg_write(adapter, dev,REG_SERIALIZER,   0x00);
		reg_write(adapter, dev,REG_BUFFER_OUT,   0x00);
		reg_write(adapter, dev,REG_PLL_SCG1,     0x00);
		reg_write(adapter, dev,REG_AUDIO_DIV,    AUDIO_DIV_SERCLK_8);
		reg_write(adapter, dev,REG_SEL_CLK,      SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);
		reg_write(adapter, dev,REG_PLL_SCGN1,    0xfa);
		reg_write(adapter, dev,REG_PLL_SCGN2,    0x00);
		reg_write(adapter, dev,REG_PLL_SCGR1,    0x5b);
		reg_write(adapter, dev,REG_PLL_SCGR2,    0x00);
		reg_write(adapter, dev,REG_PLL_SCG2,     0x10);

		/* Write the default value MUX register */
		reg_write(adapter, dev,REG_MUX_VP_VIP_OUT, 0x24);

}

int  tda998x_connected_detect(disp_adapter_t *adapter, am335x_hdmi_t *dev)
{
	UINT8 val;
	val=tda19988_cec_read(adapter, dev,REG_CEC_RXSHPDLEV);
	return (val & CEC_RXSHPDLEV_HPD);
}


/* DRM encoder functions */

int tda998x_encoder_dpms(disp_adapter_t *adapter, am335x_hdmi_t *dev)
{
		/* enable audio and video ports */
	reg_write(adapter, dev,REG_ENA_AP, 0x03);
	reg_write(adapter, dev,REG_ENA_VP_0, 0xff);
	reg_write(adapter, dev,REG_ENA_VP_1, 0xff);
	reg_write(adapter, dev,REG_ENA_VP_2, 0xff);
		/* set muxing after enabling ports: */
	reg_write(adapter, dev,REG_VIP_CNTRL_0, VIP_CNTRL_0_SWAP_A(2) | VIP_CNTRL_0_SWAP_B(3));
	reg_write(adapter, dev,REG_VIP_CNTRL_1,VIP_CNTRL_1_SWAP_C(0) | VIP_CNTRL_1_SWAP_D(1));
	reg_write(adapter, dev,REG_VIP_CNTRL_2,VIP_CNTRL_2_SWAP_E(4) | VIP_CNTRL_2_SWAP_F(5));
	return 1;
}

int tda998x_encoder_mode_set(disp_adapter_t *adapter, am335x_hdmi_t *dev)
{
	UINT16 ref_pix, ref_line, n_pix, n_line;
	UINT16 hs_pix_s, hs_pix_e;
	UINT16 vs1_pix_s, vs1_pix_e, vs1_line_s, vs1_line_e;
	UINT16 vs2_pix_s, vs2_pix_e, vs2_line_s, vs2_line_e;
	UINT16 vwin1_line_s, vwin1_line_e;
	UINT16 vwin2_line_s, vwin2_line_e;
	UINT16 de_pix_s, de_pix_e;
	UINT8 reg, div, rep;


	n_pix        = mode->htotal;
	n_line       = mode->vtotal;
	ref_pix      = 3 + mode->hsync_start - mode->hdisplay;
	if (mode->flags & DRM_MODE_FLAG_HSKEW)
		ref_pix += mode->hskew;

	de_pix_s     = mode->htotal - mode->hdisplay;
	de_pix_e     = de_pix_s + mode->hdisplay;
	hs_pix_s     = mode->hsync_start - mode->hdisplay;
	hs_pix_e     = hs_pix_s + mode->hsync_end - mode->hsync_start;

	if ((mode->flags & DRM_MODE_FLAG_INTERLACE) == 0) {
		ref_line     = 1 + mode->vsync_start - mode->vdisplay;
		vwin1_line_s = mode->vtotal - mode->vdisplay - 1;
		vwin1_line_e = vwin1_line_s + mode->vdisplay;
		vs1_pix_s    = vs1_pix_e = hs_pix_s;
		vs1_line_s   = mode->vsync_start - mode->vdisplay;
		vs1_line_e   = vs1_line_s +
				   mode->vsync_end - mode->vsync_start;
		vwin2_line_s = vwin2_line_e = 0;
		vs2_pix_s    = vs2_pix_e  = 0;
		vs2_line_s   = vs2_line_e = 0;
	} else {
		ref_line     = 1 + (mode->vsync_start - mode->vdisplay)/2;
		vwin1_line_s = (mode->vtotal - mode->vdisplay)/2;
		vwin1_line_e = vwin1_line_s + mode->vdisplay/2;
		vs1_pix_s    = vs1_pix_e = hs_pix_s;
		vs1_line_s   = (mode->vsync_start - mode->vdisplay)/2;
		vs1_line_e   = vs1_line_s +
				   (mode->vsync_end - mode->vsync_start)/2;
		vwin2_line_s = vwin1_line_s + mode->vtotal/2;
		vwin2_line_e = vwin2_line_s + mode->vdisplay/2;
		vs2_pix_s    = vs2_pix_e = hs_pix_s + mode->htotal/2;
		vs2_line_s   = vs1_line_s + mode->vtotal/2 ;
		vs2_line_e   = vs2_line_s +
				(mode->vsync_end - mode->vsync_start)/2;
	}

	div = 148500 / mode->clock;

/*	if (div != 0) {
			div--;
			if (div > 3)
				div = 3;
		}*/
	/* mute the audio FIFO: */
	reg_set(adapter, dev,REG_AIP_CNTRL_0, AIP_CNTRL_0_RST_FIFO);

	/* set HDMI HDCP mode off: */
	reg_set(adapter, dev,REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
	reg_clear(adapter, dev,REG_TX33, TX33_HDMI);
	reg_write(adapter, dev,REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(0));

	/* no pre-filter or interpolator: */
	reg_write(adapter, dev,REG_HVF_CNTRL_0, HVF_CNTRL_0_PREFIL(0) |
			HVF_CNTRL_0_INTPOL(0));
	reg_write(adapter, dev,REG_VIP_CNTRL_5, VIP_CNTRL_5_SP_CNT(0));
	reg_write(adapter, dev,REG_VIP_CNTRL_4, VIP_CNTRL_4_BLANKIT(0) |
			VIP_CNTRL_4_BLC(0));
	reg_clear(adapter, dev,REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_CCIR);

	reg_clear(adapter, dev,REG_PLL_SERIAL_1, PLL_SERIAL_1_SRL_MAN_IZ);
	reg_clear(adapter, dev,REG_PLL_SERIAL_3, PLL_SERIAL_3_SRL_DE);
	reg_write(adapter, dev,REG_SERIALIZER, 0);
	reg_write(adapter, dev,REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));

	/* TODO enable pixel repeat for pixel rates less than 25Msamp/s */
	rep = 0;
	reg_write(adapter, dev,REG_RPT_CNTRL, 0);
	reg_write(adapter, dev,REG_SEL_CLK, SEL_CLK_SEL_VRF_CLK(0) |
			SEL_CLK_SEL_CLK1 | SEL_CLK_ENA_SC_CLK);

	reg_write(adapter, dev,REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(div) |
			PLL_SERIAL_2_SRL_PR(rep));

	/* set color matrix bypass flag: */
	reg_set(adapter, dev,REG_MAT_CONTRL, MAT_CONTRL_MAT_BP);

	/* set BIAS tmds value: */
	reg_write(adapter, dev,REG_ANA_GENERAL, 0x09);

	reg_clear(adapter, dev,REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_MTHD);

	/*
	 * Sync on rising HSYNC/VSYNC
	 */
	reg_write(adapter, dev,REG_VIP_CNTRL_3, 0);
	reg_set(adapter, dev,REG_VIP_CNTRL_3, VIP_CNTRL_3_SYNC_HS);

	/*
	 * TDA19988 requires high-active sync at input stage,
	 * so invert low-active sync provided by master encoder here
	 */
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg_set(adapter, dev,REG_VIP_CNTRL_3, VIP_CNTRL_3_H_TGL);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg_set(adapter, dev,REG_VIP_CNTRL_3, VIP_CNTRL_3_V_TGL);

	/*
	 * Always generate sync polarity relative to input sync and
	 * revert input stage toggled sync at output stage
	 */
	reg = TBG_CNTRL_1_TGL_EN;
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg |= TBG_CNTRL_1_H_TGL;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg |= TBG_CNTRL_1_V_TGL;
	reg_write(adapter, dev,REG_TBG_CNTRL_1, reg);

	reg_write(adapter, dev,REG_VIDFORMAT, 0x00);
	reg_write16(adapter, dev,REG_REFPIX_MSB, ref_pix);
	reg_write16(adapter, dev,REG_REFLINE_MSB, ref_line);
	reg_write16(adapter, dev,REG_NPIX_MSB, n_pix);
	reg_write16(adapter, dev,REG_NLINE_MSB, n_line);
	reg_write16(adapter, dev,REG_VS_LINE_STRT_1_MSB, vs1_line_s);
	reg_write16(adapter, dev,REG_VS_PIX_STRT_1_MSB, vs1_pix_s);
	reg_write16(adapter, dev,REG_VS_LINE_END_1_MSB, vs1_line_e);
	reg_write16(adapter, dev,REG_VS_PIX_END_1_MSB, vs1_pix_e);
	reg_write16(adapter, dev,REG_VS_LINE_STRT_2_MSB, vs2_line_s);
	reg_write16(adapter, dev,REG_VS_PIX_STRT_2_MSB, vs2_pix_s);
	reg_write16(adapter, dev,REG_VS_LINE_END_2_MSB, vs2_line_e);
	reg_write16(adapter, dev,REG_VS_PIX_END_2_MSB, vs2_pix_e);
	reg_write16(adapter, dev,REG_HS_PIX_START_MSB, hs_pix_s);
	reg_write16(adapter, dev,REG_HS_PIX_STOP_MSB, hs_pix_e);
	reg_write16(adapter, dev,REG_VWIN_START_1_MSB, vwin1_line_s);
	reg_write16(adapter, dev,REG_VWIN_END_1_MSB, vwin1_line_e);
	reg_write16(adapter, dev,REG_VWIN_START_2_MSB, vwin2_line_s);
	reg_write16(adapter, dev,REG_VWIN_END_2_MSB, vwin2_line_e);
	reg_write16(adapter, dev,REG_DE_START_MSB, de_pix_s);
	reg_write16(adapter, dev,REG_DE_STOP_MSB, de_pix_e);

	//if (priv->rev == TDA19988) {
	reg_write(adapter, dev,REG_ENABLE_SPACE, 0x01);

	reg_clear(adapter, dev,REG_TBG_CNTRL_1, TBG_CNTRL_1_DWIN_DIS);
	reg_write(adapter, dev,REG_ENC_CNTRL, ENC_CNTRL_CTL_CODE(1));

	/* must be last register set: */
	reg_clear(adapter, dev,REG_TBG_CNTRL_0, TBG_CNTRL_0_SYNC_ONCE);

	return 1;// true
}



//------------------------------------------------------------------------------

int tda998x_drm_to_panel( struct display_panel *panel)
{
	panel->x_res = mode->hdisplay;
	panel->y_res = mode->vdisplay;

	panel->pixel_clock = mode->clock * 1000;
	panel->hbp = mode->htotal - mode->hsync_end;
	panel->hfp = mode->hsync_start - mode->hdisplay;
	panel->hsw = mode->hsync_end - mode->hsync_start;
	panel->vbp = mode->vtotal - mode->vsync_end;
	panel->vfp = mode->vsync_start - mode->vdisplay;
	panel->vsw = mode->vsync_end - mode->vsync_start;

	// subtract one because the hardware uses a value of 0 as 1 for
	// some registers
	if (panel->hbp)
		panel->hbp--;
	if(panel->hfp)
		panel->hfp--;
	if (panel->hsw)
		panel->hsw--;
	if (panel->vsw)
		panel->vsw--;

	panel->config = LCDC_PANEL_TFT | LCDC_HSVS_CONTROL | LCDC_INV_PIX_CLOCK | LCDC_HSVS_FALLING_EDGE;			// config
	// fixup HSYNC by inverting
	if (mode->flags & DRM_MODE_FLAG_PHSYNC)
		panel->config |= LCDC_INV_HSYNC;
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		panel->config |= LCDC_INV_VSYNC;

	return 1;
}


uint32_t PrcmClockGetClockRate(OMAP_CLOCKID clock_id)
{
	uint32_t freq=0;
	uint32_t sys_clk = 24;
	uint32_t val;
	uint32_t val2;
	uint32_t freqSel;

    switch(clock_id)
    {
    	case LCD_PCLK:
        	freqSel = (lcdc_device.prcmregs->CLKSEL_LCDC_PIXEL_CLK&&CLKSEL_LCDC_PIXEL_CLK_MASK)>>CLKSEL_LCDC_PIXEL_CLK_SHIFT;

        	switch (freqSel)
        	{
        		case 0: //disp DPLL M2
        			val = lcdc_device.prcmregs->CM_CLKSEL_DPLL_DISP;
        			val2 = lcdc_device.prcmregs->CM_DIV_M2_DPLL_DISP;

        			// fdpll/2
        			freq = ((uint32_t)((val & DPLL_MULT_MASK)>>DPLL_MULT_SHIFT) * sys_clk) /
        						((uint32_t)((val & DPLL_DIV_MASK) >> DPLL_DIV_SHIFT) + 1);
        			// fdll/(2*M2)
        			freq = freq / ((uint32_t)((val2 & DPLL_ADPLLS_CLKOUT_DIV_MASK) >> DPLL_ADPLLS_CLKOUT_DIV_SHIFT));
        		break;

        		case 1: //CORE DPLL M5->SYSCLK2
        			val = lcdc_device.prcmregs->CM_CLKSEL_DPLL_CORE;
        			val2 = lcdc_device.prcmregs->CM_DIV_M5_DPLL_CORE;
        			//fdpll
        			freq = 2 * ((uint32_t)((val & DPLL_MULT_MASK)>>DPLL_MULT_SHIFT) * sys_clk) /
        						((uint32_t)((val & DPLL_DIV_MASK) >> DPLL_DIV_SHIFT) + 1);
        			//fdpll/(M5)
        			freq = freq / ((uint32_t)((val2 & DPLL_ADPLLS_CLKOUT_DIV_MASK) >> DPLL_ADPLLS_CLKOUT_DIV_SHIFT));
        		break;

        		case 2: //PER DPLL M2 -> 192MHz
        			val = lcdc_device.prcmregs->CM_CLKSEL_DPLL_PERIPH;
        			val2 = lcdc_device.prcmregs->CM_DIV_M2_DPLL_PER;
        			// fdpll
        			freq = ((uint32_t)((val & DPLL_MULT_MASK)>>DPLL_MULT_SHIFT) * sys_clk) /
        						((uint32_t)((val & DPLL_DIV_MASK) >> DPLL_DIV_SHIFT) + 1);
        			//fdpll/M2
        			freq = freq / ((uint32_t)((val2 & DPLL_ADPLLJ_CLKOUT_DIV_MASK) >> DPLL_ADPLLJ_CLKOUT_DIV_SHIFT) + 1);
        		break;

        	}
        break;

        default:
            freq = 0;
        break;

	}
    return (UINT32)freq;
}

//da8xx-fb
void lcd_setup_regs(struct lcdc* lcdc)
{
	UINT32 tmp;		UINT32 lcd_clk, div;  UINT32  width, height;
	int period; int transitions_per_int;
//disable raster
    lcdc->regs->RASTER_CTRL &= ~LCDC_RASTER_CTRL_LCD_EN;
	lcd_clk = lcdc->clk ; //in HZ
	div = lcd_clk / lcdc->panel->pixel_clock;
//clock divisor
	tmp = lcdc->regs->CTRL;
	tmp &= ~(LCDC_CTRL_CLKDIV_MASK);
	tmp |= LCDC_CTRL_CLKDIV(div) | LCDC_CTRL_MODESEL_RASTER;
	lcdc->regs->CTRL = tmp;
	lcdc->regs->CLKC_ENABLE = LCDC_CLKC_ENABLE_DMA | LCDC_CLKC_ENABLE_LIDD |LCDC_CLKC_ENABLE_CORE;
//display configuration
	tmp = lcdc->regs->RASTER_CTRL ;
	tmp &= ~(LCDC_RASTER_CTRL_LCD_TFT | LCDC_RASTER_CTRL_MONO_8BIT_MODE| LCDC_RASTER_CTRL_MONOCHROME_MODE | LCDC_RASTER_CTRL_LCD_TFT_24 |LCDC_RASTER_CTRL_TFT_24BPP_UNPACK | LCDC_RASTER_CTRL_LCD_STN_565 | LCDC_RASTER_CTRL_TFT_ALT_ENABLE);
	if(lcdc->panel->pixel_format == DISPC_PIXELFORMAT_RGB24 ||lcdc->panel->pixel_format == DISPC_PIXELFORMAT_RGB32 ||lcdc->panel->pixel_format == DISPC_PIXELFORMAT_ARGB32 ||lcdc->panel->pixel_format == DISPC_PIXELFORMAT_RGBA32)
	   	tmp |= LCDC_RASTER_CTRL_LCD_TFT_24;
	if(lcdc->panel->pixel_format == DISPC_PIXELFORMAT_RGB32 ||lcdc->panel->pixel_format == DISPC_PIXELFORMAT_ARGB32 ||lcdc->panel->pixel_format == DISPC_PIXELFORMAT_RGBA32)
		tmp |= LCDC_RASTER_CTRL_TFT_24BPP_UNPACK;

	tmp |= LCDC_RASTER_CTRL_LCD_TFT;
	tmp &= ~(LCDC_RASTER_CTRL_REQDLY_MASK);
	lcdc->regs->RASTER_CTRL = tmp;
	tmp = lcdc->regs->RASTER_TIMING_2;
	tmp &= ~(LCDC_SIGNAL_MASK << 20);
	tmp |= ((lcdc->panel->config & LCDC_SIGNAL_MASK) << 20);
	lcdc->regs->RASTER_TIMING_2 = tmp;

//ac bias
	period=0xff; transitions_per_int=0x0;
	tmp = lcdc->regs->RASTER_TIMING_2 & 0xFFF000FF;
	tmp |= LCDC_AC_BIAS_FREQUENCY(period) |LCDC_AC_BIAS_TRANSITIONS_PER_INT(transitions_per_int);
	lcdc->regs->RASTER_TIMING_2 = tmp;

//vertical sync
	tmp = lcdc->regs->RASTER_TIMING_1 & 0x000003FF;
	tmp |= ((lcdc->panel->vbp & 0xff) << 24) |((lcdc->panel->vfp & 0xff) << 16) |((lcdc->panel->vsw & 0x3f) << 10) ;
	lcdc->regs->RASTER_TIMING_1 = tmp;

//horizontal sync
	tmp = lcdc->regs->RASTER_TIMING_0 & 0x000003FF;
	tmp |= ((lcdc->panel->hbp & 0xff) << 24) |((lcdc->panel->hfp & 0xff) << 16) |((lcdc->panel->hsw & 0x3f) << 10);
	lcdc->regs->RASTER_TIMING_0 = tmp;
	tmp = lcdc->regs->RASTER_TIMING_2 & 0x87FFFFFF;
	tmp |= (((lcdc->panel->hsw >>6) & 0xf) << 27);
	lcdc->regs->RASTER_TIMING_2 = tmp;

//frame buffer
	/* Set the Panel Width */
	/* Pixels per line = (PPL + 1)*16
	 * 0x7F in bits 4..10 gives max horizontal resolution = 2048 pixels.
	 */
	tmp = lcdc->regs->RASTER_TIMING_0 & 0xfffffc00;
	width = lcdc->panel->x_res & 0x7f0;
	width = (width >> 4) - 1;
	tmp |= ((width & 0x3f) << 4) | ((width & 0x40) >> 3);
	lcdc->regs->RASTER_TIMING_0 = tmp;
	/* Set the Panel Height */
	/* Set bits 9:0 of Lines Per Pixel */
	tmp = lcdc->regs->RASTER_TIMING_1 & 0xfffffc00;
	height = lcdc->panel->y_res;

	tmp |= ((height - 1) & 0x3ff) ;
	lcdc->regs->RASTER_TIMING_1 = tmp;

	/* Set bit 10 of Lines Per Pixel */
	tmp = lcdc->regs->RASTER_TIMING_2 ;
	tmp |= ((height - 1) & 0x400) << 16;    //bit 26 in timing2 register
	lcdc->regs->RASTER_TIMING_2 = tmp;

	/* Set the Raster Order of the Frame Buffer */
	tmp = lcdc->regs->RASTER_CTRL ;
//	tmp |= (LCDC_RASTER_CTRL_RASTER_ORDER | LCDC_LOAD_FRAME<<20);
	tmp |= LCDC_RASTER_CTRL_PALMODE(LCDC_LOAD_FRAME);
	lcdc->regs->RASTER_CTRL = tmp;

//dma config
	lcdc->dma_fifo_thresh = LCDC_DMA_CTRL_TH_FIFO_READY_512;
	tmp = lcdc->regs->LCDDMA_CTRL & (LCDC_DMA_CTRL_DUAL_FRAME_BUFFER_EN | LCDC_DMA_CTRL_BURST_SIZE_MASK |LCDC_DMA_CTRL_TH_FIFO_READY_MASK);
	tmp |= LCDC_DMA_CTRL_BURST_SIZE(LCDC_DMA_CTRL_BURST_16);
	tmp |= LCDC_DMA_CTRL_TH_FIFO_READY(lcdc->dma_fifo_thresh);
	lcdc->regs->LCDDMA_CTRL = tmp;
}

#ifdef AM335X_HDMI_DEBUG
void lcdc_dumpregs(struct lcdc *lcdc)
{

	fprintf(stderr,"lcdc->regs->PID =               0x%08x\r\n", lcdc->regs->PID);
	fprintf(stderr,"lcdc->regs->CTRL =              0x%08x\r\n", lcdc->regs->CTRL);
	fprintf(stderr,"lcdc->regs->LIDD_CTRL =         0x%08x\r\n", lcdc->regs->LIDD_CTRL);
	fprintf(stderr,"lcdc->regs->RASTER_CTRL     =   0x%08x\r\n", lcdc->regs->RASTER_CTRL);
	fprintf(stderr,"lcdc->regs->RASTER_TIMING_0 =   0x%08x\r\n", lcdc->regs->RASTER_TIMING_0);
	fprintf(stderr,"lcdc->regs->RASTER_TIMING_1 =   0x%08x\r\n", lcdc->regs->RASTER_TIMING_1);
	fprintf(stderr,"lcdc->regs->RASTER_TIMING_2 =   0x%08x\r\n", lcdc->regs->RASTER_TIMING_2);
	fprintf(stderr,"lcdc->regs->RASTER_SUBPANEL =   0x%08x\r\n", lcdc->regs->RASTER_SUBPANEL);
	fprintf(stderr,"lcdc->regs->RASTER_SUBPANEL2 =  0x%08x\r\n", lcdc->regs->RASTER_SUBPANEL2);
	fprintf(stderr,"lcdc->regs->LCDDMA_CTRL =       0x%08x\r\n", lcdc->regs->LCDDMA_CTRL);
	fprintf(stderr,"lcdc->regs->LCDDMA_FB0_BASE =   0x%08x\r\n", lcdc->regs->LCDDMA_FB0_BASE);
	fprintf(stderr,"lcdc->regs->LCDDMA_FB0_CEILING =0x%08x\r\n", lcdc->regs->LCDDMA_FB0_CEILING);
	fprintf(stderr,"lcdc->regs->LCDDMA_FB1_BASE =   0x%08x\r\n", lcdc->regs->LCDDMA_FB1_BASE);
	fprintf(stderr,"lcdc->regs->LCDDMA_FB1_CEILING =0x%08x\r\n", lcdc->regs->LCDDMA_FB1_CEILING);
	fprintf(stderr,"lcdc->regs->SYSCONFIG     =     0x%08x\r\n", lcdc->regs->SYSCONFIG);
	fprintf(stderr,"lcdc->regs->IRQSTATUS_RAW =     0x%08x\r\n", lcdc->regs->IRQSTATUS_RAW);
	fprintf(stderr,"lcdc->regs->IRQSTATUS     =     0x%08x\r\n", lcdc->regs->IRQSTATUS);
	fprintf(stderr,"lcdc->regs->IRQENABLE_SET =     0x%08x\r\n", lcdc->regs->IRQENABLE_SET);
	fprintf(stderr,"lcdc->regs->IRQENABLE_CLEAR =   0x%08x\r\n", lcdc->regs->IRQENABLE_CLEAR);
	fprintf(stderr,"lcdc->regs->CLKC_ENABLE =       0x%08x\r\n", lcdc->regs->CLKC_ENABLE);
	fprintf(stderr,"lcdc->regs->CLKC_RESET =        0x%08x\r\n", lcdc->regs->CLKC_RESET);
}

void dump_hdmi_reg(disp_adapter_t *adapter, am335x_hdmi_t *dev)
{

	fprintf(stderr,"HDMI REGISTER LIST\r\n");
	fprintf(stderr,"REG_VERSION_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VERSION_LSB));
	fprintf(stderr,"REG_VERSION_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VERSION_MSB));
	fprintf(stderr,"REG_MAIN_CNTRL0: 0x%x\r\n", reg_read(adapter, dev,REG_MAIN_CNTRL0));
	fprintf(stderr,"REG_SOFTRESET: 0x%x\r\n", reg_read(adapter, dev,REG_SOFTRESET));
	fprintf(stderr,"REG_DDC_DISABLE: 0x%x\r\n", reg_read(adapter, dev,REG_DDC_DISABLE));
	fprintf(stderr,"REG_CCLK_ON: 0x%x\r\n", reg_read(adapter, dev,REG_CCLK_ON));
	fprintf(stderr,"REG_I2C_MASTER: 0x%x\r\n", reg_read(adapter, dev,REG_I2C_MASTER));
	fprintf(stderr,"REG_FEAT_POWERDOWN: 0x%x\r\n", reg_read(adapter, dev,REG_FEAT_POWERDOWN));
	fprintf(stderr,"REG_INT_FLAGS_0: 0x%x\r\n", reg_read(adapter, dev,REG_INT_FLAGS_0));
	fprintf(stderr,"REG_INT_FLAGS_1: 0x%x\r\n", reg_read(adapter, dev,REG_INT_FLAGS_1));
	fprintf(stderr,"REG_INT_FLAGS_2: 0x%x\r\n", reg_read(adapter, dev,REG_INT_FLAGS_2));
	fprintf(stderr,"REG_ENA_ACLK: 0x%x\r\n", reg_read(adapter, dev,REG_ENA_ACLK));
	fprintf(stderr,"REG_ENA_VP_0: 0x%x\r\n", reg_read(adapter, dev,REG_ENA_VP_0));
	fprintf(stderr,"REG_ENA_VP_1: 0x%x\r\n", reg_read(adapter, dev,REG_ENA_VP_1));
	fprintf(stderr,"REG_ENA_VP_2: 0x%x\r\n", reg_read(adapter, dev,REG_ENA_VP_2));
	fprintf(stderr,"REG_ENA_AP: 0x%x\r\n", reg_read(adapter, dev,REG_ENA_AP));
	fprintf(stderr,"REG_VIP_CNTRL_0: 0x%x\r\n", reg_read(adapter, dev,REG_VIP_CNTRL_0));
	fprintf(stderr,"REG_VIP_CNTRL_1: 0x%x\r\n", reg_read(adapter, dev,REG_VIP_CNTRL_1));
	fprintf(stderr,"REG_VIP_CNTRL_2: 0x%x\r\n", reg_read(adapter, dev,REG_VIP_CNTRL_2));
	fprintf(stderr,"REG_VIP_CNTRL_3: 0x%x\r\n", reg_read(adapter, dev,REG_VIP_CNTRL_3));
	fprintf(stderr,"REG_VIP_CNTRL_4: 0x%x\r\n", reg_read(adapter, dev,REG_VIP_CNTRL_4));
	fprintf(stderr,"REG_VIP_CNTRL_5: 0x%x\r\n", reg_read(adapter, dev,REG_VIP_CNTRL_5));
	fprintf(stderr,"REG_MUX_AP: 0x%x\r\n", reg_read(adapter, dev,REG_MUX_AP));
	fprintf(stderr,"REG_MUX_VP_VIP_OUT: 0x%x\r\n", reg_read(adapter, dev,REG_MUX_VP_VIP_OUT));
	fprintf(stderr,"REG_MAT_CONTRL: 0x%x\r\n", reg_read(adapter, dev,REG_MAT_CONTRL));
	fprintf(stderr,"REG_VIDFORMAT: 0x%x\r\n", reg_read(adapter, dev,REG_VIDFORMAT));
	fprintf(stderr,"REG_REFPIX_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_REFPIX_MSB));
	fprintf(stderr,"REG_REFPIX_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_REFPIX_LSB));
	fprintf(stderr,"REG_REFLINE_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_REFLINE_MSB));
	fprintf(stderr,"REG_REFLINE_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_REFLINE_LSB));
	fprintf(stderr,"REG_NPIX_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_NPIX_MSB));
	fprintf(stderr,"REG_NPIX_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_NPIX_LSB));
	fprintf(stderr,"REG_NLINE_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_NLINE_MSB));
	fprintf(stderr,"REG_NLINE_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_NLINE_LSB));
	fprintf(stderr,"REG_VS_LINE_STRT_1_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_STRT_1_MSB));
	fprintf(stderr,"REG_VS_LINE_STRT_1_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_STRT_1_LSB));
	fprintf(stderr,"REG_VS_PIX_STRT_1_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_STRT_1_MSB));
	fprintf(stderr,"REG_VS_PIX_STRT_1_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_STRT_1_LSB));
	fprintf(stderr,"REG_VS_LINE_END_1_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_END_1_MSB));
	fprintf(stderr,"REG_VS_LINE_END_1_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_END_1_LSB));
	fprintf(stderr,"REG_VS_PIX_END_1_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_END_1_MSB));
	fprintf(stderr,"REG_VS_PIX_END_1_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_END_1_LSB));
	fprintf(stderr,"REG_VS_LINE_STRT_2_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_STRT_2_MSB));
	fprintf(stderr,"REG_VS_LINE_STRT_2_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_STRT_2_LSB));
	fprintf(stderr,"REG_VS_PIX_STRT_2_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_STRT_2_MSB));
	fprintf(stderr,"REG_VS_PIX_STRT_2_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_STRT_2_LSB));
	fprintf(stderr,"REG_VS_LINE_END_2_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_END_2_MSB));
	fprintf(stderr,"REG_VS_LINE_END_2_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_LINE_END_2_LSB));
	fprintf(stderr,"REG_VS_PIX_END_2_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_END_2_MSB));
	fprintf(stderr,"REG_VS_PIX_END_2_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VS_PIX_END_2_LSB));
	fprintf(stderr,"REG_HS_PIX_START_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_HS_PIX_START_MSB));
	fprintf(stderr,"REG_HS_PIX_START_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_HS_PIX_START_LSB));
	fprintf(stderr,"REG_HS_PIX_STOP_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_HS_PIX_STOP_MSB));
	fprintf(stderr,"REG_HS_PIX_STOP_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_HS_PIX_STOP_LSB));
	fprintf(stderr,"REG_VWIN_START_1_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_START_1_MSB));
	fprintf(stderr,"REG_VWIN_START_1_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_START_1_LSB));
	fprintf(stderr,"REG_VWIN_END_1_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_END_1_MSB));
	fprintf(stderr,"REG_VWIN_END_1_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_END_1_LSB));
	fprintf(stderr,"REG_VWIN_START_2_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_START_2_MSB));
	fprintf(stderr,"REG_VWIN_START_2_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_START_2_LSB));
	fprintf(stderr,"REG_VWIN_END_2_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_END_2_MSB));
	fprintf(stderr,"REG_VWIN_END_2_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_VWIN_END_2_LSB));
	fprintf(stderr,"REG_DE_START_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_DE_START_MSB));
	fprintf(stderr,"REG_DE_START_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_DE_START_LSB));
	fprintf(stderr,"REG_DE_STOP_MSB: 0x%x\r\n", reg_read(adapter, dev,REG_DE_STOP_MSB));
	fprintf(stderr,"REG_DE_STOP_LSB: 0x%x\r\n", reg_read(adapter, dev,REG_DE_STOP_LSB));
	fprintf(stderr,"REG_TBG_CNTRL_0: 0x%x\r\n", reg_read(adapter, dev,REG_TBG_CNTRL_0));
	fprintf(stderr,"REG_TBG_CNTRL_1: 0x%x\r\n", reg_read(adapter, dev,REG_TBG_CNTRL_1));
	fprintf(stderr,"REG_ENABLE_SPACE: 0x%x\r\n", reg_read(adapter, dev,REG_ENABLE_SPACE));
	fprintf(stderr,"REG_HVF_CNTRL_0: 0x%x\r\n", reg_read(adapter, dev,REG_HVF_CNTRL_0));
	fprintf(stderr,"REG_HVF_CNTRL_1: 0x%x\r\n", reg_read(adapter, dev,REG_HVF_CNTRL_1));
	fprintf(stderr,"REG_RPT_CNTRL: 0x%x\r\n", reg_read(adapter, dev,REG_RPT_CNTRL));
	fprintf(stderr,"REG_I2S_FORMAT: 0x%x\r\n", reg_read(adapter, dev,REG_I2S_FORMAT));
	fprintf(stderr,"REG_AIP_CLKSEL: 0x%x\r\n", reg_read(adapter, dev,REG_AIP_CLKSEL));
	fprintf(stderr,"REG_PLL_SERIAL_1: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SERIAL_1));
	fprintf(stderr,"REG_PLL_SERIAL_2: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SERIAL_2));
	fprintf(stderr,"REG_PLL_SERIAL_3: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SERIAL_3));
	fprintf(stderr,"REG_SERIALIZER: 0x%x\r\n", reg_read(adapter, dev,REG_SERIALIZER));
	fprintf(stderr,"REG_BUFFER_OUT: 0x%x\r\n", reg_read(adapter, dev,REG_BUFFER_OUT));
	fprintf(stderr,"REG_PLL_SCG1: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SCG1));
	fprintf(stderr,"REG_PLL_SCG2: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SCG2));
	fprintf(stderr,"REG_PLL_SCGN1: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SCGN1));
	fprintf(stderr,"REG_PLL_SCGN2: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SCGN2));
	fprintf(stderr,"REG_PLL_SCGR1: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SCGR1));
	fprintf(stderr,"REG_PLL_SCGR2: 0x%x\r\n", reg_read(adapter, dev,REG_PLL_SCGR2));
	fprintf(stderr,"REG_AUDIO_DIV: 0x%x\r\n", reg_read(adapter, dev,REG_AUDIO_DIV));
	fprintf(stderr,"REG_SEL_CLK: 0x%x\r\n", reg_read(adapter, dev,REG_SEL_CLK));
	fprintf(stderr,"REG_ANA_GENERAL: 0x%x\r\n", reg_read(adapter, dev,REG_ANA_GENERAL));
	fprintf(stderr,"REG_EDID_DATA_0: 0x%x\r\n", reg_read(adapter, dev,REG_EDID_DATA_0));
	fprintf(stderr,"REG_EDID_CTRL: 0x%x\r\n", reg_read(adapter, dev,REG_EDID_CTRL));
	fprintf(stderr,"REG_DDC_ADDR: 0x%x\r\n", reg_read(adapter, dev,REG_DDC_ADDR));
	fprintf(stderr,"REG_DDC_OFFS: 0x%x\r\n", reg_read(adapter, dev,REG_DDC_OFFS));
	fprintf(stderr,"REG_DDC_SEGM_ADDR: 0x%x\r\n", reg_read(adapter, dev,REG_DDC_SEGM_ADDR));
	fprintf(stderr,"REG_DDC_SEGM: 0x%x\r\n", reg_read(adapter, dev,REG_DDC_SEGM));
	fprintf(stderr,"REG_IF1_HB0: 0x%x\r\n", reg_read(adapter, dev,REG_IF1_HB0));
	fprintf(stderr,"REG_IF2_HB0: 0x%x\r\n", reg_read(adapter, dev,REG_IF2_HB0));
	fprintf(stderr,"REG_IF3_HB0: 0x%x\r\n", reg_read(adapter, dev,REG_IF3_HB0));
	fprintf(stderr,"REG_IF4_HB0: 0x%x\r\n", reg_read(adapter, dev,REG_IF4_HB0));
	fprintf(stderr,"REG_IF5_HB0: 0x%x\r\n", reg_read(adapter, dev,REG_IF5_HB0));
	fprintf(stderr,"REG_AIP_CNTRL_0: 0x%x\r\n", reg_read(adapter, dev,REG_AIP_CNTRL_0));
	fprintf(stderr,"REG_CA_I2S: 0x%x\r\n", reg_read(adapter, dev,REG_CA_I2S));
	fprintf(stderr,"REG_LATENCY_RD: 0x%x\r\n", reg_read(adapter, dev,REG_LATENCY_RD));
	fprintf(stderr,"REG_ACR_CTS_0: 0x%x\r\n", reg_read(adapter, dev,REG_ACR_CTS_0));
	fprintf(stderr,"REG_ACR_CTS_1: 0x%x\r\n", reg_read(adapter, dev,REG_ACR_CTS_1));
	fprintf(stderr,"REG_ACR_CTS_2: 0x%x\r\n", reg_read(adapter, dev,REG_ACR_CTS_2));
	fprintf(stderr,"REG_ACR_N_0: 0x%x\r\n", reg_read(adapter, dev,REG_ACR_N_0));
	fprintf(stderr,"REG_ACR_N_1: 0x%x\r\n", reg_read(adapter, dev,REG_ACR_N_1));
	fprintf(stderr,"REG_ACR_N_2: 0x%x\r\n", reg_read(adapter, dev,REG_ACR_N_2));
	fprintf(stderr,"REG_GC_AVMUTE: 0x%x\r\n", reg_read(adapter, dev,REG_GC_AVMUTE));
	fprintf(stderr,"REG_CTS_N: 0x%x\r\n", reg_read(adapter, dev,REG_CTS_N));
	fprintf(stderr,"REG_ENC_CNTRL: 0x%x\r\n", reg_read(adapter, dev,REG_ENC_CNTRL));
	fprintf(stderr,"REG_DIP_FLAGS: 0x%x\r\n", reg_read(adapter, dev,REG_DIP_FLAGS));
	fprintf(stderr,"REG_DIP_IF_FLAGS: 0x%x\r\n", reg_read(adapter, dev,REG_DIP_IF_FLAGS));
	fprintf(stderr,"REG_TX3: 0x%x\r\n", reg_read(adapter, dev,REG_TX3));
	fprintf(stderr,"REG_TX4: 0x%x\r\n", reg_read(adapter, dev,REG_TX4));
	fprintf(stderr,"REG_TX33: 0x%x\r\n", reg_read(adapter, dev,REG_TX33));

	fprintf(stderr,"REG_CEC_INTSTATUS: 0x%x\r\n",tda19988_cec_read (adapter, dev, REG_CEC_INTSTATUS));
	fprintf(stderr,"REG_CEC_FRO_IM_CLK_CTRL: 0x%x\r\n",tda19988_cec_read (adapter, dev, REG_CEC_FRO_IM_CLK_CTRL));
	fprintf(stderr,"REG_CEC_RXSHPDINTENA: 0x%x\r\n",tda19988_cec_read (adapter, dev, REG_CEC_RXSHPDINTENA));
	fprintf(stderr,"REG_CEC_RXSHPDINT: 0x%x\r\n",tda19988_cec_read (adapter, dev, REG_CEC_RXSHPDINT));
	fprintf(stderr,"REG_CEC_RXSHPDLEV: 0x%x\r\n",tda19988_cec_read (adapter, dev, REG_CEC_RXSHPDLEV));
	fprintf(stderr,"REG_CEC_ENAMODS: 0x%x\r\n",tda19988_cec_read (adapter, dev, REG_CEC_ENAMODS));
}
#endif

int init_hdmi(disp_adapter_t *adapter, omapl1xx_context_t *omapl1xx, struct lcdc *lcdc)
{
	am335x_hdmi_t *dev;
	UINT16 rev;
	 uint8_t        	val;
	 unsigned int  dwLength;
	int			 i;
	int			 ret = -1;

	dev = (am335x_hdmi_t *)malloc(sizeof(am335x_hdmi_t));
	if (!dev)
	{
		slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "HDMI: out of memory trying to allocate %d bytes",sizeof(am335x_hdmi_t));
		return -1;
	}
	omapl1xx->hdmi_dev  = dev;
	dev->cable_state = HDMI_CABLE_NOTCONNECTED;
	dev->hdmi_cap    = 0;
	lcdc->clk = PrcmClockGetClockRate(LCD_PCLK);
	slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "lcdc->clk  %d",lcdc->clk);
/*
 * Change the panel size here
 */
	//tda998x_drm_to_panel(&drm_800x600, &panel_initx);
	mode=&drm_800x480;
	//mode=&drm_1024x768;
	tda998x_drm_to_panel( &panel_initx);

	lcdc->panel = &panel_initx;
//tda1998x_init	starts
	strncpy(dev->i2c_dev, "/dev/i2c0", MAX_STRLEN);
	dev->i2c_fd = open (dev->i2c_dev, O_RDWR);
	if (dev->i2c_fd == -1)
	{
		slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "HDMI: Failed to open I2C device: %s",dev->i2c_dev);
		free(dev);
		return -1;
	}
    val = CEC_ENAMODS_EN_RXSENS | CEC_ENAMODS_EN_HDMI;
	tda19988_cec_write(adapter,dev,  REG_CEC_ENAMODS,val);
	tda998x_reset(adapter, dev);
 //version
	rev = reg_read(adapter, dev,REG_VERSION_LSB) |
	reg_read(adapter, dev,REG_VERSION_MSB) << 8;
		// mask off feature bits
	rev &= ~0x30; /* not-hdcp and not-scalar bit */
	switch (rev)
	{
		case TDA9989N2:
		              slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "found TDA9989 n2 ");
		case TDA19989:
		              slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "found TDA19989 ");
		case TDA19989N2:
		              slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "found TDA19989 n2 ");
		case TDA19988:
		              slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "found TDA19988 ");
		default:
			          slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "found unsupported device: %04x");
	}
	/* after reset, enable DDC: */
	reg_write(adapter, dev,REG_DDC_DISABLE, 0x00);
	/* set clock on DDC channel: */
	reg_write(adapter, dev,REG_TX3, 39);
	/* if necessary, disable multi-master: */
	if (rev == TDA19989)
		reg_set(adapter, dev,REG_I2C_MASTER, I2C_MASTER_DIS_MM);
	val = CEC_FRO_IM_CLK_CTRL_GHOST_DIS | CEC_FRO_IM_CLK_CTRL_IMCLK_SEL;
	tda19988_cec_write(adapter, dev,REG_CEC_FRO_IM_CLK_CTRL, val);
//tda1998x_init	end
//EID Interrupt routine not implemented
	if( tda998x_connected_detect(adapter,dev))
	    slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "HDMI Connected");
	else
		slogf(_SLOGC_GRAPHICS, _SLOG_INFO, "HDMI Disconnected");
	tda998x_encoder_dpms(adapter,dev);
  //  tda998x_encoder_mode_set(adapter,dev,&drm_800x600x);
	tda998x_encoder_mode_set(adapter,dev);


	lcdc->fb_pa =   IMAGE_DISPLAY_PA;
    lcdc->fb_size = IMAGE_DISPLAY_SIZE;
	lcdc->palette_phys = IMAGE_DISPLAY_PALETTE_PA;

    // Compute the size
    dwLength = 2 * lcdc->panel->x_res *  lcdc->panel->y_res;  // DISPC_PIXELFORMAT_RGB16=2

 //reset clock starts
    	lcdc->regs->RASTER_CTRL  = 0;
    	/* take module out of reset */
    	lcdc->regs->CLKC_RESET = 0xC;
    	delay(100);
    	lcdc->regs->CLKC_RESET = 0x0;

        lcdc->regs->LCDDMA_CTRL = 0;
        lcdc->regs->LCDDMA_FB0_BASE = lcdc->fb_pa;
        lcdc->regs->LCDDMA_FB0_CEILING = lcdc->fb_pa + dwLength -1;
 // disable interrupt
        lcdc->regs->IRQENABLE_CLEAR = 0x3FF;
        lcdc->clk *= 1000000;  /* change from MHz to Hz */
     //   lcdc->dma_burst_sz = 16;
       lcd_setup_regs(lcdc); //stripped version
#ifdef AM335X_HDMI_DEBUG
        lcdc_dumpregs(lcdc);
        dump_hdmi_reg(adapter, dev);
#endif
		return 0;
}





