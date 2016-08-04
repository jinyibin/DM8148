//#define DEBUG
/*
 * Copyright (C) 2007-2009 Texas Instruments Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#define DEBUG
#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/device.h>
#include <linux/workqueue.h>

#include <media/v4l2-ioctl.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/davinci/videohd.h> // For HD std (V4L2_STD_1080I, etc)
#include <asm/uaccess.h>

#include "adv7611.h"

/* Debug functions */
static int debug = 2;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "Debug level (0-2)");


/* Function Prototypes */
static int adv7611_i2c_read_reg(struct i2c_client *client, u8 addr, u8 reg, u8 * val);
static int adv7611_i2c_write_reg(struct i2c_client *client, u8 addr, u8 reg, u8 val);
static int adv7611_querystd(struct v4l2_subdev *sd, v4l2_std_id *id);
static int adv7611_s_std(struct v4l2_subdev *sd, v4l2_std_id std);

static int adv7611_s_stream(struct v4l2_subdev *sd, int enable);

static int adv7611_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int adv7611_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl);
static int adv7611_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc);

static int adv7611_initialize(struct v4l2_subdev *sd);
static int adv7611_deinitialize(struct v4l2_subdev *sd);
static int adv7611_s_dv_preset(struct v4l2_subdev *sd,struct v4l2_dv_preset *dv_preset);
static int adv7611_query_dv_preset(struct v4l2_subdev *sd,struct v4l2_dv_preset *qpreset);
static int adv7611_enum_dv_presets(struct v4l2_subdev *sd,struct v4l2_dv_enum_preset *preset);

#if 0
static struct v4l2_standard adv7611_standards[ADV7611_MAX_NO_STANDARDS] = {
	{
		.index = 0,
		.id = V4L2_STD_720P_60,
		.name = "720P-60",
		.frameperiod = {1, 60},
		.framelines = 750
	},
	{
		.index = 1,
		.id = V4L2_STD_1080I_60,
		.name = "1080I-30",
		.frameperiod = {1, 30},
		.framelines = 1125
	},
	{
		.index = 2,
		.id = V4L2_STD_1080I_50,
		.name = "1080I-25",
		.frameperiod = {1, 25},
		.framelines = 1125
	},
	{
		.index = 3,
		.id = V4L2_STD_720P_50,
		.name = "720P-50",
		.frameperiod = {1, 50},
		.framelines = 750
	},
	{
		.index = 4,
		.id = V4L2_STD_1080P_25,
		.name = "1080P-25",
		.frameperiod = {1, 25},
		.framelines = 1125
	},
	{
		.index = 5,
		.id = V4L2_STD_1080P_30,
		.name = "1080P-30",
		.frameperiod = {1, 30},
		.framelines = 1125
	},
	{
		.index = 6,
		.id = V4L2_STD_1080P_24,
		.name = "1080P-24",
		.frameperiod = {1, 24},
		.framelines = 1125
	},
	{
		.index = 7,
		.id = V4L2_STD_525P_60,
		.name = "480P-60",
		.frameperiod = {1, 60},
		.framelines = 525
	},
	{
		.index = 8,
		.id = V4L2_STD_625P_50,
		.name = "576P-50",
		.frameperiod = {1, 50},
		.framelines = 625
	},
	{
		.index = 9,
		.id = V4L2_STD_525_60,
		.name = "NTSC",
		.frameperiod = {1001, 30000},
		.framelines = 525
	},
	{
		.index = 10,
		.id = V4L2_STD_625_50,
		.name = "PAL",
		.frameperiod = {1, 25},
		.framelines = 625
	},
	{
		.index = 11,
		.id = V4L2_STD_1080P_50,
		.name = "1080P-50",
		.frameperiod = {1, 50},
		.framelines = 1125
	},
	{
		.index = 12,
		.id = V4L2_STD_1080P_60,
		.name = "1080P-60",
		.frameperiod = {1, 60},
		.framelines = 1125
	},

};
#endif

struct adv7611_channel {
	struct v4l2_subdev    sd;
        struct work_struct    work;
        int                   ch_id;

        int                   streaming;

        s32                   output_format; // see ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_*

        unsigned char IO_ADDR;
        unsigned char DPLL_ADDR;
        unsigned char CEC_ADDR;
        unsigned char INFOFRAME_ADDR;
        unsigned char KSV_ADDR;
        unsigned char EDID_ADDR;
        unsigned char HDMI_ADDR;
        unsigned char CP_ADDR;
};


static const struct v4l2_subdev_video_ops adv7611_video_ops = {
	.querystd = adv7611_querystd,
	.s_stream = adv7611_s_stream,
	.s_dv_preset = adv7611_s_dv_preset,
        .query_dv_preset = adv7611_query_dv_preset,
        .enum_dv_presets = adv7611_enum_dv_presets,
//	.g_input_status = adv7611_g_input_status,
};

static const struct v4l2_subdev_core_ops adv7611_core_ops = {
        .g_chip_ident = NULL,
	.g_ctrl = adv7611_g_ctrl,
	.s_ctrl = adv7611_s_ctrl,
	.queryctrl = adv7611_queryctrl,
	.s_std = adv7611_s_std,
};

static const struct v4l2_subdev_ops adv7611_ops = {
	.core = &adv7611_core_ops,
	.video = &adv7611_video_ops,
};


static inline struct adv7611_channel *to_adv7611(struct v4l2_subdev *sd)
{
	return container_of(sd, struct adv7611_channel, sd);
}

static int adv7611_query_dv_preset(struct v4l2_subdev *sd,
						struct v4l2_dv_preset *qpreset)
{
	struct adv7611_channel *channel = (to_adv7611(sd));
	qpreset->preset = V4L2_DV_1080P30;
        v4l2_dbg(2, debug, sd, "detected preset: %d\n", qpreset->preset);
        return 0;
}
static int adv7611_enum_dv_presets(struct v4l2_subdev *sd,struct v4l2_dv_enum_preset *preset)
{
	/* Check requested format index is within range */
	//if (preset->index >= NUM_PRESETS)
v4l2_dbg(2, debug, sd, "adv7611_enum_dv_presets");
if (preset->index >= 1)
		return -EINVAL;

	//return v4l_fill_dv_preset_info(tvp7002_presets[preset->index].preset, preset);
        preset->preset = V4L2_DV_1080P30;
        preset->width = 1920;
        preset->height = 1080;
        
        strlcpy(preset->name,"1080p@30", 8);
        
        return 0;
}

/* adv7611_initialize :
 * This function will set the video format standard
 */
static int adv7611_initialize(struct v4l2_subdev *sd)
{
	int err = 0;
	struct i2c_client *ch_client = NULL;
        struct adv7611_channel *channel = (to_adv7611(sd));

        struct {
                adv7611_i2caddr_t i2caddr;
                u8                reg;
                u8                val;
        } *init_cur, init_sequence[] = {
		/* programmable I2C map addresses */
		{ ADV7611_I2CADDR_IO,   0xF4, 0x80 }, /* CEC Map I2C address */
		{ ADV7611_I2CADDR_IO,	0xF5, 0x7C }, /* INFOFRAME Map I2C address */
		{ ADV7611_I2CADDR_IO,	0xF8, 0x4C }, /* DPLL Map I2C address */
		{ ADV7611_I2CADDR_IO,	0xF9, 0x64 }, /* KSV Map I2C address */
		{ ADV7611_I2CADDR_IO,	0xFA, 0x6C }, /* EDID Map I2C address */
		{ ADV7611_I2CADDR_IO,   0xFB, 0x68 }, /* HDMI Map I2C address */
		{ ADV7611_I2CADDR_IO,   0xFD, 0x44 }, /* CP Map I2C address */

		/* INITIALIZATION SETTINGS FOR HDMI MODE */
		{ ADV7611_I2CADDR_CP,   0x6C, 0x01 }, /* ADI required setting */
		{ ADV7611_I2CADDR_HDMI, 0x9B, 0x03 }, /* ADI required setting */
		{ ADV7611_I2CADDR_HDMI, 0x6F, 0x08 }, // ADI recommended setting
		{ ADV7611_I2CADDR_HDMI, 0x85, 0x1F }, // ADI recommended setting
		{ ADV7611_I2CADDR_HDMI, 0x87, 0x70 }, // ADI recommended setting
		{ ADV7611_I2CADDR_HDMI, 0x57, 0xDA }, // ADI recommended setting
		{ ADV7611_I2CADDR_HDMI, 0x58, 0x01 }, // ADI recommended setting
		{ ADV7611_I2CADDR_HDMI, 0x03, 0x98 }, // Set DIS_I2C_ZERO_COMPR 0x03[7]=1
		{ ADV7611_I2CADDR_HDMI, 0x4C, 0x44 }, // Set NEW_VS_PARAM 0x44[2]=1
		/* For non-fast switching applications, ADI required setting */
		{ ADV7611_I2CADDR_HDMI, 0xC1, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC2, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC3, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC4, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC5, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC6, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC7, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC8, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xC9, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xCA, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xCB, 0x01 },
		{ ADV7611_I2CADDR_HDMI, 0xCC, 0x01 },              



		/* Free-run Operation */
		//{ ADV7611_I2CADDR_IO, 0x01, 0x06 }, // Set PRIM_MODE to the desired free-run standard: HDMI graphics
		//{ ADV7611_I2CADDR_IO, 0x00, 0x01 }, // Set VID_STD to the desired free-run standard: 800x600@60
		//{ ADV7611_I2CADDR_IO, 0x01, 0x06 }, // Set VFREQ to the frequency of the desired free-run standard: 60 Hz
		{ ADV7611_I2CADDR_CP, 0xC9, 0x05 }, // Set DIS_AUTO_PARAM_BUFF to slave free-run parameters from PRIM_MODE and VID_STD
		{ ADV7611_I2CADDR_CP, 0xBF, 0x02}, // Enable auto CP free-run mode,insert blue automaticly


                //{ ADV7611_I2CADDR_IO,	0x00, 0x0b },//720*576
                { ADV7611_I2CADDR_IO,	0x00, 0x1e },//1920*1080
                //{ ADV7611_I2CADDR_IO,	0x00, 0x13 },//1280*720
		{ ADV7611_I2CADDR_IO,	0x01, 0x25 },//30Hz,Comp
		//{ ADV7611_I2CADDR_IO,	0x01, 0x05 },//60Hz,Comp
		{ ADV7611_I2CADDR_IO,	0x02, 0xf5 },// YUV SDR out
		//{ ADV7611_I2CADDR_IO,	0x02, 0xf2 },// RGB SDR out
		//{ ADV7611_I2CADDR_IO,	0x03, 0x40 },//24bit 4:4:4
		{ ADV7611_I2CADDR_IO,	0x03, 0x80 },//16bit 4:2:2
		{ ADV7611_I2CADDR_IO,	0x05, 0x2c },//embed sync
		{ ADV7611_I2CADDR_IO,	0x06, 0xa6 }, // Invert HS, VS pins

		/* Bring chip out of powerdown and disable tristate */
		{ ADV7611_I2CADDR_IO,	0x0b, 0x44 },
		{ ADV7611_I2CADDR_IO,	0x0c, 0x42 },
		{ ADV7611_I2CADDR_IO,	0x14, 0x7f },//drive strength max for 1080p
		//{ ADV7611_I2CADDR_IO,	0x14, 0x6a },//drive strength media
		{ ADV7611_I2CADDR_IO,	0x15, 0x80 },//pin active

		/* LLC DLL enable */
//		  { ADV7611_I2CADDR_IO,   0x19, 0x83 },
		{ ADV7611_I2CADDR_IO,	0x19, 0x94 },//enable DLL
		{ ADV7611_I2CADDR_IO,	0x33, 0x40 },//mux the dll output on LLC output

                /* Force HDMI free run */
                { ADV7611_I2CADDR_CP,   0xba, 0x1 },//enable free run in HDMI mode,mode 1,
                                                     //free run when no TMDS clock,or resolution diff

                /* Disable HDCP 1.1*/
                { ADV7611_I2CADDR_KSV,  0x40, 0x81 },

                /* ADI recommended writes */
                { ADV7611_I2CADDR_HDMI, 0x9B, 0x03 },
                { ADV7611_I2CADDR_HDMI, 0xC1, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC2, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC3, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC4, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC5, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC6, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC7, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC8, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC9, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCA, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCB, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCC, 0x01 },

                { ADV7611_I2CADDR_HDMI, 0x00, 0x00 }, // Set HDMI port A
                { ADV7611_I2CADDR_HDMI, 0x83, 0xFE }, // Enable clock terminator for port A
                { ADV7611_I2CADDR_HDMI, 0x6F, 0x08 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x85, 0x1F }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x87, 0x70 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x8D, 0x04 }, // LFG
                { ADV7611_I2CADDR_HDMI, 0x8E, 0x1E }, // HFG
                { ADV7611_I2CADDR_HDMI, 0x1A, 0x8A }, // unmute audio
                { ADV7611_I2CADDR_HDMI, 0x57, 0xDA }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x58, 0x01 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x75, 0x10 }, // DDC drive strength



		/* Hot Plug Assert,deassert hpa before setting EDID */
		{ ADV7611_I2CADDR_IO, 0x20, 0x70 }, // Manually deassert hot plug on port A,set low
                { ADV7611_I2CADDR_HDMI, 0x6C, 0xa3 },// HPA manual control

                { ADV7611_I2CADDR_KSV,  0x74, 0x00 },//disable EDID
                { ADV7611_I2CADDR_EDID,  0x0, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x01,0xff },
                { ADV7611_I2CADDR_EDID,  0x02, 0xff },
                { ADV7611_I2CADDR_EDID,  0x03, 0xff },
                { ADV7611_I2CADDR_EDID,  0x04, 0xff },
                { ADV7611_I2CADDR_EDID,  0x05, 0xff },
                { ADV7611_I2CADDR_EDID,  0x06, 0xff },
                { ADV7611_I2CADDR_EDID,  0x07, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x08, 0x6 },
                { ADV7611_I2CADDR_EDID,  0x09, 0x8f },
                { ADV7611_I2CADDR_EDID,  0x0a, 0x7 },
                { ADV7611_I2CADDR_EDID,  0x0b, 0x11 },
                { ADV7611_I2CADDR_EDID,  0x0c, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x0d, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x0e, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x0f, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x10, 0x17 },
                { ADV7611_I2CADDR_EDID,  0x11, 0x11 },
                { ADV7611_I2CADDR_EDID,  0x12, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x13, 0x3 },
                { ADV7611_I2CADDR_EDID,  0x14, 0x80 },
                { ADV7611_I2CADDR_EDID,  0x15, 0xc },
                { ADV7611_I2CADDR_EDID,  0x16, 0x9 },
                { ADV7611_I2CADDR_EDID,  0x17, 0x78 },
                { ADV7611_I2CADDR_EDID,  0x18, 0xa },
                { ADV7611_I2CADDR_EDID,  0x19, 0x1e },
                { ADV7611_I2CADDR_EDID,  0x1a, 0xac },
                { ADV7611_I2CADDR_EDID,  0x1b, 0x98 },
                { ADV7611_I2CADDR_EDID,  0x1c, 0x59 },
                { ADV7611_I2CADDR_EDID,  0x1d, 0x56 },
                { ADV7611_I2CADDR_EDID,  0x1e, 0x85 },
                { ADV7611_I2CADDR_EDID,  0x1f, 0x28 },
                { ADV7611_I2CADDR_EDID,  0x20, 0x29 },
                { ADV7611_I2CADDR_EDID,  0x21, 0x52 },
                { ADV7611_I2CADDR_EDID,  0x22, 0x57 },
                { ADV7611_I2CADDR_EDID,  0x23, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x24, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x25, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x26, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x27, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x28, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x29, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x2a, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x2b, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x2c, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x2d, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x2e, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x2f, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x30, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x31, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x32, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x33, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x34, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x35, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x36, 0x8c },
                { ADV7611_I2CADDR_EDID,  0x37, 0xa },
                { ADV7611_I2CADDR_EDID,  0x38, 0xd0 },
                { ADV7611_I2CADDR_EDID,  0x39, 0x8a },
                { ADV7611_I2CADDR_EDID,  0x3a, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x3b, 0xe0 },
                { ADV7611_I2CADDR_EDID,  0x3c, 0x2d },
                { ADV7611_I2CADDR_EDID,  0x3d, 0x10 },
                { ADV7611_I2CADDR_EDID,  0x3e, 0x10 },
                { ADV7611_I2CADDR_EDID,  0x3f, 0x3e },
                { ADV7611_I2CADDR_EDID,  0x40, 0x96 },
                { ADV7611_I2CADDR_EDID,  0x41, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x42, 0x81 },
                { ADV7611_I2CADDR_EDID,  0x43, 0x60 },
                { ADV7611_I2CADDR_EDID,  0x44, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x45, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x46, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x47, 0x18 },
                { ADV7611_I2CADDR_EDID,  0x48, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x49, 0x1d },
                { ADV7611_I2CADDR_EDID,  0x4a, 0x80 },
                { ADV7611_I2CADDR_EDID,  0x4b, 0x18 },
                { ADV7611_I2CADDR_EDID,  0x4c, 0x71 },
                { ADV7611_I2CADDR_EDID,  0x4d, 0x1c },
                { ADV7611_I2CADDR_EDID,  0x4e, 0x16 },
                { ADV7611_I2CADDR_EDID,  0x4f, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x50, 0x58 },
                { ADV7611_I2CADDR_EDID,  0x51, 0x2c },
                { ADV7611_I2CADDR_EDID,  0x52, 0x25 },
                { ADV7611_I2CADDR_EDID,  0x53, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x54, 0x81 },
                { ADV7611_I2CADDR_EDID,  0x55, 0x49 },
                { ADV7611_I2CADDR_EDID,  0x56, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x57, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x58, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x59, 0x9e },
                { ADV7611_I2CADDR_EDID,  0x5a, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x5b, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x5c, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x5d, 0xfc },
                { ADV7611_I2CADDR_EDID,  0x5e, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x5f, 0x56 },
                { ADV7611_I2CADDR_EDID,  0x60, 0x41 },
                { ADV7611_I2CADDR_EDID,  0x61, 0x2d },
                { ADV7611_I2CADDR_EDID,  0x62, 0x31 },
                { ADV7611_I2CADDR_EDID,  0x63, 0x38 },
                { ADV7611_I2CADDR_EDID,  0x64, 0x30 },
                { ADV7611_I2CADDR_EDID,  0x65, 0x39 },
                { ADV7611_I2CADDR_EDID,  0x66, 0x41 },
                { ADV7611_I2CADDR_EDID,  0x67, 0xa },
                { ADV7611_I2CADDR_EDID,  0x68, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x69, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x6a, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x6b, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x6c, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x6d, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x6e, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x6f, 0xfd },
                { ADV7611_I2CADDR_EDID,  0x70, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x71, 0x17 },
                { ADV7611_I2CADDR_EDID,  0x72, 0x3d },
                { ADV7611_I2CADDR_EDID,  0x73, 0xd },
                { ADV7611_I2CADDR_EDID,  0x74, 0x2e },
                { ADV7611_I2CADDR_EDID,  0x75, 0x11 },
                { ADV7611_I2CADDR_EDID,  0x76, 0x0 },
                { ADV7611_I2CADDR_EDID,  0x77, 0xa },
                { ADV7611_I2CADDR_EDID,  0x78, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x79, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x7a, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x7b, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x7c, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x7d, 0x20 },
                { ADV7611_I2CADDR_EDID,  0x7e, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x7f, 0x1c },
                { ADV7611_I2CADDR_EDID,  0x80, 0x2 },
                { ADV7611_I2CADDR_EDID,  0x81, 0x3 },
                { ADV7611_I2CADDR_EDID,  0x82, 0x34 },
                { ADV7611_I2CADDR_EDID,  0x83, 0x71 },
                { ADV7611_I2CADDR_EDID,  0x84, 0x4d },
                { ADV7611_I2CADDR_EDID,  0x85, 0x82 },
                { ADV7611_I2CADDR_EDID,  0x86, 0x5 },
                { ADV7611_I2CADDR_EDID,  0x87, 0x4 },
                { ADV7611_I2CADDR_EDID,  0x88, 0x1 },
                { ADV7611_I2CADDR_EDID,  0x89, 0x10 },
                { ADV7611_I2CADDR_EDID,  0x8a, 0x11},
                { ADV7611_I2CADDR_EDID,  0x8b, 0x14 },
                { ADV7611_I2CADDR_EDID,  0x8c, 0x13 },
                { ADV7611_I2CADDR_EDID,  0x8d, 0x1f },
                { ADV7611_I2CADDR_EDID,  0x8e, 0x6 },
                { ADV7611_I2CADDR_EDID,  0x8f, 0x15 },
                { ADV7611_I2CADDR_EDID,  0x90, 0x3 },
                { ADV7611_I2CADDR_EDID,  0x91, 0x12 },
                { ADV7611_I2CADDR_EDID,  0x92, 0x35 },
                { ADV7611_I2CADDR_EDID,  0x93, 0xf },
                { ADV7611_I2CADDR_EDID,  0x94, 0x7f },
                { ADV7611_I2CADDR_EDID,  0x95, 0x7 },
                { ADV7611_I2CADDR_EDID,  0x96, 0x17 },
                { ADV7611_I2CADDR_EDID,  0x97, 0x1f },
                { ADV7611_I2CADDR_EDID,  0x98, 0x38 },
                { ADV7611_I2CADDR_EDID,  0x99, 0x1f },
                { ADV7611_I2CADDR_EDID,  0x9a, 0x7 },
                { ADV7611_I2CADDR_EDID,  0x9b, 0x30 },
                { ADV7611_I2CADDR_EDID,  0x9c, 0x2f },
                { ADV7611_I2CADDR_EDID,  0x9d, 0x7 },
                { ADV7611_I2CADDR_EDID,  0x9e, 0x72 },
                { ADV7611_I2CADDR_EDID,  0x9f, 0x3f },
                { ADV7611_I2CADDR_EDID,  0xa0, 0x7f },
                { ADV7611_I2CADDR_EDID,  0xa1, 0x72 },
                { ADV7611_I2CADDR_EDID,  0xa2, 0x57 },
                { ADV7611_I2CADDR_EDID,  0xa3, 0x7f },
                { ADV7611_I2CADDR_EDID,  0xa4, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xa5, 0x37 },
                { ADV7611_I2CADDR_EDID,  0xa6, 0x7f },
                { ADV7611_I2CADDR_EDID,  0xa7, 0x72 },
                { ADV7611_I2CADDR_EDID,  0xa8, 0x83 },
                { ADV7611_I2CADDR_EDID,  0xa9, 0x4f },
                { ADV7611_I2CADDR_EDID,  0xaa, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xab, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xac, 0x67 },
                { ADV7611_I2CADDR_EDID,  0xad, 0x3 },
                { ADV7611_I2CADDR_EDID,  0xae, 0xc },
                { ADV7611_I2CADDR_EDID,  0xaf, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xb0, 0x10 },
                { ADV7611_I2CADDR_EDID,  0xb1, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xb2, 0x88 },
                { ADV7611_I2CADDR_EDID,  0xb3, 0x2d },
                { ADV7611_I2CADDR_EDID,  0xb4, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xb5, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xb6, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xb7, 0xff },
                { ADV7611_I2CADDR_EDID,  0xb8, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xb9, 0xa },
                { ADV7611_I2CADDR_EDID,  0xba, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xbb, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xbc, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xbd, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xbe, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xbf, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xc0, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xc1, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xc2, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xc3, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xc4, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xc5, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xc6, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xc7, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xc8, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xc9, 0xff },
                { ADV7611_I2CADDR_EDID,  0xca, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xcb, 0xa },
                { ADV7611_I2CADDR_EDID,  0xcc, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xcd, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xce, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xcf, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd0, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd1, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd2, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd3, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd4, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd5, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd6, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd7, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xd8, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xd9, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xda, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xdb, 0xff },
                { ADV7611_I2CADDR_EDID,  0xdc, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xdd, 0xa },
                { ADV7611_I2CADDR_EDID,  0xde, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xdf, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe0, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe1, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe2, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe3, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe4, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe5, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe6, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe7, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe8, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xe9, 0x20 },
                { ADV7611_I2CADDR_EDID,  0xea, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xeb, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xec, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xed, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xee, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xef, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf0, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf1, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf2, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf3, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf4, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf5, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf6, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf7, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf8, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xf9, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xfa, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xfb, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xfc, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xfd, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xfe, 0x0 },
                { ADV7611_I2CADDR_EDID,  0xff, 0xda },

                { ADV7611_I2CADDR_KSV,  0x74, 0x03 },//enable EDID
                { ADV7611_I2CADDR_IO, 0x20, 0xf0 }, // Manually assert hot plug on port A,set high
                { ADV7611_I2CADDR_HDMI, 0x6C, 0xa3 },// HPA auto control
                { ADV7611_I2CADDR_NONE, 0, 0 },
#if 0
		/* Hot Plug Assert */
		//{ ADV7611_I2CADDR_IO, 0x20, 0xF8 }, // Manually assert hot plug on port A

		/* Free-run Operation */
		{ ADV7611_I2CADDR_IO, 0x01, 0x06 }, // Set PRIM_MODE to the desired free-run standard: HDMI graphics
		{ ADV7611_I2CADDR_IO, 0x00, 0x01 }, // Set VID_STD to the desired free-run standard: 800x600@60
		{ ADV7611_I2CADDR_IO, 0x01, 0x06 }, // Set VFREQ to the frequency of the desired free-run standard: 60 Hz
		{ ADV7611_I2CADDR_CP, 0xC9, 0x05 }, // Set DIS_AUTO_PARAM_BUFF to slave free-run parameters from PRIM_MODE and VID_STD
		{ ADV7611_I2CADDR_CP, 0xBF, 0x01 }, // Enable free-run mode
#endif

#if 0
		{ ADV7611_I2CADDR_IO,	0x00, 0x1e },//1920*1080
                { ADV7611_I2CADDR_IO,	0x01, 0x06 },
		{ ADV7611_I2CADDR_IO,	0x02, 0xf5 }, // RGB4 SDR out
		{ ADV7611_I2CADDR_IO,	0x03, 0x40 },
		{ ADV7611_I2CADDR_IO,	0x05, 0x2c },
		{ ADV7611_I2CADDR_IO,	0x06, 0xa6 }, // Invert HS, VS pins

		/* Bring chip out of powerdown and disable tristate */
		{ ADV7611_I2CADDR_IO,	0x0b, 0x44 },
		{ ADV7611_I2CADDR_IO,	0x0c, 0x42 },
		{ ADV7611_I2CADDR_IO,	0x14, 0x3f },
		{ ADV7611_I2CADDR_IO,	0x15, 0xBE },

		/* LLC DLL enable */
//		  { ADV7611_I2CADDR_IO,   0x19, 0x83 },
		{ ADV7611_I2CADDR_IO,	0x19, 0x94 },
		{ ADV7611_I2CADDR_IO,	0x33, 0x40 },

                /* Force HDMI free run */
                { ADV7611_I2CADDR_CP,   0xba, 0x01 },

                /* Disable HDCP 1.1*/
                { ADV7611_I2CADDR_KSV,  0x40, 0x81 },

                /* ADI recommended writes */
                { ADV7611_I2CADDR_HDMI, 0x9B, 0x03 },
                { ADV7611_I2CADDR_HDMI, 0xC1, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC2, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC3, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC4, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC5, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC6, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC7, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC8, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xC9, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCA, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCB, 0x01 },
                { ADV7611_I2CADDR_HDMI, 0xCC, 0x01 },

                { ADV7611_I2CADDR_HDMI, 0x00, 0x00 }, // Set HDMI port A
                { ADV7611_I2CADDR_HDMI, 0x83, 0xFE }, // Enable clock terminator for port A
                { ADV7611_I2CADDR_HDMI, 0x6F, 0x08 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x85, 0x1F }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x87, 0x70 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x8D, 0x04 }, // LFG
                { ADV7611_I2CADDR_HDMI, 0x8E, 0x1E }, // HFG
                { ADV7611_I2CADDR_HDMI, 0x1A, 0x8A }, // unmute audio
                { ADV7611_I2CADDR_HDMI, 0x57, 0xDA }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x58, 0x01 }, // ADI recommended setting
                { ADV7611_I2CADDR_HDMI, 0x75, 0x10 }, // DDC drive strength

                { ADV7611_I2CADDR_NONE, 0, 0 },
#endif
        };

        u8 i2caddr;

	ch_client = v4l2_get_subdevdata(sd);

	v4l2_dbg(1, debug, sd, "Adv7611 driver registered\n");

	/*Configure the ADV7611 in default 720p 60 Hz standard for normal
	   power up mode */

        for ( init_cur=&init_sequence[0];
              err >=0 && init_cur->i2caddr != ADV7611_I2CADDR_NONE;
              init_cur++ ) {

                switch ( init_cur->i2caddr ) {
                case ADV7611_I2CADDR_IO:
                        i2caddr = channel->IO_ADDR;
                        break;
                case ADV7611_I2CADDR_DPLL:
                        i2caddr = channel->DPLL_ADDR;
                        break;
                case ADV7611_I2CADDR_CEC:
                        i2caddr = channel->CEC_ADDR;
                        break;
                case ADV7611_I2CADDR_INFOFRAME:
                        i2caddr = channel->INFOFRAME_ADDR;
                        break;
                case ADV7611_I2CADDR_KSV:
                        i2caddr = channel->KSV_ADDR;
                        break;
                case ADV7611_I2CADDR_EDID:
                        i2caddr = channel->EDID_ADDR;
                        break;
                case ADV7611_I2CADDR_HDMI:
                        i2caddr = channel->HDMI_ADDR;
                        break;
                case ADV7611_I2CADDR_CP:
                        i2caddr = channel->CP_ADDR;
                        break;
                default:
                        err = -EINVAL;
                        adv7611_deinitialize(sd);
                        return err;
                        break;
                }
                err = adv7611_i2c_write_reg(ch_client,
                                             i2caddr,
                                             init_cur->reg,
                                             init_cur->val);
        }


	if (err < 0) {
		err = -EINVAL;
		adv7611_deinitialize(sd);
		return err;
	}

	v4l2_dbg(1, debug, sd, "End of adv7611_init.\n");
	return err;
}

static int adv7611_deinitialize(struct v4l2_subdev *sd)
{
        struct adv7611_channel *channel = (to_adv7611(sd));
	struct i2c_client      *ch_client = NULL;
        int                     err;

	ch_client = v4l2_get_subdevdata(sd);

	v4l2_dbg(1, debug, sd, "adv7611_deinitialize.\n");

        err = adv7611_i2c_write_reg(ch_client,
                                    channel->IO_ADDR,
                                    0x15,
                                    0xBE);

        return err;
}

static int adv7611_queryctrl(struct v4l2_subdev *sd, struct v4l2_queryctrl *qc)
{
	v4l2_dbg(2, debug, sd, "adv7611_queryctrl");
        switch (qc->id) {
	case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT:
		return v4l2_ctrl_query_fill(qc,
                                            0,
                                            ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_MAX,
                                            1,
                                            ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_RGB24);
	default:
                return -EINVAL;
	}

	return 0;
}

static int adv7611_g_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	int ret = 0;
	struct i2c_client      *ch_client = NULL;
        struct adv7611_channel *ch;

        v4l2_dbg(2, debug, sd, "adv7611_g_ctrl");
        ch = to_adv7611(sd);

	ch_client = v4l2_get_subdevdata(sd);


	switch (ctrl->id) {
	case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT:
                ctrl->value = ch->output_format;
                break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int adv7611_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	u8 reg_io_02;
	u8 reg_io_03;
	u8 reg_io_05;
	int ret = 0;

	struct i2c_client      *ch_client = NULL;
        struct adv7611_channel *ch;


        ch = to_adv7611(sd);

	ch_client = v4l2_get_subdevdata(sd);


	switch (ctrl->id) {
	case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT:
                switch ( ctrl->value ) {
                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_YUV422:
                        reg_io_02 = 0xf5; // YUV colorspace
                        reg_io_03 = 0x80; // 16-bit SDR
                        reg_io_05 = 0x2c; // embedded syncs
                        break;
                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_RGB24:
                        reg_io_02 = 0xf7; // RGB colorspace
                        reg_io_03 = 0x40; // 24-bit SDR embedded syncs
                        reg_io_05 = 0x2c; // embedded syncs
                        break;

                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_RGB24_DISCRETE:
                        reg_io_02 = 0xf7; // RGB colorspace
                        reg_io_03 = 0x40; // 24-bit SDR embedded syncs
                        reg_io_05 = 0x28; // embedded syncs
                        break;

                case ADV7611_V4L2_CONTROL_OUTPUT_FORMAT_YUV444:
                        reg_io_02 = 0xf5; // YUV colorspace
                        reg_io_03 = 0x40; // 24-bit SDR embedded syncs
                        reg_io_05 = 0x2c; // embedded syncs
                        break;

                default:
                        ret = -ERANGE;
                        break;
                }

                if ( 0 == ret ) {
                        ret = adv7611_i2c_write_reg(ch_client,
                                                    ch->IO_ADDR,
                                                    0x02,
                                                    reg_io_02);
                        ret |= adv7611_i2c_write_reg(ch_client,
                                                     ch->IO_ADDR,
                                                     0x03,
                                                     reg_io_03);
                        ret |= adv7611_i2c_write_reg(ch_client,
                                                     ch->IO_ADDR,
                                                     0x05,
                                                     reg_io_05);
                }

                ch->output_format = ctrl->value;

		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}



/* adv7611_setstd :
 * Function to set the video standard
 */
static int adv7611_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	int err = 0;
        struct adv7611_channel *channel = to_adv7611(sd);
        struct i2c_client *ch_client = v4l2_get_subdevdata(sd);

        /* No support for forcing standard */

        (void) channel;
        (void) ch_client;

	v4l2_dbg(1, debug, sd, "Start of adv7611_setstd..\n");

	v4l2_dbg(1, debug, sd, "End of adv7611 set standard...\n");
	return err;
}

static int adv7611_s_dv_preset(struct v4l2_subdev *sd,
					struct v4l2_dv_preset *dv_preset)
{
/*
	struct tvp7002 *device = to_tvp7002(sd);
	u32 preset;
	int i;

	for (i = 0; i < NUM_PRESETS; i++) {
		preset = tvp7002_presets[i].preset;
		if (preset == dv_preset->preset) {
			device->current_preset = &tvp7002_presets[i];
			return tvp7002_write_inittab(sd, tvp7002_presets[i].p_settings);
		}
	}

	return -EINVAL;
*/
struct adv7611_channel *channel = to_adv7611(sd);
v4l2_dbg(2, debug, sd, "adv7611_s_dv_preset\n");
        return 0;
}


/* adv7611_querystd :
 * Function to return standard detected by decoder
 */
static int adv7611_querystd(struct v4l2_subdev *sd, v4l2_std_id *id)
{
	int err = 0;
	unsigned char val;
	unsigned short stdi_cp8l       = 0;      /* Block length - clocks per 8 lines */
        unsigned char  stdi_lcvs       = 0;      /* Line count in vertical sync */
        unsigned short stdi_lpf        = 0;       /* Lines per field */
        unsigned short stdi_cpfdiv256  = 0; /* Clocks per field div 256 */
        unsigned char  stdi_interlaced = 0;

        int detected      = 0;
        int gotformat     = 0;
        int formatretries = 4;
        struct i2c_client *ch_client    = v4l2_get_subdevdata(sd);
        struct adv7611_channel *channel = to_adv7611(sd);
        unsigned int fps_1000 = 0;

        struct {
                v4l2_std_id id;
                unsigned short       lpf_low;
                unsigned short       lpf_high;
                unsigned int         fps1000_low;
                unsigned int         fps1000_high;

        } *queryStdEntry, queryStdTable[ ] = {
                {V4L2_STD_1080P_25, 0x462, 0x465, 24800, 25200},
                {V4L2_STD_1080P_30, 0x462, 0x465, 29700, 30300},
                {V4L2_STD_1080P_50, 0x462, 0x465, 49500, 50500},
                {V4L2_STD_1080P_60|V4L2_STD_HD_DIV_1001, 0x462, 0x465, 59900, 59979},
                {V4L2_STD_1080P_60, 0x462, 0x465, 59400, 60600},
                {V4L2_STD_1080P_24, 0x462, 0x465, 23700, 24300},

                {V4L2_STD_1080I_60|V4L2_STD_HD_DIV_1001, 0x230, 0x233, 59900, 59979},
                {V4L2_STD_1080I_60, 0x230, 0x233, 59400, 60600},
                {V4L2_STD_1080I_50, 0x230, 0x233, 49500, 50500},

                {V4L2_STD_720P_60|V4L2_STD_HD_DIV_1001,  0x2ea, 0x2ee, 59900, 59979},
                {V4L2_STD_720P_60,  0x2ea, 0x2ee, 59400, 60600},
                {V4L2_STD_720P_50,  0x2ea, 0x2ee, 49500, 50500},


                {V4L2_STD_800x600_60,  627,   629, 59000, 61000 },
                {V4L2_STD_800x600_72,  665,   667, 71000, 73000 },
                {V4L2_STD_800x600_75,  624,   626, 74000, 76000 },
                {V4L2_STD_800x600_85,  630,   632, 84000, 86000 },

                {V4L2_STD_525P_60,     523,   525, 59000,  61000 },
                {V4L2_STD_625P_50,     623,   625, 49000,  51000 },
                {V4L2_STD_525_60,      260,   263, 59000,  61000 },
                {V4L2_STD_625_50,      310,   313, 49000,  51000 },

                {V4L2_STD_1024x768_60, 805,   807, 59000, 61000 },
                {V4L2_STD_1024x768_70, 805,   807, 69000, 71000 },
                {V4L2_STD_1024x768_75, 799,   801, 74000, 76000 },
                {V4L2_STD_1024x768_85, 807,   809, 84000, 86000 },

                {V4L2_STD_1280x600_60, 621,   623, 59000, 61000 },

                {V4L2_STD_1280x720_60, 745,   747, 59000, 61000 },
                {V4L2_STD_1280x720_75, 751,   753, 74000, 76000 },
                {V4L2_STD_1280x720_85, 755,   757, 84000, 86000 },

                {V4L2_STD_1280x768_60, 794,   796, 59000, 61000 },
                {V4L2_STD_1280x768_75, 801,   803, 74000, 76000 },
                {V4L2_STD_1280x768_85, 806,   808, 84000, 86000 },

                {V4L2_STD_1280x800_60, 822,   824, 59000, 61000 },

                {V4L2_STD_1280x1024_60, 1065, 1067, 59000, 61000 },
                {V4L2_STD_1280x1024_75, 1065, 1067, 74000, 76000 },
                {V4L2_STD_1280x1024_85, 1071, 1073, 84000, 86000 },

                {               0,      0,     0,     0,     0},
        };

	v4l2_dbg(1, debug, sd, "Starting querystd function...\n");
	if (id == NULL) {
		dev_err(&ch_client->dev, "NULL Pointer.\n");
		return -EINVAL;
	}


        err = adv7611_i2c_read_reg(ch_client,
                                   channel->CP_ADDR,
                                   0xb1,
                                   &val);

        if (err < 0) {
                dev_err(&ch_client->dev,
                        "I2C read fails...sync detect\n");
                return err;
        }

        detected = (val & 0x80);
        stdi_interlaced = (val & 0x40) ? 1 : 0;
        stdi_cp8l = (val&0x3f);
        stdi_cp8l <<= 8;

        if ( !detected ) {
                v4l2_dbg( 1, debug, sd, "No sync detected\n");
                return -EIO;
        }


        do {
                /* Query clock cycles per 8 lines */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb2, &val);
                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...Lines per frame high\n");
                        return err;
                }

                stdi_cp8l |= (val&0xff);


                /* Query line count in vertical sync */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb3, &stdi_lcvs);
                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...Lines per frame low\n");
                        return err;
                }
                stdi_lcvs >>= 3;
                stdi_lcvs &= 0x1f;


                /* Query lines per field */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xa3, &val);
                stdi_lpf = (val& 0xf);
                stdi_lpf <<= 8;

                err |= adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xa4, &val);
                stdi_lpf |= val;

                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...Lines per field\n");
                        return err;
                }

                /* Query clocks per field div 256 */
                err = adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb8, &val);
                stdi_cpfdiv256 = (val& 0x1f);
                stdi_cpfdiv256 <<= 8;

                err |= adv7611_i2c_read_reg(ch_client,
                                           channel->CP_ADDR,
                                           0xb9, &val);
                stdi_cpfdiv256 |= val;

                if (err < 0) {
                        dev_err(&ch_client->dev,
                                "I2C read fails...CPF div 256\n");
                        return err;
                }


                fps_1000 = (28636360/256)*1000;
                if ( stdi_cpfdiv256 != 0 ) {
                        fps_1000 /= stdi_cpfdiv256;
                }

                for ( queryStdEntry = &queryStdTable[0];
                      queryStdEntry->id != 0 ;
                      queryStdEntry++ ) {

                        dev_dbg(&ch_client->dev,
                                "entry %d %u-%u %u-%u\n",
                                queryStdEntry - &queryStdTable[0],
                                queryStdEntry->lpf_low,
                                queryStdEntry->lpf_high,
                                queryStdEntry->fps1000_low,
                                queryStdEntry->fps1000_high );

                        if ( (queryStdEntry->lpf_low <= stdi_lpf)
                             && (queryStdEntry->lpf_high >= stdi_lpf )
                             && (queryStdEntry->fps1000_low <= fps_1000)
                             && (queryStdEntry->fps1000_high >= fps_1000) ) {

                                *id = queryStdEntry->id;
                                gotformat = 1;
                                break;
                        }
                }


                if ( !gotformat ) {
                        /* VSYNC ctr may take some time to converge */
                        msleep(50);
                }

        } while ( gotformat == 0 && --formatretries > 0 ) ;

	dev_notice(&ch_client->dev,
		   "ADV7611 - interlaced=%d lines per field=%d clocks per 8 lines=%d fps=%u.%03u\n",
                   (int)stdi_interlaced,
                   (int)stdi_lpf, (int)stdi_cp8l, fps_1000/1000, fps_1000%1000);

        if ( !gotformat ) {
		dev_notice(&ch_client->dev, "querystd: No std detected\n" );
                return -EINVAL;
	}

        err = 0;

	v4l2_dbg(1, debug, sd, "End of querystd function.\n");
	return err;
}


/* adv7611_s_stream:
 *
 *     Enable streaming.
 *
 *     For video decoder, this means driving the output bus,
 *     which may be shared with other video decoders.
 */
static int adv7611_s_stream(struct v4l2_subdev *sd, int enable)
{
        struct i2c_client *ch_client = v4l2_get_subdevdata(sd);
        struct adv7611_channel *channel = to_adv7611(sd);
        int err = 0;

        v4l2_dbg(1, debug, sd, "s_stream %d\n", enable);

//        if (channel->streaming == enable)
//                return 0;

        if ( enable ) {
                err = adv7611_i2c_write_reg(ch_client,
                                            channel->IO_ADDR,
                                            0x15,
                                            0xA0);
        } else {
                err = adv7611_i2c_write_reg(ch_client,
                                            channel->IO_ADDR,
                                            0x15,
                                            0xBE);
        }

        if (err) {
                v4l2_err(sd, "s_stream: err %d\n", err);
        } else {
                channel->streaming = enable;
        }

        return err;
}


/* adv7611_i2c_read_reg :This function is used to read value from register
 * for i2c client.
 */
static int adv7611_i2c_read_reg(struct i2c_client *client, u8 addr, u8 reg, u8 * val)
{
	int err = 0;
        int retries = 5;

	struct i2c_msg msg[2];
	unsigned char writedata[1];
	unsigned char readdata[1];

	if (!client->adapter) {
		err = -ENODEV;
	} else {

             do {
		msg[0].addr = addr;
		msg[0].flags = 0;
		msg[0].len = 1;
		msg[0].buf = writedata;
		writedata[0] = reg;

		msg[1].addr = addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = 1;
		msg[1].buf = readdata;

		err = i2c_transfer(client->adapter, msg, 2);
                if (err >= 2) {
                        *val = readdata[0];
		} else {
                        msleep(10);
                        v4l2_warn(client, "Read: retry ... %d\n", retries);
                }
             } while ( err < 2 && --retries > 0);
	}
        if ( err < 0 ) {
                dev_err( &client->adapter->dev, "ADV7611: read addr %02x reg x%02x failed\n", (int) addr, (int) reg );
        } else {
                v4l_dbg( 2, debug, client, "ADV7611: read addr %02x reg x%02x val %02x\n", (int) addr, (int)reg, (int)(*val) );
        }

	return ((err < 0) ? err : 0);
}

/* adv7611_i2c_write_reg :This function is used to write value into register
 * for i2c client.
 */
static int adv7611_i2c_write_reg(struct i2c_client *client, u8 addr, u8 reg, u8 val)
{
	int err = 0;

        int retries = 3;

	struct i2c_msg msg[1];
	unsigned char data[2];
	if (!client->adapter) {
		err = -ENODEV;
	} else {
             do {
		msg->addr = addr;
		msg->flags = 0;
		msg->len = 2;
		msg->buf = data;
		data[0] = reg;
		data[1] = val;
		err = i2c_transfer(client->adapter, msg, 1);
                if ( err < 0 ) {
                     msleep(10);
                     v4l_warn(client, "Write: retry ... %d\n", retries);
                }
             } while ( err < 0 && --retries > 0);
	}

        if ( err < 0
             && client->adapter != NULL ) {
             dev_err( &client->adapter->dev,
                      "adv7611 i2c write failed: addr %02x reg x%02x value x%02x\n",
                      (unsigned int)addr,
                      (unsigned int)reg,
                      (unsigned int)val
                  );
        }

        v4l_dbg( 2, debug, client, "ADV7611: write addr %02x reg x%02x val %02x\n", (int) addr, (int)reg, (int)val );

	return ((err < 0) ? err : 0);
}


/****************************************************************************
			I2C Client & Driver
 ****************************************************************************/

static int adv7611_probe(struct i2c_client *c,
			 const struct i2c_device_id *id)
{
	struct adv7611_channel *core;
	struct v4l2_subdev *sd;
        int ret;

	/* Check if the adapter supports the needed features */
	if (!i2c_check_functionality(c->adapter,
	     I2C_FUNC_SMBUS_READ_BYTE | I2C_FUNC_SMBUS_WRITE_BYTE_DATA))
		return -EIO;

	core = kzalloc(sizeof(struct adv7611_channel), GFP_KERNEL);
	if (!core) {
		return -ENOMEM;
	}


	sd = &core->sd;
	v4l2_i2c_subdev_init(sd, c, &adv7611_ops);
	v4l_info(c, "chip found @ 0x%02x (%s)\n",
		 c->addr << 1, c->adapter->name);

        core->IO_ADDR = c->addr;
        if ( core->IO_ADDR == 0x4c ) {
                core->DPLL_ADDR = 0x26;
		core->CEC_ADDR  = 0x40;
		core->INFOFRAME_ADDR = 0x3E;
		core->KSV_ADDR  = 0x32;
		core->EDID_ADDR = 0x36;
		core->HDMI_ADDR = 0x34;
		core->CP_ADDR   = 0x22;
        }
        else if ( core->IO_ADDR == 0x4d ) {
                core->DPLL_ADDR = 0x45;
		core->CEC_ADDR  = 0x46;
		core->INFOFRAME_ADDR = 0x47;
		core->KSV_ADDR  = 0x48;
		core->EDID_ADDR = 0x49;
		core->HDMI_ADDR = 0x4a;
		core->CP_ADDR   = 0x4b;
        }
        else {
                ret = -EINVAL;
                v4l_err(c, "adv7611 invalid I2C address %02x\n", c->addr );
                kfree(core);
                return ret;
        }


        ret = adv7611_initialize(sd);
        if ( ret != 0 ) {
             v4l_err(c, "adv7611 init failed, code %d\n", ret );
             kfree(core);
             return ret;
        }

	return ret;
}

static int adv7611_remove(struct i2c_client *c)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(c);

#ifdef DEBUG
 	v4l_info(c,
		"adv7611.c: removing adv7611 adapter on address 0x%x\n",
		c->addr << 1);
#endif

	v4l2_device_unregister_subdev(sd);
//	kfree(to_adv7611(sd));
	return 0;
}

/* ----------------------------------------------------------------------- */

static const struct i2c_device_id adv7611_id[] = {
	{ "adv7611", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adv7611_id);

static struct i2c_driver adv7611_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= "adv7611",
	},
	.probe		= adv7611_probe,
	.remove		= adv7611_remove,
	.id_table	= adv7611_id,
};

static __init int init_adv7611(void)
{
	return i2c_add_driver(&adv7611_driver);
}

static __exit void exit_adv7611(void)
{
	i2c_del_driver(&adv7611_driver);
}

module_init(init_adv7611);
module_exit(exit_adv7611);

MODULE_DESCRIPTION("Analog Devices AD9880 video decoder driver");
MODULE_AUTHOR("John Whittington");
MODULE_LICENSE("GPL");
