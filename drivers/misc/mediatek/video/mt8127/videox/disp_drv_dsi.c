#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/semaphore.h>
#include "disp_drv_log.h"
#include <mt-plat/mt_boot.h>
#include "disp_drv_platform.h"
#include "lcd_drv.h"
#include "dsi_drv.h"
#include "dpi_drv.h"

#include "lcm_drv.h"
#include "disp_hal.h"
#include "disp_debug.h"

/* ------------------------------------------------------------------------- */
/* Private Variables */
/* ------------------------------------------------------------------------- */
static uint32_t dsiTmpBufBpp = 3;

struct TempBuffer {
	uint32_t pa;
	uint32_t pitchInBytes;
};

#define TempBuffer	struct TempBuffer

#ifndef MT65XX_NEW_DISP
static TempBuffer s_tmpBuffers[3];
#endif
static bool needStartDSI;

/* ------------------------------------------------------------------------- */
/* Private Functions */
/* ------------------------------------------------------------------------- */

static void init_lcd_te_control(void)
{
	const LCM_DBI_PARAMS *dbi = &(lcm_params->dbi);

	LCD_CHECK_RET(LCD_TE_Enable(false));
	if (LCM_DBI_TE_MODE_DISABLED == dbi->te_mode) {
		LCD_CHECK_RET(LCD_TE_Enable(false));
		return;
	}

	if (LCM_DBI_TE_MODE_VSYNC_ONLY == dbi->te_mode) {
		LCD_CHECK_RET(LCD_TE_SetMode(LCD_TE_MODE_VSYNC_ONLY));
	} else if (LCM_DBI_TE_MODE_VSYNC_OR_HSYNC == dbi->te_mode) {
		LCD_CHECK_RET(LCD_TE_SetMode(LCD_TE_MODE_VSYNC_OR_HSYNC));
		LCD_CHECK_RET(LCD_TE_ConfigVHSyncMode(dbi->te_hs_delay_cnt,
						      dbi->te_vs_width_cnt,
						      (LCD_TE_VS_WIDTH_CNT_DIV) dbi->
						      te_vs_width_cnt_div));
	} else
		ASSERT(0);

	LCD_CHECK_RET(LCD_TE_SetEdgePolarity(dbi->te_edge_polarity));
	LCD_CHECK_RET(LCD_TE_Enable(true));
}

static inline LCD_IF_WIDTH to_lcd_if_width(LCM_DBI_DATA_WIDTH data_width)
{
	switch (data_width) {
	case LCM_DBI_DATA_WIDTH_8BITS:
		return LCD_IF_WIDTH_8_BITS;
	case LCM_DBI_DATA_WIDTH_9BITS:
		return LCD_IF_WIDTH_9_BITS;
	case LCM_DBI_DATA_WIDTH_16BITS:
		return LCD_IF_WIDTH_16_BITS;
	case LCM_DBI_DATA_WIDTH_18BITS:
		return LCD_IF_WIDTH_18_BITS;
	case LCM_DBI_DATA_WIDTH_24BITS:
		return LCD_IF_WIDTH_24_BITS;
	default:
		ASSERT(0);
	}
	return LCD_IF_WIDTH_18_BITS;
}

static bool disp_drv_dsi_init_context(void)
{
	if (lcm_drv != NULL && lcm_params != NULL)
		return true;


	if (NULL == lcm_drv)
		return false;


	lcm_drv->get_params(lcm_params);


	return true;
}

void init_dsi(bool isDsiPoweredOn)
{
	/*DSI_PHY_clk_setting(lcm_params);*/
	DSI_CHECK_RET(DSI_Init(isDsiPoweredOn));

	if (0 < lcm_params->dsi.compatibility_for_nvk) {
		DSI_CHECK_RET(DSI_TXRX_Control(true,	/* cksm_en */
					       true,	/* ecc_en */
					       lcm_params->dsi.LANE_NUM,	/* ecc_en */
					       0,	/* vc_num */
					       false,	/* null_packet_en */
					       false,	/* err_correction_en */
					       false,	/* dis_eotp_en */
					       false, 0));	/* max_return_size */
	} else {
		DSI_CHECK_RET(DSI_TXRX_Control(true,	/* cksm_en */
					       true,	/* ecc_en */
					       lcm_params->dsi.LANE_NUM,	/* ecc_en */
					       0,	/* vc_num */
					       false,	/* null_packet_en */
					       false,	/* err_correction_en */
					       false,	/* dis_eotp_en */
					       (bool) (1 - lcm_params->dsi.cont_clock), 0));
	}

	/* initialize DSI_PHY */
	DSI_PHY_clk_switch(true);
	DSI_PHY_TIMCONFIG(lcm_params);

	DSI_CHECK_RET(DSI_PS_Control(lcm_params->dsi.PS, lcm_params->height,
				     lcm_params->width * dsiTmpBufBpp));

	if (lcm_params->dsi.mode != CMD_MODE) {
		DSI_Config_VDO_Timing(lcm_params);
		DSI_Set_VM_CMD(lcm_params);
	}

	DSI_CHECK_RET(DSI_enable_MIPI_txio(true));
	DSI_EnableClk();

}

#if !defined(CONFIG_MTK_OVERLAY_ENGINE_SUPPORT)
/* ------------------------------------------------------------ */
/* DBI Display Driver Public Functions */
/* ------------------------------------------------------------ */
static DISP_STATUS dsi_config_ddp(uint32_t fbPA)
{
	struct disp_path_config_struct config;

	memset(&config, 0, sizeof(struct disp_path_config_struct));
	config.srcModule = DISP_MODULE_OVL;
	config.bgROI.x = 0;
	config.bgROI.y = 0;
	config.bgROI.width = lcm_params->width;
	config.bgROI.height = lcm_params->height;
	config.bgColor = 0x0;
	config.pitch = lcm_params->width * 2;
	config.srcWidth = lcm_params->width;
	config.srcHeight = lcm_params->height;
	config.srcROI.x = 0;
	config.srcROI.y = 0;
	config.srcROI.height = lcm_params->height;
	config.srcROI.width = lcm_params->width;
	config.ovl_config.source = OVL_LAYER_SOURCE_MEM;
	config.addr = fbPA;
	config.outFormat = RDMA_OUTPUT_FORMAT_ARGB;

	if (lcm_params->dsi.mode != CMD_MODE) {
		config.ovl_config.layer_en = 0;
		config.ovl_config.layer = DDP_OVL_LAYER_MUN - 1;
		disp_path_config_layer(&config.ovl_config);
		config.ovl_config.layer = DDP_OVL_LAYER_MUN - 2;
		disp_path_config_layer(&config.ovl_config);
	}

	config.ovl_config.layer = DDP_OVL_LAYER_MUN - 1;
	config.ovl_config.layer_en = 1;
	config.ovl_config.fmt = eRGB565;
	config.ovl_config.addr = fbPA;
	config.ovl_config.source = OVL_LAYER_SOURCE_MEM;
	config.ovl_config.src_x = 0;
	config.ovl_config.src_y = 0;
	config.ovl_config.dst_x = 0;
	config.ovl_config.dst_y = 0;
	config.ovl_config.dst_w = lcm_params->width;
	config.ovl_config.dst_h = lcm_params->height;
	config.ovl_config.src_pitch = ALIGN_TO(lcm_params->width, MTK_FB_ALIGNMENT) * 2;
	config.ovl_config.keyEn = 0;
	config.ovl_config.key = 0xFF;
	config.ovl_config.aen = 0;
	config.ovl_config.alpha = 0;

	LCD_LayerSetAddress(DDP_OVL_LAYER_MUN - 1, fbPA);
	LCD_LayerSetFormat(DDP_OVL_LAYER_MUN - 1, LCD_LAYER_FORMAT_RGB565);
	LCD_LayerSetOffset(DDP_OVL_LAYER_MUN - 1, 0, 0);
	LCD_LayerSetSize(DDP_OVL_LAYER_MUN - 1, lcm_params->width, lcm_params->height);
	LCD_LayerSetPitch(DDP_OVL_LAYER_MUN - 1, ALIGN_TO(lcm_params->width, MTK_FB_ALIGNMENT) * 2);
	LCD_LayerEnable(DDP_OVL_LAYER_MUN - 1, true);

	if (lcm_params->dsi.mode == CMD_MODE)
		config.dstModule = DISP_MODULE_DSI_CMD;
	else
		config.dstModule = DISP_MODULE_DSI_VDO;

	disp_path_config(&config);

	if (lcm_params->dsi.mode != CMD_MODE) {
		/* DSI_Wait_VDO_Idle(); */
		disp_path_get_mutex();
	}

	if (lcm_params->dsi.mode != CMD_MODE) {
		disp_path_release_mutex();
		/* if(1 == lcm_params->dsi.ufoe_enable) */
		/* UFOE_Start(); */
		/* DSI_Start(); */
	}
	DISP_MSG("%s, config done\n", __func__);
	return DISP_STATUS_OK;
}
#endif

bool DDMS_capturing = 0;
static DISP_STATUS dsi_enable_power(bool enable);

static DISP_STATUS dsi_init(uint32_t fbVA, uint32_t fbPA, bool isLcmInited)
{
	if (!disp_drv_dsi_init_context())
		return DISP_STATUS_NOT_IMPLEMENTED;

	if (lcm_params->dsi.mode == CMD_MODE) {
		init_dsi(isLcmInited);
		MASKREG32(DDP_REG_BASE_DSI + 0x10, 0x2, 0x2);
		if (NULL != lcm_drv->init && !isLcmInited) {
			lcm_drv->init();
			DSI_LP_Reset();
		}

		DSI_clk_HS_mode(1);
		DSI_SetMode(lcm_params->dsi.mode);
	} else {
		init_dsi(isLcmInited);
		if (NULL != lcm_drv->init && !isLcmInited)
			lcm_drv->init();

		DSI_SetMode(lcm_params->dsi.mode);

		if (isLcmInited)
			is_video_mode_running = true;
	}

	RDMASetTargetLine(0, lcm_params->height * 4 / 5);
#if !defined(CONFIG_MTK_OVERLAY_ENGINE_SUPPORT)
	dsi_config_ddp(fbPA);
#endif

	DPI_PowerOn();
	DPI_PowerOff();
#ifdef SPM_SODI_ENABLED
	if (lcm_params->dsi.mode == CMD_MODE)
		spm_sodi_lcm_video_mode(false);
	else
		spm_sodi_lcm_video_mode(true);
#endif
	return DISP_STATUS_OK;
}


/* protected by sem_early_suspend, sem_update_screen */
static DISP_STATUS dsi_enable_power(bool enable)
{
	disp_drv_dsi_init_context();

	if (lcm_params->dsi.mode == CMD_MODE) {
		if (enable) {
			/* enable MMSYS CG */
			DSI_CHECK_RET(DSI_PowerOn());

			/* initialize clock setting */
			DSI_PHY_clk_setting(lcm_params);

			/* restore dsi register */
			DSI_CHECK_RET(DSI_RestoreRegisters());

			/* enable sleep-out mode */
			DSI_CHECK_RET(DSI_SleepOut());

			/* enter HS mode */
			DSI_PHY_clk_switch(1);

			/* enter wakeup */
			DSI_CHECK_RET(DSI_Wakeup());

			/* enable clock */
			DSI_CHECK_RET(DSI_EnableClk());

			DSI_CHECK_RET(DSI_enable_MIPI_txio(true));
			DSI_Reset();
		} else {
			/* backup dsi register */
			DSI_CHECK_RET(DSI_WaitForNotBusy());
			DSI_CHECK_RET(DSI_BackupRegisters());

			/* enter ULPS mode */
			DSI_clk_ULP_mode(1);
			DSI_lane0_ULP_mode(1);
			DSI_clk_HS_mode(0);
			/* disable clock */
			DSI_CHECK_RET(DSI_DisableClk());
			DSI_CHECK_RET(DSI_PowerOff());

			/* disable mipi pll */
			DSI_PHY_clk_switch(0);

			/* Switch bus to GPIO, then power level will be decided
			   by GPIO setting. */
			DSI_CHECK_RET(DSI_enable_MIPI_txio(false));
		}
	} else {
		if (enable) {
			/* enable MMSYS CG */
			DSI_CHECK_RET(DSI_PowerOn());

			/* initialize clock setting */
			DSI_PHY_clk_setting(lcm_params);

			/* restore dsi register */
			DSI_CHECK_RET(DSI_RestoreRegisters());

			/* enable sleep-out mode */
			DSI_CHECK_RET(DSI_SleepOut());

			/* enter HS mode */
			DSI_PHY_clk_switch(1);

			/* enter wakeup */
			DSI_CHECK_RET(DSI_Wakeup());
			DSI_clk_HS_mode(0);
			/* enable clock */
			DSI_CHECK_RET(DSI_EnableClk());

			DSI_CHECK_RET(DSI_enable_MIPI_txio(true));
			DSI_Reset();
			needStartDSI = true;
		} else {
			is_video_mode_running = false;

			/* backup dsi register */
			DSI_CHECK_RET(DSI_WaitForNotBusy());
			DSI_CHECK_RET(DSI_BackupRegisters());

			/* enter ULPS mode */
			DSI_clk_ULP_mode(1);
			DSI_lane0_ULP_mode(1);

			/* disable clock */
			DSI_CHECK_RET(DSI_DisableClk());
			DSI_CHECK_RET(DSI_PowerOff());

			/* disable mipi pll */
			DSI_PHY_clk_switch(0);

			/* Switch bus to GPIO, then power level will be decided
			   by GPIO setting. */
			DSI_CHECK_RET(DSI_enable_MIPI_txio(false));
		}
	}

	return DISP_STATUS_OK;
}


/* protected by sem_flipping, sem_early_suspend,
	sem_overlay_buffer, sem_update_screen */
static DISP_STATUS dsi_update_screen(bool isMuextLocked)
{
	disp_drv_dsi_init_context();

	DSI_CHECK_RET(DSI_enable_MIPI_txio(true));

	/* DSI_CHECK_RET(DSI_handle_TE()); */

	DSI_SetMode(lcm_params->dsi.mode);
#ifndef MT65XX_NEW_DISP
	LCD_CHECK_RET(LCD_StartTransfer(false));
#endif
	if (lcm_params->type == LCM_TYPE_DSI && lcm_params->dsi.mode == CMD_MODE && !DDMS_capturing) {
#ifdef MT65XX_NEW_DISP
		DSI_CHECK_RET(DSI_StartTransfer(isMuextLocked));
#else
		DSI_CHECK_RET(DSI_Start());
#endif
	} else if (lcm_params->type == LCM_TYPE_DSI &&
		   lcm_params->dsi.mode != CMD_MODE && !DDMS_capturing) {
		DSI_clk_HS_mode(1);
		DSI_CHECK_RET(DSI_StartTransfer(isMuextLocked));
#ifndef BUILD_UBOOT
		is_video_mode_running = true;
#ifndef MT65XX_NEW_DISP
		if (lcm_params->dsi.noncont_clock)
			DSI_set_noncont_clk(true, lcm_params->dsi.noncont_clock_period);

		if (lcm_params->dsi.lcm_int_te_monitor)
			DSI_set_int_TE(true, lcm_params->dsi.lcm_int_te_period);
#endif
#endif
	}

	if (DDMS_capturing)
		DISP_LOG_PRINT(ANDROID_LOG_INFO, "DSI",
			       "[DISP] dsi_update_screen. DDMS is capturing. Skip one frame.\n");

	return DISP_STATUS_OK;
}


static uint32_t dsi_get_working_buffer_size(void)
{
	disp_drv_dsi_init_context();

	if (lcm_params->dsi.mode != CMD_MODE) {

		return lcm_params->width * lcm_params->height *
		    dsiTmpBufBpp * lcm_params->dsi.intermediat_buffer_num;
	}

	return 0;
}

static uint32_t dsi_get_working_buffer_bpp(void)
{
	disp_drv_dsi_init_context();

	if (lcm_params->dsi.mode != CMD_MODE)
		return dsiTmpBufBpp;


	return 0;
}

static PANEL_COLOR_FORMAT dsi_get_panel_color_format(void)
{
	disp_drv_dsi_init_context();

	{

		switch (lcm_params->dsi.data_format.format) {
		case LCM_DSI_FORMAT_RGB565:
			return PANEL_COLOR_FORMAT_RGB565;
		case LCM_DSI_FORMAT_RGB666:
			return PANEL_COLOR_FORMAT_RGB666;
		case LCM_DSI_FORMAT_RGB888:
			return PANEL_COLOR_FORMAT_RGB888;
		default:
			ASSERT(0);
		}

	}
}

static uint32_t dsi_get_dithering_bpp(void)
{
	return PANEL_COLOR_FORMAT_TO_BPP(dsi_get_panel_color_format());
}


/* protected by sem_early_suspend */
DISP_STATUS dsi_capture_framebuffer(uint32_t pvbuf, uint32_t bpp)
{
	DSI_CHECK_RET(DSI_WaitForNotBusy());

	DDMS_capturing = 1;

	if (lcm_params->dsi.mode == CMD_MODE) {
		LCD_CHECK_RET(LCD_EnableDCtoDsi(false));
#ifndef MT65XX_NEW_DISP
		LCD_CHECK_RET(LCD_Capture_Framebuffer(pvbuf, bpp));
#else
		DSI_CHECK_RET(DSI_Capture_Framebuffer(pvbuf, bpp, true));
#endif
	} else {
		DSI_CHECK_RET(DSI_Capture_Framebuffer(pvbuf, bpp, false));
	}


	if (lcm_params->dsi.mode == CMD_MODE)
		LCD_CHECK_RET(LCD_EnableDCtoDsi(true));

	DDMS_capturing = 0;

	return DISP_STATUS_OK;
}


/* called by "esd_recovery_kthread" */
/* protected by sem_early_suspend, sem_update_screen */
bool dsi_esd_check(void)
{
	bool result = false;

	if (lcm_params->dsi.mode == CMD_MODE) {
		result = lcm_drv->esd_check();
		return result;
	}
#ifndef BUILD_UBOOT
#ifndef MT65XX_NEW_DISP
	if (lcm_params->dsi.lcm_int_te_monitor)
		result = DSI_esd_check();

	if (result)
		return true;

	if (lcm_params->dsi.lcm_ext_te_monitor)
		result = LCD_esd_check();
#else
	result = DSI_esd_check();
	DSI_LP_Reset();
	needStartDSI = true;
	if (!result)
		dsi_update_screen(true);
#endif
	return result;
#endif

}

void disp_dsi_late_prepare(void)
{
	fbconfig_set_cmd_mode();	/* backup registers and set cmd mode; */

}


void disp_dsi_post(void)
{

	fbconfig_set_vdo_mode();
	needStartDSI = true;
	DSI_StartTransfer(true);
/* dsi_update_screen(true); */
}

int fbconfig_mipi_clk_set(unsigned int clk)
{
	DSI_STATUS ret = DSI_STATUS_ERROR;

	ret = fbconfig_DSI_set_CLK(clk);
	return ret;
}

void fbconfig_mipi_lane_set(unsigned int lane_num)
{
	fbconfig_DSI_set_lane_num(lane_num);

}

/* called by "esd_recovery_kthread" */
/* protected by sem_early_suspend, sem_update_screen */
void dsi_esd_reset(void)
{
	/* / we assume the power is on here */
	/* /  what we need is some setting for LCM init */
	if (lcm_params->dsi.mode == CMD_MODE) {
		DSI_clk_HS_mode(0);
		DSI_clk_ULP_mode(0);
		DSI_lane0_ULP_mode(0);
	} else {
		DSI_SetMode(CMD_MODE);
		DSI_clk_HS_mode(0);
		/* clock/data lane go to Ideal */
		DSI_Reset();
	}

}

const DISP_IF_DRIVER *DISP_GetDriverDSI(void)
{
	static const DISP_IF_DRIVER DSI_DISP_DRV = {
		.init = dsi_init,
		.enable_power = dsi_enable_power,
		.update_screen = dsi_update_screen,
		.get_working_buffer_size = dsi_get_working_buffer_size,

		.get_panel_color_format = dsi_get_panel_color_format,
		.get_working_buffer_bpp = dsi_get_working_buffer_bpp,
		.init_te_control = init_lcd_te_control,
		.get_dithering_bpp = dsi_get_dithering_bpp,
		.capture_framebuffer = dsi_capture_framebuffer,
		.esd_reset = dsi_esd_reset,
		.esd_check = dsi_esd_check,
	};

	return &DSI_DISP_DRV;
}
