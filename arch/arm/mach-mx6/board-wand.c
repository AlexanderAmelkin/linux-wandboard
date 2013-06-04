
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/phy.h>

#include <mach/common.h>
#include <mach/devices-common.h>
#include <mach/gpio.h>
#include <mach/iomux-v3.h>
#include <mach/mx6.h>

#include <mach/iomux-mx6dl.h>
#include <mach/iomux-mx6q.h>

#include "crm_regs.h"
#include "devices-imx6q.h"
#include "usb.h"
#include "board-wand.h"

#define WAND_BT_ON		IMX_GPIO_NR(3, 13)
#define WAND_BT_WAKE		IMX_GPIO_NR(3, 14)
#define WAND_BT_HOST_WAKE	IMX_GPIO_NR(3, 15)

#define WAND_RGMII_INT		IMX_GPIO_NR(1, 28)
#define WAND_RGMII_RST		IMX_GPIO_NR(3, 29)

#define WAND_USB_OTG_OC		IMX_GPIO_NR(1, 9)
#define WAND_USB_OTG_PWR	IMX_GPIO_NR(3, 22)
#define WAND_USB_H1_OC		IMX_GPIO_NR(3, 30)

#define WAND_WL_REF_ON		IMX_GPIO_NR(2, 29)
#define WAND_WL_RST_N		IMX_GPIO_NR(5, 2)
#define WAND_WL_REG_ON		IMX_GPIO_NR(1, 26)
#define WAND_WL_HOST_WAKE	IMX_GPIO_NR(1, 29)
#define WAND_WL_WAKE		IMX_GPIO_NR(1, 30)


/* See arch/arm/plat-mxc/include/mach/iomux-mx6dl.h for definitions */

/****************************************************************************
 *                                                                          
 * DMA controller init
 *                                                                          
 ****************************************************************************/

static __init void wand_init_dma(void) {
        imx6q_add_dma();        
}


/****************************************************************************
 *                                                                          
 * SD init
 *
 * SD1 is routed to EDM connector (external SD on wand baseboard)
 * SD2 is WiFi
 * SD3 is boot SD on the module
 *                                                                          
 ****************************************************************************/

/* ------------------------------------------------------------------------ */

static int wand_sd_speed_change(unsigned int sd, int clock) {
	static int pad_speed[3] = { 200, 200, 200 };

	if (clock > 100000000) {                
		if (pad_speed[sd] == 200) return 0;
		pad_speed[sd] = 200;
		wand_mux_pads_init_sdmmc(sd,200);

	} else if (clock > 52000000) {
		if (pad_speed[sd] == 100) return 0;
		pad_speed[sd] = 100;
		wand_mux_pads_init_sdmmc(sd,100);
	} else {
		if (pad_speed[sd] == 50) return 0;
		pad_speed[sd] = 50;
		wand_mux_pads_init_sdmmc(sd,50);
	}
	return 0;
}

/* ------------------------------------------------------------------------ */

#define WAND_SD1_CD		IMX_GPIO_NR(1, 2)
#define WAND_SD3_CD		IMX_GPIO_NR(3, 9)
#define WAND_SD3_WP		IMX_GPIO_NR(1, 10)

static const struct esdhc_platform_data wand_sd_data[3] = {
	{
		.cd_gpio		= WAND_SD1_CD,
		.wp_gpio		=-EINVAL,
		.keep_power_at_suspend	= 1,
	        .support_8bit		= 0,
		.platform_pad_change	= wand_sd_speed_change,
                .cd_type                = ESDHC_CD_CONTROLLER,
	}, {
		.cd_gpio		=-EINVAL,
		.wp_gpio		=-EINVAL,
		.keep_power_at_suspend	= 1,
		.platform_pad_change	= wand_sd_speed_change,
                .always_present		= 1,
                .cd_type                = ESDHC_CD_PERMANENT,
	}, {
		.cd_gpio		= WAND_SD3_CD,
		.wp_gpio		= WAND_SD3_WP,
		.keep_power_at_suspend	= 1,
		.support_8bit		= 0,
		.delay_line		= 0,
		.platform_pad_change	= wand_sd_speed_change,
                .cd_type                = ESDHC_CD_CONTROLLER,
	}
};

/* ------------------------------------------------------------------------ */

static void wand_init_sd(void) {
	int i;
	/* Card Detect for SD1 & SD3, respectively */
	EDM_SET_PAD(PAD_GPIO_2__GPIO_1_2); 
	EDM_SET_PAD(PAD_EIM_DA9__GPIO_3_9);

	/* Add mmc devices in reverse order, so mmc0 always is boot sd (SD3) */
	for (i=2; i>=0; i--) {
		wand_mux_pads_init_sdmmc(i,50);
                imx6q_add_sdhci_usdhc_imx(i, &wand_sd_data[i]);
	}
}


/****************************************************************************
 *                                                                          
 * I2C
 *                                                                          
 ****************************************************************************/

/* ------------------------------------------------------------------------ */

static struct imxi2c_platform_data wand_i2c_data[] = {
	{ .bitrate	= 100000, },
	{ .bitrate	= 400000, },
	{ .bitrate	= 400000, },
};

/* ------------------------------------------------------------------------ */

static void __init wand_init_i2c(void) {
        int i;
	for (i=0; i<3; i++) {
		wand_mux_pads_init_i2c(i);
		imx6q_add_imx_i2c(i, &wand_i2c_data[i]);
        }
}


/****************************************************************************
 *                                                                          
 * Initialize debug console (UART1)
 *                                                                          
 ****************************************************************************/

/* ------------------------------------------------------------------------ */
 
static __init void wand_init_uart(void) {

	wand_mux_pads_init_uart();

	imx6q_add_imx_uart(0, NULL);
}


/****************************************************************************
 *                                                                          
 * Initialize sound (SSI, ASRC, AUD3 channel and S/PDIF)
 *                                                                          
 ****************************************************************************/
/* ------------------------------------------------------------------------ */

extern struct mxc_audio_platform_data wand_audio_channel_data;

/* This function is called as a callback from the audio channel data struct */
static int wand_audio_clock_enable(void) {
	struct clk *clko;
	struct clk *new_parent;
	int rate;

	clko = clk_get(NULL, "clko_clk");
	if (IS_ERR(clko)) return PTR_ERR(clko);
	
        new_parent = clk_get(NULL, "ahb");
	if (!IS_ERR(new_parent)) {
		clk_set_parent(clko, new_parent);
		clk_put(new_parent);
	}
        
	rate = clk_round_rate(clko, 16000000);
	if (rate < 8000000 || rate > 27000000) {
		pr_err("SGTL5000: mclk freq %d out of range!\n", rate);
		clk_put(clko);
		return -1;
	}

        wand_audio_channel_data.sysclk = rate;
	clk_set_rate(clko, rate);
	clk_enable(clko);
        
	return 0;        
}

/* ------------------------------------------------------------------------ */

/* This struct is added by the baseboard when initializing the codec */
struct mxc_audio_platform_data wand_audio_channel_data = {
	.ssi_num = 1,
	.src_port = 2,
	.ext_port = 3, /* audio channel: 3=AUD3. TODO: EDM */
	.init = wand_audio_clock_enable,
	.hp_gpio = -1,
};
EXPORT_SYMBOL_GPL(wand_audio_channel_data); /* TODO: edm naming? */

/* ------------------------------------------------------------------------ */

static int wand_set_spdif_clk_rate(struct clk *clk, unsigned long rate) {
	unsigned long rate_actual;
	rate_actual = clk_round_rate(clk, rate);
	clk_set_rate(clk, rate_actual);
	return 0;
}

/* ------------------------------------------------------------------------ */

static struct mxc_spdif_platform_data wand_spdif = {
	.spdif_tx		= 1,	/* enable tx */
	.spdif_rx		= 1,	/* enable rx */
	.spdif_clk_44100	= 1,    /* tx clk from spdif0_clk_root */
	.spdif_clk_48000	= 1,    /* tx clk from spdif0_clk_root */
	.spdif_div_44100	= 23,
	.spdif_div_48000	= 37,
	.spdif_div_32000	= 37,
	.spdif_rx_clk		= 0,    /* rx clk from spdif stream */
	.spdif_clk_set_rate	= wand_set_spdif_clk_rate,
	.spdif_clk		= NULL, /* spdif bus clk */
};

/* ------------------------------------------------------------------------ */

static struct imx_ssi_platform_data wand_ssi_pdata = {
	.flags = IMX_SSI_DMA | IMX_SSI_SYN,
};

/* ------------------------------------------------------------------------ */

static struct imx_asrc_platform_data wand_asrc_data = {
	.channel_bits	= 4,
	.clk_map_ver	= 2,
};

/* ------------------------------------------------------------------------ */

void __init wand_init_audio(void) {
        
	wand_mux_pads_init_audio();

        /* Sample rate converter is added together with audio */
        wand_asrc_data.asrc_core_clk = clk_get(NULL, "asrc_clk");
        wand_asrc_data.asrc_audio_clk = clk_get(NULL, "asrc_serial_clk");
	imx6q_add_asrc(&wand_asrc_data);

	imx6q_add_imx_ssi(1, &wand_ssi_pdata);
	/* Enable SPDIF */

	EDM_SET_PAD(PAD_ENET_RXD0__SPDIF_OUT1);

	wand_spdif.spdif_core_clk = clk_get_sys("mxc_spdif.0", NULL);
	clk_put(wand_spdif.spdif_core_clk);
	imx6q_add_spdif(&wand_spdif);                
	imx6q_add_spdif_dai();
	imx6q_add_spdif_audio_device();
}


/*****************************************************************************
 *                                                                           
 * Init FEC and AR8031 PHY
 *                                                                            
 *****************************************************************************/

/* ------------------------------------------------------------------------ */

static int wand_fec_phy_init(struct phy_device *phydev) {
	unsigned short val;

	/* Enable AR8031 125MHz clk */
	phy_write(phydev, 0x0d, 0x0007); /* Set device address to 7*/
	phy_write(phydev, 0x00, 0x8000); /* Apply by soft reset */
	udelay(500); 
        
	phy_write(phydev, 0x0e, 0x8016); /* set mmd reg */
	phy_write(phydev, 0x0d, 0x4007); /* apply */

	val = phy_read(phydev, 0xe);
	val &= 0xffe3;
	val |= 0x18;
	phy_write(phydev, 0xe, val);
	phy_write(phydev, 0x0d, 0x4007); /* Post data */        

	/* Introduce random tx clock delay. Why is this needed? */
	phy_write(phydev, 0x1d, 0x5);
	val = phy_read(phydev, 0x1e);
	val |= 0x0100;
	phy_write(phydev, 0x1e, val);

	return 0;
}

/* ------------------------------------------------------------------------ */

static int wand_fec_power_hibernate(struct phy_device *phydev) { return 0; }

/* ------------------------------------------------------------------------ */

static struct fec_platform_data wand_fec_data = {
	.init			= wand_fec_phy_init,
	.power_hibernate	= wand_fec_power_hibernate,
	.phy			= PHY_INTERFACE_MODE_RGMII,
//	.phy_noscan_mask	= ~2, /* phy is on adress 1 */
};

/* ------------------------------------------------------------------------ */

static __init void wand_init_ethernet(void) {

	wand_mux_pads_init_ethernet();

	gpio_request(WAND_RGMII_RST, "rgmii reset");
	gpio_direction_output(WAND_RGMII_RST, 0);
#ifdef CONFIG_FEC_1588
	mxc_iomux_set_gpr_register(1, 21, 1, 1);
#endif
	msleep(10);
	gpio_set_value(WAND_RGMII_RST, 1);
	imx6_init_fec(wand_fec_data);
}


/****************************************************************************
 *                                                                          
 * USB
 *                                                                          
 ****************************************************************************/


/* ------------------------------------------------------------------------ */

static void wand_usbotg_vbus(bool on) {
        gpio_set_value_cansleep(WAND_USB_OTG_PWR, !on);
}

/* ------------------------------------------------------------------------ */

static __init void wand_init_usb(void) {
        
	wand_mux_pads_init_usb();

        gpio_request(WAND_USB_OTG_OC, "otg oc");
	gpio_direction_input(WAND_USB_OTG_OC);

        gpio_request(WAND_USB_OTG_PWR, "otg pwr");
        gpio_direction_output(WAND_USB_OTG_PWR, 0);

	imx_otg_base = MX6_IO_ADDRESS(MX6Q_USB_OTG_BASE_ADDR);
	mxc_iomux_set_gpr_register(1, 13, 1, 1);

	mx6_set_otghost_vbus_func(wand_usbotg_vbus);

        gpio_request(WAND_USB_H1_OC, "usbh1 oc");
	gpio_direction_input(WAND_USB_H1_OC);
}

/****************************************************************************
 *                                                                          
 * CORE DISPLAY
 *                                                                          
 ****************************************************************************/

/* HDMI------------------------------------------------------------------------ */

#include <linux/mfd/mxc-hdmi-core.h>

static void wand_hdmi_dev_init(int ipu_id, int disp_id) {
	if ((unsigned)ipu_id > 1) ipu_id = 0;
	if ((unsigned)disp_id > 1) disp_id = 0;

	mxc_iomux_set_gpr_register(3, 2, 2, 2*ipu_id + disp_id);

	/* Set HDMI event as SDMA event2 while Chip version later than TO1.2 */
	if (hdmi_SDMA_check())
		mxc_iomux_set_gpr_register(0, 0, 1, 1);
}

/* ------------------------------------------------------------------------ */

static struct fsl_mxc_hdmi_platform_data wand_hdmi_data = {
	.init = wand_hdmi_dev_init,
};

/* ------------------------------------------------------------------------ */

static struct fsl_mxc_hdmi_core_platform_data wand_hdmi_core_data = {
	.ipu_id		= 1,
	.disp_id	= 0,
};

/* ------------------------------------------------------------------------ */

static const struct i2c_board_info wand_hdmi_i2c_info = {
	I2C_BOARD_INFO("mxc_hdmi_i2c", 0x50),
};

static void wand_init_display_hdmi(void) {

	i2c_register_board_info(0, &wand_hdmi_i2c_info, 1);

	imx6q_add_mxc_hdmi_core(&wand_hdmi_core_data);
	imx6q_add_mxc_hdmi(&wand_hdmi_data);
        /* Enable HDMI audio */
	imx6q_add_hdmi_soc();
	imx6q_add_hdmi_soc_dai();        
//	mxc_iomux_set_gpr_register(0, 0, 1, 1);
}

/* ------------------------------------------------------------------------ */

/*******LVDS*************/
static struct fsl_mxc_ldb_platform_data wand_ldb_data = {
	.ipu_id = 0,
	.disp_id = 1,
	.ext_ref = 1,
	.mode = LDB_DUL_DI1,//LDB_SEP1,
	.sec_ipu_id = 0,
	.sec_disp_id = 0,
};

static void wand_init_display_lvds(void) {
	wand_mux_pads_init_lvds();

        gpio_request(IMX_GPIO_NR(2, 8), "lvds0_en");
        gpio_direction_output(IMX_GPIO_NR(2, 8), 1);

        gpio_request(IMX_GPIO_NR(2, 9), "lvds0_blt_ctrl");
        gpio_direction_output(IMX_GPIO_NR(2, 9), 1);

        gpio_request(IMX_GPIO_NR(2, 10), "disp0_bklen");
        gpio_direction_output(IMX_GPIO_NR(2, 10), 1);

        gpio_request(IMX_GPIO_NR(2, 11), "disp0_vdden");
        gpio_direction_output(IMX_GPIO_NR(2, 11), 1);

	imx6q_add_ldb(&wand_ldb_data);
}

/****************************/

static struct fsl_mxc_lcd_platform_data wand_lcdif_data = {
	.ipu_id = 0,
	.disp_id = 0,
	.default_ifmt = IPU_PIX_FMT_RGB565,
};

static void wand_init_display_lcdif(void) {
	wand_mux_pads_init_lcdif();
	imx6q_add_lcdif(&wand_lcdif_data);

}
/**************************/

static struct ipuv3_fb_platform_data wand_fb_pdata[] = {
	{ /*fb0*/
	.disp_dev = "ldb",
	.interface_pix_fmt = IPU_PIX_FMT_RGB666,
	.mode_str = "LDB-XGA",
	.default_bpp = 16,
	.int_clk = false,
	.late_init = false,
	}, {
	.disp_dev = "hdmi",
	.interface_pix_fmt = IPU_PIX_FMT_RGB24,
	.mode_str = "1920x1080M@60",
	.default_bpp = 32,
	.int_clk = false,
	.late_init = false,
	}, {
	.disp_dev = "ldb",
	.interface_pix_fmt = IPU_PIX_FMT_RGB666,
	.mode_str = "LDB-XGA",
	.default_bpp = 16,
	.int_clk = false,
	.late_init = false,
	},
};

static struct imx_ipuv3_platform_data wand_ipu_data[] = {
	{
		.rev = 4,
		.csi_clk[0] = "clko_clk",
		.bypass_reset = false,
	}, {
		.rev = 4,
		.csi_clk[0] = "clko_clk",
		.bypass_reset = false,
	},
};

static __init void wand_init_ipu(void) {
	int i;

	imx6q_add_ipuv3(0, &wand_ipu_data[0]);

	if (cpu_is_mx6q()) {
		imx6q_add_ipuv3(1, &wand_ipu_data[1]);
		for (i = 0; i < 4 && i < ARRAY_SIZE(wand_fb_pdata); i++)
			imx6q_add_ipuv3fb(i, &wand_fb_pdata[i]);
	} else
		for (i = 0; i < 2 && i < ARRAY_SIZE(wand_fb_pdata); i++)
			imx6q_add_ipuv3fb(i, &wand_fb_pdata[i]);

	/*
	 * MX6DL/Solo only supports single IPU
	 * The following codes are used to change ipu id
	 * and display id information for MX6DL/Solo. Then
	 * register 1 IPU device and up to 2 displays for
	 * MX6DL/Solo
	 */
	if (cpu_is_mx6dl()) {
		wand_ldb_data.ipu_id = 0;
		wand_ldb_data.disp_id = 1;
		wand_hdmi_core_data.ipu_id = 0;
		wand_hdmi_core_data.disp_id = 0;
		wand_ldb_data.sec_ipu_id = 0;
	}
	imx6q_add_vdoa();
}

static void wand_init_display(void) {
	wand_init_display_hdmi();
	wand_init_display_lvds();
	wand_init_display_lcdif();
}

/* ------------------------------------------------------------------------ */

/****************************************************************************
 *                                                                          
 * AHCI - SATA
 *                                                                          
 ****************************************************************************/

#if defined(CONFIG_IMX_HAVE_PLATFORM_AHCI)
#include <mach/ahci_sata.h>

static struct clk *wand_sata_clk;

/* HW Initialization, if return 0, initialization is successful. */
static int wand_sata_init(struct device *dev, void __iomem *addr)
{
	u32 tmpdata;
	int ret = 0;
	struct clk *clk;

	wand_sata_clk = clk_get(dev, "imx_sata_clk");
	if (IS_ERR(wand_sata_clk)) {
		dev_err(dev, "no sata clock.\n");
		return PTR_ERR(wand_sata_clk);
	}
	ret = clk_enable(wand_sata_clk);
	if (ret) {
		dev_err(dev, "can't enable sata clock.\n");
		goto put_sata_clk;
	}

	/* Set PHY Paremeters, two steps to configure the GPR13,
	 * one write for rest of parameters, mask of first write is 0x07FFFFFD,
	 * and the other one write for setting the mpll_clk_off_b
	 *.rx_eq_val_0(iomuxc_gpr13[26:24]),
	 *.los_lvl(iomuxc_gpr13[23:19]),
	 *.rx_dpll_mode_0(iomuxc_gpr13[18:16]),
	 *.sata_speed(iomuxc_gpr13[15]),
	 *.mpll_ss_en(iomuxc_gpr13[14]),
	 *.tx_atten_0(iomuxc_gpr13[13:11]),
	 *.tx_boost_0(iomuxc_gpr13[10:7]),
	 *.tx_lvl(iomuxc_gpr13[6:2]),
	 *.mpll_ck_off(iomuxc_gpr13[1]),
	 *.tx_edgerate_0(iomuxc_gpr13[0]),
	 */
	tmpdata = readl(IOMUXC_GPR13);
	writel(((tmpdata & ~0x07FFFFFD) | 0x0593A044), IOMUXC_GPR13);

	/* enable SATA_PHY PLL */
	tmpdata = readl(IOMUXC_GPR13);
	writel(((tmpdata & ~0x2) | 0x2), IOMUXC_GPR13);

	/* Get the AHB clock rate, and configure the TIMER1MS reg later */
	clk = clk_get(NULL, "ahb");
	if (IS_ERR(clk)) {
		dev_err(dev, "no ahb clock.\n");
		ret = PTR_ERR(clk);
		goto release_sata_clk;
	}
	tmpdata = clk_get_rate(clk) / 1000;
	clk_put(clk);

	ret = sata_init(addr, tmpdata);
	if (ret == 0)
		return ret;

release_sata_clk:
	clk_disable(wand_sata_clk);
put_sata_clk:
	clk_put(wand_sata_clk);

	return ret;
}

static void wand_sata_exit(struct device *dev)
{
	clk_disable(wand_sata_clk);
	clk_put(wand_sata_clk);
}

static struct ahci_platform_data wand_sata_data = {
	.init = wand_sata_init,
	.exit = wand_sata_exit,
};


static __init void wand_init_ahci(void)
{
	/* SATA is not supported by MX6DL/Solo */
	if (cpu_is_mx6q())
		imx6q_add_ahci(0, &wand_sata_data);
}

#else

static inline void wand_init_ahci(void) { ;}
#endif

/****************************************************************************
 *                                                                          
 * WiFi
 *                                                                          
 ****************************************************************************/


/* ------------------------------------------------------------------------ */

/* assumes SD/MMC pins are set; call after wand_init_sd() */
static __init void wand_init_wifi(void) {

	wand_mux_pads_init_wifi();
                
	gpio_request(WAND_WL_RST_N, "wl_rst_n");
	gpio_direction_output(WAND_WL_RST_N, 0);
	msleep(11);
	gpio_set_value(WAND_WL_RST_N, 1);

	gpio_request(WAND_WL_REF_ON, "wl_ref_on");
	gpio_direction_output(WAND_WL_REF_ON, 1);

	gpio_request(WAND_WL_REG_ON, "wl_reg_on");
	gpio_direction_output(WAND_WL_REG_ON, 1);
        
	gpio_request(WAND_WL_WAKE, "wl_wake");
	gpio_direction_output(WAND_WL_WAKE, 1);

	gpio_request(WAND_WL_HOST_WAKE, "wl_host_wake");
	gpio_direction_input(WAND_WL_HOST_WAKE);
}


/****************************************************************************
 *                                                                          
 * Bluetooth
 *                                                                          
 ****************************************************************************/


/* ------------------------------------------------------------------------ */
#include <mach/imx_rfkill.h>

static void wandboard_bt_reset(void)
{
	printk(KERN_INFO "wandboard_bt_reset");
#if 0
	gpio_request(SABRESD_BT_RESET, "bt-reset");
	gpio_direction_output(SABRESD_BT_RESET, 0);
	/* pull down reset pin at least >5ms */
	mdelay(6);
	/* pull up after power supply BT */
	gpio_direction_output(SABRESD_BT_RESET, 1);
	gpio_free(SABRESD_BT_RESET);
	msleep(100);
#endif
}

static int wandboard_bt_power_change(int status)
{
	if (status)
		wandboard_bt_reset();
	return 0;
}

static struct platform_device wandboard_bt_rfkill = {
	.name = "mxc_bt_rfkill",
};

static struct imx_bt_rfkill_platform_data wandboard_bt_rfkill_data = {
	.power_change = wandboard_bt_power_change,
};


static const struct imxuart_platform_data wand_bt_uart_data = {
	.flags = IMXUART_HAVE_RTSCTS,
	.dma_req_tx = MX6Q_DMA_REQ_UART3_TX,
	.dma_req_rx = MX6Q_DMA_REQ_UART3_RX,
};

/* ------------------------------------------------------------------------ */

/* This assumes wifi is initialized (chip has power) */
static __init void wand_init_bluetooth(void) {

	wand_mux_pads_init_bluetooth();

	imx6q_add_imx_uart(2, &wand_bt_uart_data);

	mxc_register_device(&wandboard_bt_rfkill, &wandboard_bt_rfkill_data);

	gpio_request(WAND_BT_ON, "bt_on");
	gpio_direction_output(WAND_BT_ON, 0);
	msleep(11);
	gpio_set_value(WAND_BT_ON, 1);

	gpio_request(WAND_BT_WAKE, "bt_wake");
	gpio_direction_output(WAND_BT_WAKE, 1);

	gpio_request(WAND_BT_HOST_WAKE, "bt_host_wake");
	gpio_direction_input(WAND_BT_WAKE);
}


/****************************************************************************
 *                                                                          
 * Power and thermal management
 *                                                                          
 ****************************************************************************/

extern bool enable_wait_mode;

static const struct anatop_thermal_platform_data wand_thermal = {
	.name = "anatop_thermal",
};

/* ------------------------------------------------------------------------ */

static void wand_suspend_enter(void) {
	gpio_set_value(WAND_WL_WAKE, 0);
	gpio_set_value(WAND_BT_WAKE, 0);
}

/* ------------------------------------------------------------------------ */

static void wand_suspend_exit(void) {
	gpio_set_value(WAND_WL_WAKE, 1);
	gpio_set_value(WAND_BT_WAKE, 1);
}

/* ------------------------------------------------------------------------ */

static const struct pm_platform_data wand_pm_data = {
	.name		= "imx_pm",
	.suspend_enter	= wand_suspend_enter,
	.suspend_exit	= wand_suspend_exit,
};

/* ------------------------------------------------------------------------ */

static const struct mxc_dvfs_platform_data wand_dvfscore_data = {
/*
	.reg_id			= "VDDCORE",
	.soc_id			= "VDDSOC",
*/
	.clk1_id		= "cpu_clk",
	.clk2_id 		= "gpc_dvfs_clk",
	.gpc_cntr_offset 	= MXC_GPC_CNTR_OFFSET,
	.ccm_cdcr_offset 	= MXC_CCM_CDCR_OFFSET,
	.ccm_cacrr_offset 	= MXC_CCM_CACRR_OFFSET,
	.ccm_cdhipr_offset 	= MXC_CCM_CDHIPR_OFFSET,
	.prediv_mask 		= 0x1F800,
	.prediv_offset 		= 11,
	.prediv_val 		= 3,
	.div3ck_mask 		= 0xE0000000,
	.div3ck_offset 		= 29,
	.div3ck_val 		= 2,
	.emac_val 		= 0x08,
	.upthr_val 		= 25,
	.dnthr_val 		= 9,
	.pncthr_val 		= 33,
	.upcnt_val 		= 10,
	.dncnt_val 		= 10,
	.delay_time 		= 80,
};

/* ------------------------------------------------------------------------ */

static __init void wand_init_pm(void) {
	enable_wait_mode = false;
	imx6q_add_anatop_thermal_imx(1, &wand_thermal);
	imx6q_add_pm_imx(0, &wand_pm_data);
	imx6q_add_dvfs_core(&wand_dvfscore_data);
	imx6q_add_busfreq();
}


/****************************************************************************
 *                                                                          
 * Expansion pin header GPIOs
 *                                                                          
 ****************************************************************************/


/* ------------------------------------------------------------------------ */

static __init void wand_init_external_gpios(void) {

	wand_mux_pads_init_external_gpios();

	gpio_request(IMX_GPIO_NR(3, 11), "external_gpio_0");
	gpio_export(IMX_GPIO_NR(3, 11), true);
	gpio_request(IMX_GPIO_NR(3, 27), "external_gpio_1");
	gpio_export(IMX_GPIO_NR(3, 27), true);
	gpio_request(IMX_GPIO_NR(6, 31), "external_gpio_2");
	gpio_export(IMX_GPIO_NR(6, 31), true);
	gpio_request(IMX_GPIO_NR(1, 24), "external_gpio_3");
	gpio_export(IMX_GPIO_NR(1, 24), true);
	gpio_request(IMX_GPIO_NR(7,  8), "external_gpio_4");
	gpio_export(IMX_GPIO_NR(7,  8), true);
	gpio_request(IMX_GPIO_NR(3, 26), "external_gpio_5");
	gpio_export(IMX_GPIO_NR(3, 26), true);
	gpio_request(IMX_GPIO_NR(3, 8), "external_gpio_6");
	gpio_export(IMX_GPIO_NR(3, 8), true);
	gpio_request(IMX_GPIO_NR(4, 5), "external_gpio_7");
	gpio_export(IMX_GPIO_NR(4, 5), true);
}


/****************************************************************************
 *                                                                          
 * SPI - while not used on the Wandboard, the pins are routed out
 *                                                                          
 ****************************************************************************/

/* The choice of using gpios for chipselect is deliberate,
   there can be issues using the dedicated mux modes for cs.
*/

/* ------------------------------------------------------------------------ */

static const int wand_spi1_chipselect[] = { IMX_GPIO_NR(2, 30) };

/* platform device */
static const struct spi_imx_master wand_spi1_data = {
	.chipselect     = wand_spi1_chipselect,
	.num_chipselect = ARRAY_SIZE(wand_spi1_chipselect),
};

/* ------------------------------------------------------------------------ */

static const int wand_spi2_chipselect[] = { IMX_GPIO_NR(2, 26), IMX_GPIO_NR(2, 27) };

static const struct spi_imx_master wand_spi2_data = {
	.chipselect     = wand_spi2_chipselect,
	.num_chipselect = ARRAY_SIZE(wand_spi2_chipselect),
};

/* ------------------------------------------------------------------------ */

static void __init wand_init_spi(void) {

	wand_mux_pads_init_spi();

	imx6q_add_ecspi(0, &wand_spi1_data);
	imx6q_add_ecspi(1, &wand_spi2_data);
}

/****************************************************************************
 *                                                                          
 * Vivante GPU/VPU
 *                                                                          
 ****************************************************************************/

static struct viv_gpu_platform_data wand_gpu_pdata = {
	.reserved_mem_size = SZ_128M + SZ_64M - SZ_16M,
};

static __init void wand_init_gpu(void) {
	imx_add_viv_gpu(&imx6_gpu_data, &wand_gpu_pdata);
        imx6q_add_vpu();
        imx6q_add_v4l2_output(0);
}

/*****************************************************************************
 *                                                                           
 * Init clocks and early boot console                                      
 *                                                                            
 *****************************************************************************/

extern void __iomem *twd_base;

static void __init wand_init_timer(void) {
	struct clk *uart_clk;
#ifdef CONFIG_LOCAL_TIMERS
	twd_base = ioremap(LOCAL_TWD_ADDR, SZ_256);
#endif
	mx6_clocks_init(32768, 24000000, 0, 0);

	uart_clk = clk_get_sys("imx-uart.0", NULL);
	early_console_setup(UART1_BASE_ADDR, uart_clk);
}

/* ------------------------------------------------------------------------ */

static struct sys_timer wand_timer = {
	.init = wand_init_timer,
};

/*****************************************************************************
 *****************************************************************************/


#include <asm/setup.h>
#include <linux/ion.h>

static struct ion_platform_data wand_ion_data = {
	.nr = 1,
	.heaps = {
		{
		.id = 0,
		.type = ION_HEAP_TYPE_CARVEOUT,
		.name = "vpu_ion",
		.size = SZ_16M,
		},
	},
};

#if defined(CONFIG_ION)
static void wand_init_ion(void)
{
	if (wand_ion_data.heaps[0].size)
		imx6q_add_ion(0, &wand_ion_data,
			sizeof(wand_ion_data) + sizeof(struct ion_platform_heap));
}
#else
static inline void wand_init_ion(void) {;}
#endif

static void __init fixup_wand_board(struct machine_desc *desc, struct tag *tags,
				   char **cmdline, struct meminfo *mi)
{
	char *str;
	struct tag *t;
	int i = 0;
	struct ipuv3_fb_platform_data *pdata_fb = wand_fb_pdata;

	for_each_tag(t, tags) {
		if (t->hdr.tag == ATAG_CMDLINE) {
			str = t->u.cmdline.cmdline;
			str = strstr(str, "fbmem=");
			if (str != NULL) {
				str += 6;
				pdata_fb[i++].res_size[0] = memparse(str, &str);
				while (*str == ',' &&
					i < ARRAY_SIZE(wand_fb_pdata)) {
					str++;
					pdata_fb[i++].res_size[0] = memparse(str, &str);
				}
			}
			/* ION reserved memory */
			str = t->u.cmdline.cmdline;
			str = strstr(str, "ionmem=");
			if (str != NULL) {
				str += 7;
				wand_ion_data.heaps[0].size = memparse(str, &str);
			}
			/* Primary framebuffer base address */
			str = t->u.cmdline.cmdline;
			str = strstr(str, "fb0base=");
			if (str != NULL) {
				str += 8;
				pdata_fb[0].res_base[0] =
						simple_strtol(str, &str, 16);
			}
			/* GPU reserved memory */
			str = t->u.cmdline.cmdline;
			str = strstr(str, "gpumem=");
			if (str != NULL) {
				str += 7;
				wand_gpu_pdata.reserved_mem_size = memparse(str, &str);
			}
			break;
		}
	}
}

#ifdef CONFIG_ANDROID_RAM_CONSOLE
static struct resource ram_console_resource = {
	.name = "android ram console",
	.flags = IORESOURCE_MEM,
};

static struct platform_device android_ram_console = {
	.name = "ram_console",
	.num_resources = 1,
	.resource = &ram_console_resource,
};

static int __init imx6x_add_ram_console(void)
{
	return platform_device_register(&android_ram_console);
}
#else
#define imx6x_add_ram_console() do {} while (0)
#endif

#include <linux/memblock.h>
static void __init wand_reserve(void)
{
	phys_addr_t phys;
	int i, fb0_reserved = 0, fb_array_size;

	/*
	 * Reserve primary framebuffer memory if its base address
	 * is set by kernel command line.
	 */
	fb_array_size = ARRAY_SIZE(wand_fb_pdata);
	if (fb_array_size > 0 && wand_fb_pdata[0].res_base[0] &&
	    wand_fb_pdata[0].res_size[0]) {
		memblock_reserve(wand_fb_pdata[0].res_base[0],
				 wand_fb_pdata[0].res_size[0]);
		memblock_remove(wand_fb_pdata[0].res_base[0],
				wand_fb_pdata[0].res_size[0]);
		wand_fb_pdata[0].late_init = true;
		wand_ipu_data[wand_ldb_data.ipu_id].bypass_reset = true;
		fb0_reserved = 1;
	}
	for (i = fb0_reserved; i < fb_array_size; i++)
		if (wand_fb_pdata[i].res_size[0]) {
			/* Reserve for other background buffer. */
			phys = memblock_alloc(wand_fb_pdata[i].res_size[0],
						SZ_4K);
			memblock_remove(phys, wand_fb_pdata[i].res_size[0]);
			wand_fb_pdata[i].res_base[0] = phys;
		}

#ifdef CONFIG_ANDROID_RAM_CONSOLE
#if 1
	phys = memblock_alloc_base(SZ_1M, SZ_4K, SZ_1G);
	printk("EDWARD :  ram console init at phys 0x%x\n",phys);
	memblock_remove(phys, SZ_1M);
	memblock_free(phys, SZ_1M);
	ram_console_resource.start = phys;
	ram_console_resource.end   = phys + SZ_1M - 1;
#else
//	phys = memblock_alloc_base(SZ_128K, SZ_4K, SZ_1G);
	phys = memblock_alloc_base(SZ_1M, SZ_4K, SZ_1G);
	printk("EDWARD :  ram console init at phys 0x%x\n",phys);
	memblock_remove(phys, SZ_128K);
	memblock_free(phys, SZ_128K);
	ram_console_resource.start = phys;
	ram_console_resource.end   = phys + SZ_128K - 1;
#endif
#endif

#if defined(CONFIG_MXC_GPU_VIV) || defined(CONFIG_MXC_GPU_VIV_MODULE)
	if (wand_gpu_pdata.reserved_mem_size) {
		printk("EDWARD : GPU_Reserved Memory equals to %d\n",wand_gpu_pdata.reserved_mem_size);
		phys = memblock_alloc_base(wand_gpu_pdata.reserved_mem_size,
					   SZ_4K, SZ_512M);
		printk("EDWARD :  gpumem init at phys 0x%x\n",phys);
		memblock_remove(phys, wand_gpu_pdata.reserved_mem_size);
		wand_gpu_pdata.reserved_mem_base = phys;
	}
#endif

#if defined(CONFIG_ION)
	if (wand_ion_data.heaps[0].size) {
		phys = memblock_alloc(wand_ion_data.heaps[0].size, SZ_4K);
		memblock_remove(phys, wand_ion_data.heaps[0].size);
		wand_ion_data.heaps[0].base = phys;
	}
#endif
}

/*****************************************************************************
 *                                                                           
 * BOARD INIT                                                                
 *                                                                            
 *****************************************************************************/

#include <mach/system.h>

extern char *gp_reg_id;
extern char *soc_reg_id;
extern u32 enable_ldo_mode;

static void __init wand_board_init(void) {

	if(enable_ldo_mode == LDO_MODE_BYPASSED)
	{
		gp_reg_id = "DUMMY_VDDCORE";
		soc_reg_id = "DUMMY_VDDSOC";
	}

	wand_init_dma();
	wand_init_uart();
	imx6x_add_ram_console();

	wand_init_sd();
	wand_init_i2c();
	wand_init_audio();
	wand_init_ethernet();
	wand_init_usb();
	wand_init_ipu();
	wand_init_display();
	wand_init_ion();

#if defined(CONFIG_IMX_HAVE_PLATFORM_IMX2_WDT) && defined(CONFIG_IMX2_WDT)
	imx6q_add_imx2_wdt(0, NULL);
#endif

	wand_init_ahci();

	wand_init_wifi();
	wand_init_bluetooth();
	wand_init_pm();
	wand_init_external_gpios();
	wand_init_spi();
	wand_init_gpu();
}


/* ------------------------------------------------------------------------ */
        
MACHINE_START(WANDBOARD, "Freescale i.MX 6Quad/DualLite/Solo Wandboard")
	.boot_params	= MX6_PHYS_OFFSET + 0x100,
	.fixup 		= fixup_wand_board,
	.map_io		= mx6_map_io,
	.init_irq	= mx6_init_irq,
	.init_machine	= wand_board_init,
	.timer		= &wand_timer,
	.reserve	= wand_reserve,
MACHINE_END

