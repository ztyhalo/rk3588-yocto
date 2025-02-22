// SPDX-License-Identifier: GPL-2.0
/*
 * ov5640 driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 add poweron function.
 * V0.0X01.0X02 add enum_frame_interval function.
 * V0.0X01.0X03 add quick stream on/off
 * V0.0X01.0X04 add function g_mbus_config
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x04)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

/* 45Mhz * 4 Binning */
#define OV5695_PIXEL_RATE		(45 * 1000 * 1000 * 4)
#define OV5695_XVCLK_FREQ		24000000

#define CHIP_ID				0x005640
#define OV5640_REG_CHIP_ID		0x300a

// #define OV5695_REG_CTRL_MODE		0x0100
// #define OV5695_MODE_SW_STANDBY		0x0
// #define OV5695_MODE_STREAMING		BIT(0)

// #define OV5695_REG_EXPOSURE		0x3500
// #define	OV5695_EXPOSURE_MIN		4
// #define	OV5695_EXPOSURE_STEP		1
// #define OV5695_VTS_MAX			0x7fff

// #define OV5695_REG_ANALOG_GAIN		0x3509
// #define	ANALOG_GAIN_MIN			0x10
// #define	ANALOG_GAIN_MAX			0xf8
// #define	ANALOG_GAIN_STEP		1
// #define	ANALOG_GAIN_DEFAULT		0xf8

// #define OV5695_REG_DIGI_GAIN_H		0x350a
// #define OV5695_REG_DIGI_GAIN_L		0x350b
// #define OV5695_DIGI_GAIN_L_MASK		0x3f
// #define OV5695_DIGI_GAIN_H_SHIFT	6
// #define OV5695_DIGI_GAIN_MIN		0
// #define OV5695_DIGI_GAIN_MAX		(0x4000 - 1)
// #define OV5695_DIGI_GAIN_STEP		1
// #define OV5695_DIGI_GAIN_DEFAULT	1024

// #define OV5695_REG_TEST_PATTERN		0x4503
// #define	OV5695_TEST_PATTERN_ENABLE	0x80
// #define	OV5695_TEST_PATTERN_DISABLE	0x0

// #define OV5695_REG_VTS			0x380e

// #define REG_NULL			0xFFFF

// #define OV5695_REG_VALUE_08BIT		1
// #define OV5695_REG_VALUE_16BIT		2
// #define OV5695_REG_VALUE_24BIT		3

// #define OV5695_LANES			2
// #define OV5695_BITS_PER_SAMPLE		10

#define I2C_M_WR			0
#define I2C_MSG_MAX			300
#define I2C_DATA_MAX			(I2C_MSG_MAX * 3)

#define OV5640_NAME			"ov5640"

/* regulator supplies */
static const char * const ov5640_supply_name[] = {
	"DOVDD", /* Digital I/O (1.8V) supply */
	"AVDD",  /* Analog (2.8V) supply */
	"DVDD",  /* Digital Core (1.5V) supply */
};

#define OV5640_NUM_SUPPLIES ARRAY_SIZE(ov5640_supply_name)

struct regval {
	u16 addr;
	u8 val;
};

struct ov5640_mode {
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
};

struct ov5640 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV5640_NUM_SUPPLIES];

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct ov5640_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
};

#define to_ov5640(sd) container_of(sd, struct ov5640, subdev)

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 672(0x2a0)
 * framelength 2232(0x8b8)
 * grabwindow_width 1296
 * grabwindow_height 972
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5640_global_regs[] = {
	{0x0103, 0x01},
	{0x0100, 0x00},
	{0x0300, 0x04},
	{0x0301, 0x00},
	{0x0302, 0x69},
	{0x0303, 0x00},
	{0x0304, 0x00},
	{0x0305, 0x01},
	{0x0307, 0x00},
	{0x030b, 0x00},
	{0x030c, 0x00},
	{0x030d, 0x1e},
	{0x030e, 0x04},
	{0x030f, 0x03},
	{0x0312, 0x01},
	{0x3000, 0x00},
	{0x3002, 0xa1},
	{0x3008, 0x00},
	{0x3010, 0x00},
	{0x3022, 0x51},
	{0x3106, 0x15},
	{0x3107, 0x01},
	{0x3108, 0x05},
	{0x3500, 0x00},
	{0x3501, 0x45},
	{0x3502, 0x00},
	{0x3503, 0x08},
	{0x3504, 0x03},
	{0x3505, 0x8c},
	{0x3507, 0x03},
	{0x3508, 0x00},
	{0x3509, 0x10},
	{0x350c, 0x00},
	{0x350d, 0x80},
	{0x3510, 0x00},
	{0x3511, 0x02},
	{0x3512, 0x00},
	{0x3601, 0x55},
	{0x3602, 0x58},
	{0x3614, 0x30},
	{0x3615, 0x77},
	{0x3621, 0x08},
	{0x3624, 0x40},
	{0x3633, 0x0c},
	{0x3634, 0x0c},
	{0x3635, 0x0c},
	{0x3636, 0x0c},
	{0x3638, 0x00},
	{0x3639, 0x00},
	{0x363a, 0x00},
	{0x363b, 0x00},
	{0x363c, 0xff},
	{0x363d, 0xfa},
	{0x3650, 0x44},
	{0x3651, 0x44},
	{0x3652, 0x44},
	{0x3653, 0x44},
	{0x3654, 0x44},
	{0x3655, 0x44},
	{0x3656, 0x44},
	{0x3657, 0x44},
	{0x3660, 0x00},
	{0x3661, 0x00},
	{0x3662, 0x00},
	{0x366a, 0x00},
	{0x366e, 0x0c},
	{0x3673, 0x04},
	{0x3700, 0x14},
	{0x3703, 0x0c},
	{0x3715, 0x01},
	{0x3733, 0x10},
	{0x3734, 0x40},
	{0x373f, 0xa0},
	{0x3765, 0x20},
	{0x37a1, 0x1d},
	{0x37a8, 0x26},
	{0x37ab, 0x14},
	{0x37c2, 0x04},
	{0x37cb, 0x09},
	{0x37cc, 0x13},
	{0x37cd, 0x1f},
	{0x37ce, 0x1f},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xaf},
	{0x3808, 0x05},
	{0x3809, 0x10},
	{0x380a, 0x03},
	{0x380b, 0xcc},
	{0x380c, 0x02},
	{0x380d, 0xa0},
	{0x380e, 0x08},
	{0x380f, 0xb8},
	{0x3810, 0x00},
	{0x3811, 0x06},
	{0x3812, 0x00},
	{0x3813, 0x06},
	{0x3814, 0x03},
	{0x3815, 0x01},
	{0x3816, 0x03},
	{0x3817, 0x01},
	{0x3818, 0x00},
	{0x3819, 0x00},
	{0x381a, 0x00},
	{0x381b, 0x01},
	{0x3820, 0x8b},
	{0x3821, 0x01},
	{0x3c80, 0x08},
	{0x3c82, 0x00},
	{0x3c83, 0x00},
	{0x3c88, 0x00},
	{0x3d85, 0x14},
	{0x3f02, 0x08},
	{0x3f03, 0x10},
	{0x4008, 0x02},
	{0x4009, 0x09},
	{0x404e, 0x20},
	{0x4501, 0x00},
	{0x4502, 0x10},
	{0x4800, 0x00},
	{0x481f, 0x2a},
	{0x4837, 0x13},
	{0x5000, 0x17},
	{0x5780, 0x3e},
	{0x5781, 0x0f},
	{0x5782, 0x44},
	{0x5783, 0x02},
	{0x5784, 0x01},
	{0x5785, 0x01},
	{0x5786, 0x00},
	{0x5787, 0x04},
	{0x5788, 0x02},
	{0x5789, 0x0f},
	{0x578a, 0xfd},
	{0x578b, 0xf5},
	{0x578c, 0xf5},
	{0x578d, 0x03},
	{0x578e, 0x08},
	{0x578f, 0x0c},
	{0x5790, 0x08},
	{0x5791, 0x06},
	{0x5792, 0x00},
	{0x5793, 0x52},
	{0x5794, 0xa3},
	{0x5b00, 0x00},
	{0x5b01, 0x1c},
	{0x5b02, 0x00},
	{0x5b03, 0x7f},
	{0x5b05, 0x6c},
	{0x5e10, 0xfc},
	{0x4010, 0xf1},
	{0x3503, 0x08},
	{0x3505, 0x8c},
	{0x3507, 0x03},
	{0x3508, 0x00},
	{0x3509, 0xf8},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 740(0x2e4)
 * framelength 2024(0x7e8)
 * grabwindow_width 2592
 * grabwindow_height 1944
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5640_2592x1944_regs[] = {
	{0x3501, 0x7e},
	{0x366e, 0x18},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xab},
	{0x3808, 0x0a},
	{0x3809, 0x20},
	{0x380a, 0x07},
	{0x380b, 0x98},
	{0x380c, 0x02},
	{0x380d, 0xe4},
	{0x380e, 0x07},
	{0x380f, 0xe8},
	{0x3811, 0x06},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0x88},
	{0x3821, 0x00},
	{0x4501, 0x00},
	{0x4008, 0x04},
	{0x4009, 0x13},
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 672(0x2a0)
 * framelength 2232(0x8b8)
 * grabwindow_width 1920
 * grabwindow_height 1080
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5640_1920x1080_regs[] = {
	{0x3501, 0x45},
	{0x366e, 0x18},
	{0x3800, 0x01},
	{0x3801, 0x50},
	{0x3802, 0x01},
	{0x3803, 0xb8},
	{0x3804, 0x08},
	{0x3805, 0xef},
	{0x3806, 0x05},
	{0x3807, 0xf7},
	{0x3808, 0x07},
	{0x3809, 0x80},
	{0x380a, 0x04},
	{0x380b, 0x38},
	{0x380c, 0x02},
	{0x380d, 0xa0},
	{0x380e, 0x08},
	{0x380f, 0xb8},
	{0x3811, 0x06},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0x88},
	{0x3821, 0x00},
	{0x4501, 0x00},
	{0x4008, 0x04},
	{0x4009, 0x13},
	{REG_NULL, 0x00}
};

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 740(0x02e4)
 * framelength 1012(0x03f4)
 * grabwindow_width 1296
 * grabwindow_height 972
 * max_framerate 60fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5640_1296x972_regs[] = {
	{0x3501, 0x3e},
	{0x3611, 0x58},
	{0x3706, 0x24},
	{0x3714, 0x27},
	{0x3716, 0x00},
	{0x3717, 0x02},
	{0x37c3, 0xf0},
	{0x380d, 0xe4},
	{0x380e, 0x03},
	{0x380f, 0xf4},
	{0x3811, 0x00},
	{0x5000, 0x13},
	{REG_NULL, 0x00}
};

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 672(0x2a0)
 * framelength 2232(0x8b8)
 * grabwindow_width 1280
 * grabwindow_height 720
 * max_framerate 30fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5640_1280x720_regs[] = {
	{0x3501, 0x45},
	{0x366e, 0x0c},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x01},
	{0x3803, 0x00},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x06},
	{0x3807, 0xaf},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x02},
	{0x380b, 0xd0},
	{0x380c, 0x02},
	{0x380d, 0xa0},
	{0x380e, 0x08},
	{0x380f, 0xb8},
	{0x3811, 0x06},
	{0x3813, 0x02},
	{0x3814, 0x03},
	{0x3816, 0x03},
	{0x3817, 0x01},
	{0x3820, 0x8b},
	{0x3821, 0x01},
	{0x4501, 0x00},
	{0x4008, 0x02},
	{0x4009, 0x09},
	{REG_NULL, 0x00}
};

/*
 * Xclk 24Mhz
 * Pclk 45Mhz
 * linelength 672(0x2a0)
 * framelength 558(0x22e)
 * grabwindow_width 640
 * grabwindow_height 480
 * max_framerate 120fps
 * mipi_datarate per lane 840Mbps
 */
static const struct regval ov5640_640x480_regs[] = {
	{0x3501, 0x22},
	{0x366e, 0x0c},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x08},
	{0x3804, 0x0a},
	{0x3805, 0x3f},
	{0x3806, 0x07},
	{0x3807, 0xa7},
	{0x3808, 0x02},
	{0x3809, 0x80},
	{0x380a, 0x01},
	{0x380b, 0xe0},
	{0x380c, 0x02},
	{0x380d, 0xa0},
	{0x380e, 0x02},
	{0x380f, 0x2e},
	{0x3811, 0x06},
	{0x3813, 0x04},
	{0x3814, 0x07},
	{0x3816, 0x05},
	{0x3817, 0x03},
	{0x3820, 0x8d},
	{0x3821, 0x01},
	{0x4501, 0x00},
	{0x4008, 0x02},
	{0x4009, 0x09},
	{REG_NULL, 0x00}
};

static const struct ov5640_mode supported_modes[] = {
	{
		.width = 2592,
		.height = 1944,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0450,
		.hts_def = 0x02e4 * 4,
		.vts_def = 0x07e8,
		.reg_list = ov5640_2592x1944_regs,
	},
	{
		.width = 1920,
		.height = 1080,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0450,
		.hts_def = 0x02a0 * 4,
		.vts_def = 0x08b8,
		.reg_list = ov5640_1920x1080_regs,
	},
	{
		.width = 1296,
		.height = 972,
		.max_fps = {
			.numerator = 10000,
			.denominator = 600000,
		},
		.exp_def = 0x03e0,
		.hts_def = 0x02e4 * 4,
		.vts_def = 0x03f4,
		.reg_list = ov5640_1296x972_regs,
	},
	{
		.width = 1280,
		.height = 720,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0450,
		.hts_def = 0x02a0 * 4,
		.vts_def = 0x08b8,
		.reg_list = ov5640_1280x720_regs,
	},
	{
		.width = 640,
		.height = 480,
		.max_fps = {
			.numerator = 10000,
			.denominator = 1200000,
		},
		.exp_def = 0x0200,
		.hts_def = 0x02a0 * 4,
		.vts_def = 0x022e,
		.reg_list = ov5640_640x480_regs,
	},
};

#define ov5640_LINK_FREQ_420MHZ		420000000
static const s64 link_freq_menu_items[] = {
	ov5640_LINK_FREQ_420MHZ
};

// static const char * const ov5695_test_pattern_menu[] = {
// 	"Disabled",
// 	"Vertical Color Bar Type 1",
// 	"Vertical Color Bar Type 2",
// 	"Vertical Color Bar Type 3",
// 	"Vertical Color Bar Type 4"
// };

/* Write registers up to 4 at a time */
static int ov5640_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int ov5640_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u8 *data;
	u32 i, j = 0, k = 0;
	int ret = 0;
	struct i2c_msg *msg;

	msg = kmalloc((sizeof(struct i2c_msg) * I2C_MSG_MAX),
		GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	data = kmalloc((sizeof(unsigned char) * I2C_DATA_MAX),
		GFP_KERNEL);
	if (!data) {
		kfree(msg);
		return -ENOMEM;
	}

	for (i = 0; regs[i].addr != REG_NULL; i++) {
		(msg + j)->addr = client->addr;
		(msg + j)->flags = I2C_M_WR;
		(msg + j)->buf = (data + k);

		data[k + 0] = (u8)(regs[i].addr >> 8);
		data[k + 1] = (u8)(regs[i].addr & 0xFF);
		data[k + 2] = (u8)(regs[i].val & 0xFF);
		k = k + 3;
		(msg + j)->len = 3;

		if (j++ == (I2C_MSG_MAX - 1)) {
			ret = i2c_transfer(client->adapter, msg, j);
			if (ret < 0) {
				kfree(msg);
				kfree(data);
				return ret;
			}
			j = 0;
			k = 0;
		}
	}

	if (j != 0) {
		ret = i2c_transfer(client->adapter, msg, j);
		if (ret < 0) {
			kfree(msg);
			kfree(data);
			return ret;
		}
	}
	kfree(msg);
	kfree(data);

	return 0;
}

/* Read registers up to 4 at a time */
static int ov5640_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			   u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int ov5640_get_reso_dist(const struct ov5640_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct ov5640_mode *
ov5640_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = ov5640_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int ov5640_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	const struct ov5640_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&ov5640->mutex);

	mode = ov5640_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&ov5640->mutex);
		return -ENOTTY;
#endif
	} else {
		ov5640->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(ov5640->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov5640->vblank, vblank_def,
					 ov5640_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&ov5640->mutex);

	return 0;
}

static int ov5640_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	const struct ov5640_mode *mode = ov5640->cur_mode;

	mutex_lock(&ov5640->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&ov5640->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
	}
	mutex_unlock(&ov5640->mutex);

	return 0;
}

static int ov5640_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov5640_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int ov5640_enable_test_pattern(struct ov5640 *ov5640, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV5695_TEST_PATTERN_ENABLE;
	else
		val = OV5695_TEST_PATTERN_DISABLE;

	return ov5640_write_reg(ov5640->client, OV5695_REG_TEST_PATTERN,
				OV5695_REG_VALUE_08BIT, val);
}

static int ov5640_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	const struct ov5640_mode *mode = ov5640->cur_mode;

	mutex_lock(&ov5640->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&ov5640->mutex);

	return 0;
}

static void ov5640_get_module_inf(struct ov5640 *ov5640,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OV5695_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, ov5640->module_name,
		sizeof(inf->base.module));
	strscpy(inf->base.lens, ov5640->len_name, sizeof(inf->base.lens));
}

static long ov5640_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		ov5640_get_module_inf(ov5640, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = ov5640_write_reg(ov5640->client, OV5695_REG_CTRL_MODE,
				OV5695_REG_VALUE_08BIT, OV5695_MODE_STREAMING);
		else
			ret = ov5640_write_reg(ov5640->client, OV5695_REG_CTRL_MODE,
				OV5695_REG_VALUE_08BIT, OV5695_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long ov5640_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = ov5640_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = ov5640_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = ov5640_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __ov5640_start_stream(struct ov5640 *ov5640)
{
	int ret;

	ret = ov5640_write_array(ov5640->client, ov5640->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	mutex_unlock(&ov5640->mutex);
	ret = v4l2_ctrl_handler_setup(&ov5640->ctrl_handler);
	mutex_lock(&ov5640->mutex);
	if (ret)
		return ret;

	return ov5640_write_reg(ov5640->client, OV5695_REG_CTRL_MODE,
				OV5695_REG_VALUE_08BIT, OV5695_MODE_STREAMING);
}

static int __ov5640_stop_stream(struct ov5640 *ov5640)
{
	return ov5640_write_reg(ov5640->client, OV5695_REG_CTRL_MODE,
				OV5695_REG_VALUE_08BIT, OV5695_MODE_SW_STANDBY);
}

static int ov5640_s_stream(struct v4l2_subdev *sd, int on)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	struct i2c_client *client = ov5640->client;
	int ret = 0;

	mutex_lock(&ov5640->mutex);
	on = !!on;
	if (on == ov5640->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __ov5640_start_stream(ov5640);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__ov5640_stop_stream(ov5640);
		pm_runtime_put(&client->dev);
	}

	ov5640->streaming = on;

unlock_and_return:
	mutex_unlock(&ov5640->mutex);

	return ret;
}

static int ov5640_s_power(struct v4l2_subdev *sd, int on)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	struct i2c_client *client = ov5640->client;
	int ret = 0;

	mutex_lock(&ov5640->mutex);

	/* If the power state is not modified - no work to do. */
	if (ov5640->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = ov5640_write_array(ov5640->client, ov5640_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ov5640->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		ov5640->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&ov5640->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 ov5640_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV5695_XVCLK_FREQ / 1000 / 1000);
}

static int __ov5640_power_on(struct ov5640 *ov5640)
{
	int ret;
	u32 delay_us;
	struct device *dev = &ov5640->client->dev;

	ret = clk_set_rate(ov5640->xvclk, OV5695_XVCLK_FREQ);
	if (ret < 0) {
		dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
		return ret;
	}
	if (clk_get_rate(ov5640->xvclk) != OV5695_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(ov5640->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	if (!IS_ERR(ov5640->reset_gpio))
		gpiod_set_value_cansleep(ov5640->reset_gpio, 1);

	ret = regulator_bulk_enable(ov5640_NUM_SUPPLIES, ov5640->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(ov5640->reset_gpio))
		gpiod_set_value_cansleep(ov5640->reset_gpio, 0);

	if (!IS_ERR(ov5640->pwdn_gpio))
		gpiod_set_value_cansleep(ov5640->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = ov5640_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(ov5640->xvclk);

	return ret;
}

static void __ov5640_power_off(struct ov5640 *ov5640)
{
	if (!IS_ERR(ov5640->pwdn_gpio))
		gpiod_set_value_cansleep(ov5640->pwdn_gpio, 0);
	clk_disable_unprepare(ov5640->xvclk);
	if (!IS_ERR(ov5640->reset_gpio))
		gpiod_set_value_cansleep(ov5640->reset_gpio, 1);
	regulator_bulk_disable(ov5640_NUM_SUPPLIES, ov5640->supplies);
}

static int ov5640_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5640 *ov5640 = to_ov5640(sd);

	return __ov5640_power_on(ov5640);
}

static int ov5640_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5640 *ov5640 = to_ov5640(sd);

	__ov5640_power_off(ov5640);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int ov5640_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov5640 *ov5640 = to_ov5640(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct ov5640_mode *def_mode = &supported_modes[0];

	mutex_lock(&ov5640->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&ov5640->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int ov5640_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_pad_config *cfg,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fie->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	return 0;
}

static int ov5640_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	u32 val = 0;

	val = 1 << (ov5640_LANES - 1) |
	      V4L2_MBUS_CSI2_CHANNEL_0 |
	      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;
	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static const struct dev_pm_ops ov5640_pm_ops = {
	SET_RUNTIME_PM_OPS(ov5640_runtime_suspend,
			   ov5640_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops ov5640_internal_ops = {
	.open = ov5640_open,
};
#endif

static const struct v4l2_subdev_core_ops ov5640_core_ops = {
	.s_power = ov5640_s_power,
	.ioctl = ov5640_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = ov5640_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops ov5640_video_ops = {
	.s_stream = ov5640_s_stream,
	.g_frame_interval = ov5640_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops ov5640_pad_ops = {
	.enum_mbus_code = ov5640_enum_mbus_code,
	.enum_frame_size = ov5640_enum_frame_sizes,
	.enum_frame_interval = ov5640_enum_frame_interval,
	.get_fmt = ov5640_get_fmt,
	.set_fmt = ov5640_set_fmt,
	.get_mbus_config = ov5640_g_mbus_config,
};

static const struct v4l2_subdev_ops ov5640_subdev_ops = {
	.core	= &ov5640_core_ops,
	.video	= &ov5640_video_ops,
	.pad	= &ov5640_pad_ops,
};

static int ov5640_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov5640 *ov5640 = container_of(ctrl->handler,
					     struct ov5640, ctrl_handler);
	struct i2c_client *client = ov5640->client;
	s64 max;
	int ret = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = ov5640->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(ov5640->exposure,
					 ov5640->exposure->minimum, max,
					 ov5640->exposure->step,
					 ov5640->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = ov5640_write_reg(ov5640->client, ov5640_REG_EXPOSURE,
				       ov5640_REG_VALUE_24BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov5640_write_reg(ov5640->client, ov5640_REG_ANALOG_GAIN,
				       ov5640_REG_VALUE_08BIT, ctrl->val);
		break;
	case V4L2_CID_DIGITAL_GAIN:
		ret = ov5640_write_reg(ov5640->client, ov5640_REG_DIGI_GAIN_L,
				       ov5640_REG_VALUE_08BIT,
				       ctrl->val & ov5640_DIGI_GAIN_L_MASK);
		ret |= ov5640_write_reg(ov5640->client, ov5640_REG_DIGI_GAIN_H,
				       ov5640_REG_VALUE_08BIT,
				       ctrl->val >> ov5640_DIGI_GAIN_H_SHIFT);
		break;
	case V4L2_CID_VBLANK:
		ret = ov5640_write_reg(ov5640->client, ov5640_REG_VTS,
				       ov5640_REG_VALUE_16BIT,
				       ctrl->val + ov5640->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = ov5640_enable_test_pattern(ov5640, ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov5640_ctrl_ops = {
	.s_ctrl = ov5640_set_ctrl,
};

static int ov5640_initialize_controls(struct ov5640 *ov5640)
{
	const struct ov5640_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &ov5640->ctrl_handler;
	mode = ov5640->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 8);
	if (ret)
		return ret;
	handler->lock = &ov5640->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, ov5640_PIXEL_RATE, 1, ov5640_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	ov5640->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (ov5640->hblank)
		ov5640->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	ov5640->vblank = v4l2_ctrl_new_std(handler, &ov5640_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				ov5640_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	ov5640->exposure = v4l2_ctrl_new_std(handler, &ov5640_ctrl_ops,
				V4L2_CID_EXPOSURE, ov5640_EXPOSURE_MIN,
				exposure_max, ov5640_EXPOSURE_STEP,
				mode->exp_def);

	ov5640->anal_gain = v4l2_ctrl_new_std(handler, &ov5640_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, ANALOG_GAIN_MIN,
				ANALOG_GAIN_MAX, ANALOG_GAIN_STEP,
				ANALOG_GAIN_DEFAULT);

	/* Digital gain */
	ov5640->digi_gain = v4l2_ctrl_new_std(handler, &ov5640_ctrl_ops,
				V4L2_CID_DIGITAL_GAIN, ov5640_DIGI_GAIN_MIN,
				ov5640_DIGI_GAIN_MAX, ov5640_DIGI_GAIN_STEP,
				ov5640_DIGI_GAIN_DEFAULT);

	ov5640->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&ov5640_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(ov5640_test_pattern_menu) - 1,
				0, 0, ov5640_test_pattern_menu);

	if (handler->error) {
		ret = handler->error;
		dev_err(&ov5640->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	ov5640->subdev.ctrl_handler = handler;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int ov5640_check_sensor_id(struct ov5640 *ov5640,
				  struct i2c_client *client)
{
	struct device *dev = &ov5640->client->dev;
	u32 id = 0;
	int ret;

	ret = ov5640_read_reg(client, ov5640_REG_CHIP_ID,
			      ov5640_REG_VALUE_24BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int ov5640_configure_regulators(struct ov5640 *ov5640)
{
	int i;

	for (i = 0; i < ov5640_NUM_SUPPLIES; i++)
		ov5640->supplies[i].supply = ov5640_supply_names[i];

	return devm_regulator_bulk_get(&ov5640->client->dev,
				       ov5640_NUM_SUPPLIES,
				       ov5640->supplies);
}

static int ov5640_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct ov5640 *ov5640;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	ov5640 = devm_kzalloc(dev, sizeof(*ov5640), GFP_KERNEL);
	if (!ov5640)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &ov5640->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &ov5640->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &ov5640->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &ov5640->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ov5640->client = client;
	ov5640->cur_mode = &supported_modes[0];

	ov5640->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(ov5640->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	ov5640->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ov5640->reset_gpio)) {
		dev_warn(dev, "Failed to get reset-gpios\n");
	}

	ov5640->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(ov5640->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = ov5640_configure_regulators(ov5640);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&ov5640->mutex);

	sd = &ov5640->subdev;
	v4l2_i2c_subdev_init(sd, client, &ov5640_subdev_ops);
	ret = ov5640_initialize_controls(ov5640);
	if (ret)
		goto err_destroy_mutex;

	ret = __ov5640_power_on(ov5640);
	if (ret)
		goto err_free_handler;

	ret = ov5640_check_sensor_id(ov5640, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &ov5640_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	ov5640->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &ov5640->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(ov5640->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 ov5640->module_index, facing,
		 ov5640_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__ov5640_power_off(ov5640);
err_free_handler:
	v4l2_ctrl_handler_free(&ov5640->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&ov5640->mutex);

	return ret;
}

static int ov5640_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov5640 *ov5640 = to_ov5640(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&ov5640->ctrl_handler);
	mutex_destroy(&ov5640->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__ov5640_power_off(ov5640);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id ov5640_of_match[] = {
	{ .compatible = "ovti,ov5640" },
	{},
};
MODULE_DEVICE_TABLE(of, ov5640_of_match);
#endif

static const struct i2c_device_id ov5640_match_id[] = {
	{ "ovti,ov5640", 0 },
	{ },
};

static struct i2c_driver ov5640_i2c_driver = {
	.driver = {
		.name = ov5640_NAME,
		.pm = &ov5640_pm_ops,
		.of_match_table = of_match_ptr(ov5640_of_match),
	},
	.probe		= &ov5640_probe,
	.remove		= &ov5640_remove,
	.id_table	= ov5640_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&ov5640_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&ov5640_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision ov5640 sensor driver");
MODULE_LICENSE("GPL v2");
