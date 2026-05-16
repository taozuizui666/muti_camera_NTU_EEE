/* SPDX-License-Identifier: GPL-2.0 */

/*
 * rs300 CMOS Image Sensor driver for RK3588
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd.
 * Modified for RK3588 fixed fmt bug, adapted to v4l2_subdev_state API
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/media.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/videodev2.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-image-sizes.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION      KERNEL_VERSION(0, 0x01, 0x2)
#define DRIVER_NAME         "rs300-mipi"

#define rs300_LINK_RATE     (80 * 1000 * 1000)
#define rs300_PIXEL_RATE    (40 * 1000 * 1000)

static int mode = 2;	 //0: 1280x1024, 1: 640x900, 2: 640x512
static int fps = 30;
module_param(mode, int, 0644);
module_param(fps, int, 0644);

#define CMD_MAGIC           0xEF
#define CMD_GET             _IOWR(CMD_MAGIC, 1, struct ioctl_data)
#define CMD_SET             _IOW(CMD_MAGIC, 2, struct ioctl_data)

struct ioctl_data {
    unsigned char bRequestType;
    unsigned char bRequest;
    unsigned short wValue;
    unsigned short wIndex;
    unsigned char *data;
    unsigned short wLength;
    unsigned int timeout;
};

#define I2C_VD_BUFFER_RW    0x1D00

static unsigned short do_crc(unsigned char *ptr, int len)
{
    unsigned int i;
    unsigned short crc = 0x0000;

    while (len--) {
        crc ^= (unsigned short)(*ptr++) << 8;
        for (i = 0; i < 8; ++i) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

static u8 start_regs[] = {
    0x01, 0x30, 0xc1, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0a, 0x00,
    0x00, 0x00, /* crc [14-15] */
    0x2F, 0x0D, /* crc [16-17] */
    0x00, 0x16, 0x03, 0x1e, /* path, src, dst, fps [18-21] */
    0x80, 0x02, 0x00, 0x02, /* width, height [22-25] */
    0x00, 0x00
};

static u8 stop_regs[] = {
    0x01, 0x30, 0xc2, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0a, 0x00,
    0x00, 0x00,
    0x2F, 0x0D,
    0x01, 0x16, 0x00, 0x0e,
    0x80, 0x02, 0x00, 0x02,
    0x00, 0x00
};

static int read_regs(struct i2c_client *client, u16 reg, u8 *val, int len)
{
    struct i2c_msg msg[2];
    unsigned char data[4] = { 0, 0, 0, 0 };

    msg[0].addr = client->addr;
    msg[0].flags = 0;
    msg[0].len = 2;
    msg[0].buf = data;

    msg[1].addr = client->addr;
    msg[1].flags = I2C_M_RD;
    msg[1].len = len;
    msg[1].buf = val;

    data[0] = reg >> 8;
    data[1] = reg & 0xff;
    return i2c_transfer(client->adapter, msg, 2);
}

static int write_regs(struct i2c_client *client, u16 reg, u8 *val, int len)
{
    struct i2c_msg msg[1];
    unsigned char *outbuf = kmalloc(sizeof(unsigned char) * (len + 2), GFP_KERNEL);
    int ret;

    if (!outbuf)
        return -ENOMEM;

    msg->addr = client->addr;
    msg->flags = 0;
    msg->len = len + 2;
    msg->buf = outbuf;
    outbuf[0] = reg >> 8;
    outbuf[1] = reg & 0xff;
    memcpy(outbuf + 2, val, len);
    ret = i2c_transfer(client->adapter, msg, 1);
    kfree(outbuf);
    return ret;
}

/* ------------------------------------------------------------------
 * I2C connectivity check
 * 发送最短合法查询帧(CMD=0x31 get_info)，回读响应。
 * i2c_transfer 成功且首字节为 0xA1 视为设备在线。
 * ------------------------------------------------------------------ */
static u8 rs300_probe_req[] = {
    0x01, 0x30, 0x31, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x0a, 0x00,
    0x00, 0x00, /* inner crc [14-15] */
    0x00, 0x00, /* outer crc [16-17] */
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

static int rs300_check_i2c(struct i2c_client *client)
{
    u8 rbuf[28] = { 0 };
    unsigned short crc;
    int ret;

    crc = do_crc((u8 *)(rs300_probe_req + 18), 10);
    rs300_probe_req[14] = crc & 0xff;
    rs300_probe_req[15] = crc >> 8;
    crc = do_crc((u8 *)rs300_probe_req, 16);
    rs300_probe_req[16] = crc & 0xff;
    rs300_probe_req[17] = crc >> 8;

    ret = write_regs(client, I2C_VD_BUFFER_RW,
             rs300_probe_req, sizeof(rs300_probe_req));
    if (ret < 0) {
        dev_err(&client->dev,
            "rs300 I2C write failed (addr=0x%02x ret=%d) -- "
            "check SDA/SCL wiring and pull-up resistors\n",
            client->addr, ret);
        return ret;
    }

    usleep_range(2000, 3000); /* 等待固件处理约 1ms */

    ret = read_regs(client, I2C_VD_BUFFER_RW, rbuf, sizeof(rbuf));
    if (ret < 0) {
        dev_err(&client->dev,
            "rs300 I2C read failed (addr=0x%02x ret=%d) -- "
            "device may not be powered or address mismatch\n",
            client->addr, ret);
        return ret;
    }

    if (rbuf[0] == 0x00)
        dev_warn(&client->dev,
             "rs300 I2C alive but response empty "
             "(device may still be booting)\n");
    else if (rbuf[0] != 0xA1)
        dev_warn(&client->dev,
             "rs300 unexpected response 0x%02x (expected 0xA1)\n",
             rbuf[0]);
    else
        dev_info(&client->dev,
             "rs300 I2C OK, device info response received\n");

    return 0;
}

/* ------------------------------------------------------------------
 * MIPI endpoint connectivity check
 * 从 OF graph 解析 endpoint，校验：
 *   1. port/endpoint 节点存在
 *   2. remote-endpoint 指向有效节点
 *   3. data-lanes 与驱动声明的 2-lane 一致
 *   4. 对端节点 status = "okay"
 * ------------------------------------------------------------------ */
// #define RS300_EXPECTED_LANES 2

// static int rs300_check_mipi_endpoint(struct device *dev)
// {
//     struct device_node *ep, *remote;
//     struct v4l2_fwnode_endpoint vep = {
//         .bus_type = V4L2_MBUS_CSI2_DPHY,
//     };
//     int ret = 0;

//     ep = of_graph_get_next_endpoint(dev->of_node, NULL);
//     if (!ep) {
//         dev_err(dev,
//             "rs300 MIPI check: no endpoint in DT -- "
//             "add port/endpoint with remote-endpoint under sensor node\n");
//         return -ENODEV;
//     }

//     ret = v4l2_fwnode_endpoint_parse(of_fwnode_handle(ep), &vep);
//     if (ret) {
//         dev_err(dev,
//             "rs300 MIPI check: endpoint parse failed (%d) -- "
//             "check data-lanes / bus-type properties\n", ret);
//         goto put_ep;
//     }

//     if (vep.bus.mipi_csi2.num_data_lanes != RS300_EXPECTED_LANES) {
//         dev_err(dev,
//             "rs300 MIPI check: data-lanes mismatch DT=%u driver=%d -- "
//             "fix data-lanes in DT endpoint node\n",
//             vep.bus.mipi_csi2.num_data_lanes, RS300_EXPECTED_LANES);
//         ret = -EINVAL;
//         goto put_ep;
//     }

//     remote = of_graph_get_remote_port_parent(ep);
//     if (!remote) {
//         dev_err(dev,
//             "rs300 MIPI check: remote-endpoint target not found -- "
//             "check remote-endpoint phandle points to CSI/PHY node\n");
//         ret = -ENODEV;
//         goto put_ep;
//     }

//     if (!of_device_is_available(remote))
//         dev_warn(dev,
//              "rs300 MIPI check: remote node '%s' is DISABLED -- "
//              "set status = \"okay\" in the CSI/PHY node\n",
//              remote->full_name);
//     else
//         dev_info(dev,
//              "rs300 MIPI check: OK remote='%s' lanes=%u\n",
//              remote->full_name,
//              vep.bus.mipi_csi2.num_data_lanes);

//     of_node_put(remote);
// put_ep:
//     of_node_put(ep);
//     return ret;
// }

/* ------------------------------------------------------------------ 
  Mode table                                                        
 ------------------------------------------------------------------ */
struct rs300_mode {
	u16 width;
	u16 height;
	struct v4l2_fract max_fps;
	u32 code;
};

static const struct rs300_mode rs300_modes[] = {
    { /* mode 0 640x900 */
        .width = 640,
        .height = 900,
        .max_fps = { .numerator = 1, .denominator = 30 },
        .code = MEDIA_BUS_FMT_YUYV8_2X8,
    },
    { /* mode 1 1280x1024 */
        .width = 1280,
        .height = 1024,
        .max_fps = { .numerator = 1, .denominator = 30 },
        .code = MEDIA_BUS_FMT_YUYV8_2X8,
    },
    { /* mode 2 640x512 */
        .width = 640,
        .height = 512,
        .max_fps = { .numerator = 1, .denominator = 30 },
        .code = MEDIA_BUS_FMT_YUYV8_2X8,
    },
};

static const char *const rs300_supply_names[] = {
    "dovdd",
    "avdd",
    "dvdd",
};
#define rs300_NUM_SUPPLIES ARRAY_SIZE(rs300_supply_names)

struct rs300 {
    struct v4l2_subdev sd;
    struct media_pad pad;
    struct v4l2_mbus_framefmt format;
    struct clk *xvclk;
    struct gpio_desc *pwdn_gpio;
    struct gpio_desc *reset_gpio;
    struct regulator_bulk_data supplies[rs300_NUM_SUPPLIES];
    struct mutex lock;
    struct i2c_client *client;
    struct v4l2_ctrl_handler ctrls;
    struct v4l2_ctrl *link_frequency;
    struct v4l2_ctrl *pixel_rate;
    const struct rs300_mode *cur_mode;
    int streaming;
    u32 module_index;
    const char *module_facing;
    const char *module_name;
    const char *len_name;
};

static inline struct rs300 *to_rs300(struct v4l2_subdev *sd)
{
    return container_of(sd, struct rs300, sd);
}

static void rs300_fill_format(const struct rs300 *rs300, struct v4l2_mbus_framefmt *fmt)
{
    memset(fmt, 0, sizeof(*fmt));
    fmt->width = rs300->cur_mode->width;
    fmt->height = rs300->cur_mode->height;
    fmt->code = rs300->cur_mode->code;
    fmt->colorspace = V4L2_COLORSPACE_SRGB;
    fmt->field = V4L2_FIELD_NONE;
    fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(fmt->colorspace);
    fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
                              fmt->colorspace,
                              fmt->ycbcr_enc);
    fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(fmt->colorspace);
    pr_debug("rs300 fill_format: %ux%u code=0x%04x\n",
         fmt->width, fmt->height, fmt->code);
}

static const struct rs300_mode *rs300_find_mode(u32 width, u32 height)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(rs300_modes); i++) {
        if (rs300_modes[i].width == width &&
            rs300_modes[i].height == height)
            return &rs300_modes[i];
    }
    /* fallback nearest larger or last one */
    for (i = 0; i < ARRAY_SIZE(rs300_modes); i++) {
        if (rs300_modes[i].width >= width &&
            rs300_modes[i].height >= height)
            return &rs300_modes[i];
    }
    return &rs300_modes[0];
}

static int rs300_enum_mbus_code(struct v4l2_subdev *sd,
                struct v4l2_subdev_state *state,
                struct v4l2_subdev_mbus_code_enum *code)
{
    if (code->index != 0)
        return -EINVAL;
    code->code = MEDIA_BUS_FMT_YUYV8_2X8;
    return 0;
}

static int rs300_enum_frame_sizes(struct v4l2_subdev *sd,
                  struct v4l2_subdev_state *state,
                  struct v4l2_subdev_frame_size_enum *fse)
{
    if (fse->index >= ARRAY_SIZE(rs300_modes))
        return -EINVAL;
    fse->code = MEDIA_BUS_FMT_YUYV8_2X8;
    fse->min_width = rs300_modes[fse->index].width;
    fse->max_width = fse->min_width;
    fse->min_height = rs300_modes[fse->index].height;
    fse->max_height = fse->min_height;
    return 0;
}

static int rs300_enum_frame_interval(struct v4l2_subdev *sd,
                     struct v4l2_subdev_state *state,
                     struct v4l2_subdev_frame_interval_enum *fie)
{
    const struct rs300_mode *mode;
    int i;

    if (fie->index > 0)
        return -EINVAL;

    /* 在所有模式中查找匹配的宽高；若 width/height 均为 0 则返回当前模式 */
    mode = NULL;
    for (i = 0; i < ARRAY_SIZE(rs300_modes); i++) {
        if (rs300_modes[i].width == fie->width &&
            rs300_modes[i].height == fie->height) {
            mode = &rs300_modes[i];
            break;
        }
    }
     /* 找不到（包括 width/height=0 的情况）则返回当前激活模式 */
    if (!mode)
        mode = &rs300_modes[0];

    fie->width    = mode->width;
    fie->height   = mode->height;
    fie->code     = mode->code;
    fie->interval = mode->max_fps;
    return 0;
}

static int rs300_get_fmt(struct v4l2_subdev *sd,
             struct v4l2_subdev_state *state,
             struct v4l2_subdev_format *fmt)
{
    struct rs300 *rs300 = to_rs300(sd);

    mutex_lock(&rs300->lock);
    if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
        struct v4l2_mbus_framefmt *mf;

        mf = v4l2_subdev_get_try_format(sd, state, fmt->pad);
        fmt->format = *mf;
    } else {
        fmt->format = rs300->format;
    }
    mutex_unlock(&rs300->lock);

    v4l2_dbg(1, 1, sd, "get_fmt: which=%s w=%u h=%u code=0x%04x\n",
         fmt->which == V4L2_SUBDEV_FORMAT_TRY ? "TRY" : "ACTIVE",
         fmt->format.width, fmt->format.height, fmt->format.code);
    return 0;
}

static int rs300_set_fmt(struct v4l2_subdev *sd,
             struct v4l2_subdev_state *state,
             struct v4l2_subdev_format *fmt)
{
    struct rs300 *rs300 = to_rs300(sd);
    const struct rs300_mode *new_mode;

    new_mode = rs300_find_mode(fmt->format.width, fmt->format.height);

    mutex_lock(&rs300->lock);
    if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
        struct v4l2_mbus_framefmt *try_fmt =
            v4l2_subdev_get_try_format(sd, state, fmt->pad);
        try_fmt->width = new_mode->width;
        try_fmt->height = new_mode->height;
        try_fmt->code = new_mode->code;
        try_fmt->colorspace = V4L2_COLORSPACE_SRGB;
        try_fmt->field = V4L2_FIELD_NONE;
        try_fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(try_fmt->colorspace);
        try_fmt->quantization = V4L2_MAP_QUANTIZATION_DEFAULT(true,
                                    try_fmt->colorspace,
                                    try_fmt->ycbcr_enc);
        try_fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(try_fmt->colorspace);
        fmt->format = *try_fmt;
    } else {
        rs300->cur_mode = new_mode;
        rs300_fill_format(rs300, &rs300->format);
        fmt->format = rs300->format;
    }
    mutex_unlock(&rs300->lock);
    return 0;
}

static void rs300_get_module_inf(struct rs300 *rs300,
                 struct rkmodule_inf *inf)
{
    memset(inf, 0, sizeof(*inf));
    strscpy(inf->base.sensor, DRIVER_NAME, sizeof(inf->base.sensor));
    strscpy(inf->base.module, rs300->module_name,
        sizeof(inf->base.module));
    strscpy(inf->base.lens, rs300->len_name, sizeof(inf->base.lens));
}

static long rs300_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
    struct rs300 *rs300 = to_rs300(sd);
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    long ret = 0;
    unsigned char *data;
    struct ioctl_data *valp;

    switch (cmd) {
    case RKMODULE_GET_MODULE_INFO:
        rs300_get_module_inf(rs300, (struct rkmodule_inf *)arg);
        break;

    case CMD_GET:
        valp = (struct ioctl_data *)arg;
        if (!valp || !valp->data)
            return -EFAULT;
        data = kmalloc(valp->wLength, GFP_KERNEL);
        if (!data)
            return -ENOMEM;
        read_regs(client, valp->wIndex, data, valp->wLength);
        if (copy_to_user(valp->data, data, valp->wLength))
            ret = -EFAULT;
        kfree(data);
        break;

    case CMD_SET:
        valp = (struct ioctl_data *)arg;
        if (!valp || !valp->data)
            return -EFAULT;
        write_regs(client, valp->wIndex, valp->data, valp->wLength);
        break;

    default:
        ret = -ENOIOCTLCMD;
        break;
    }

    return ret;
}

#ifdef CONFIG_COMPAT
static long rs300_compat_ioctl32(struct v4l2_subdev *sd,
                 unsigned int cmd, unsigned long arg)
{
    void __user *up = compat_ptr(arg);
    struct rkmodule_inf *inf;
    long ret;

    switch (cmd) {
    case RKMODULE_GET_MODULE_INFO:
        inf = kzalloc(sizeof(*inf), GFP_KERNEL);
        if (!inf)
            return -ENOMEM;
        ret = rs300_ioctl(sd, cmd, inf);
        if (!ret)
            ret = copy_to_user(up, inf, sizeof(*inf));
        kfree(inf);
        break;
    default:
        ret = -ENOIOCTLCMD;
        break;
    }
    return ret;
}
#endif

static int rs300_s_power(struct v4l2_subdev *sd, int on)
{
    struct rs300 *rs300 = to_rs300(sd);

    if (on) {
        if (rs300->xvclk)
            clk_prepare_enable(rs300->xvclk);
        if (rs300->pwdn_gpio)
            gpiod_set_value_cansleep(rs300->pwdn_gpio, 0);
        if (rs300->reset_gpio) {
            gpiod_set_value_cansleep(rs300->reset_gpio, 0);
            usleep_range(5000, 10000);
            gpiod_set_value_cansleep(rs300->reset_gpio, 1);
            usleep_range(5000, 10000);
        }
    } else {
        if (rs300->pwdn_gpio)
            gpiod_set_value_cansleep(rs300->pwdn_gpio, 1);
        if (rs300->xvclk)
            clk_disable_unprepare(rs300->xvclk);
    }
    return 0;
}

static int rs300_s_stream(struct v4l2_subdev *sd, int on)
{
    struct i2c_client *client = v4l2_get_subdevdata(sd);
    struct rs300 *rs300 = to_rs300(sd);
    unsigned short crcdata;
    int ret = 0;

    mutex_lock(&rs300->lock);
    if (on) {
        if (rs300->streaming) {
            mutex_unlock(&rs300->lock);
            return 0;
        }

        /* update resolution & CRC */
        start_regs[18] = 0x00; /* path */
        start_regs[19] = 0x16; /* src */
        start_regs[20] = 0x03; /* dst */
        start_regs[21] = 0x1e; /* fps */
        start_regs[22] = rs300->cur_mode->width & 0xff;
        start_regs[23] = rs300->cur_mode->width >> 8;
        start_regs[24] = rs300->cur_mode->height & 0xff;
        start_regs[25] = rs300->cur_mode->height >> 8;

        crcdata = do_crc((u8 *)(start_regs + 18), 10);
        start_regs[14] = crcdata & 0xff;
        start_regs[15] = crcdata >> 8;
        crcdata = do_crc((u8 *)start_regs, 16);
        start_regs[16] = crcdata & 0xff;
        start_regs[17] = crcdata >> 8;

        v4l_info(client, "rs300 start stream %dx%d\n",
             rs300->cur_mode->width, rs300->cur_mode->height);

        if (write_regs(client, I2C_VD_BUFFER_RW,
               start_regs, sizeof(start_regs)) < 0) {
            v4l_err(client, "error start rs300\n");
            ret = -ENODEV;
            goto unlock;
        }
        rs300->streaming = 1;
    } else {
        if (!rs300->streaming) {
            mutex_unlock(&rs300->lock);
            return 0;
        }

        crcdata = do_crc((u8 *)(stop_regs + 18), 10);
        stop_regs[14] = crcdata & 0xff;
        stop_regs[15] = crcdata >> 8;
        crcdata = do_crc((u8 *)stop_regs, 16);
        stop_regs[16] = crcdata & 0xff;
        stop_regs[17] = crcdata >> 8;

        v4l_info(client, "rs300 stop stream\n");
        /* 若停流耗时过长可注释掉下面这行 */
        write_regs(client, I2C_VD_BUFFER_RW, stop_regs, sizeof(stop_regs));
        rs300->streaming = 0;
    }
unlock:
    mutex_unlock(&rs300->lock);
    return ret;
}

static int rs300_g_frame_interval(struct v4l2_subdev *sd,
                  struct v4l2_subdev_frame_interval *fi)
{
    struct rs300 *rs300 = to_rs300(sd);

    fi->interval = rs300->cur_mode->max_fps;
    return 0;
}

static int rs300_g_mbus_config(struct v4l2_subdev *sd,
               unsigned int pad,
               struct v4l2_mbus_config *config)
{
    config->type = V4L2_MBUS_CSI2_DPHY;
    config->bus.mipi_csi2.num_data_lanes = 2;
    config->bus.mipi_csi2.flags = 0;
    return 0;
}

static const s64 link_freq_menu_items[] = {
    rs300_LINK_RATE,
};

static int rs300_init_controls(struct rs300 *rs300)
{
    struct v4l2_ctrl_handler *handler = &rs300->ctrls;
    int ret;

    ret = v4l2_ctrl_handler_init(handler, 3);
    if (ret)
        return ret;

    rs300->link_frequency =
        v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
                   0, 0, link_freq_menu_items);
    if (rs300->link_frequency)
        rs300->link_frequency->flags |= V4L2_CTRL_FLAG_READ_ONLY;

    rs300->pixel_rate =
        v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
                  0, rs300_PIXEL_RATE, 1, rs300_PIXEL_RATE);

    /*
     * VBLANK: rkcif 在 online 模式下要求 vblank >= 1000us。
     * rs300 固件不支持动态配置消隐，注册为只读固定值。
     * 按 30fps 帧周期 33333us，保守预留 3000us 作为消隐时间。
     */
    v4l2_ctrl_new_std(handler, NULL, V4L2_CID_VBLANK,
              3000, 3000, 1, 3000);

    if (handler->error) {
        ret = handler->error;
        v4l2_ctrl_handler_free(handler);
        return ret;
    }

    rs300->sd.ctrl_handler = handler;
    return 0;
}

static const struct v4l2_subdev_core_ops rs300_core_ops = {
    .log_status = v4l2_ctrl_subdev_log_status,
    .subscribe_event = v4l2_ctrl_subdev_subscribe_event,
    .unsubscribe_event = v4l2_event_subdev_unsubscribe,
    .ioctl = rs300_ioctl,
    .s_power = rs300_s_power,
#ifdef CONFIG_COMPAT
    .compat_ioctl32 = rs300_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops rs300_video_ops = {
    .s_stream = rs300_s_stream,
    .g_frame_interval = rs300_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops rs300_pad_ops = {
    .enum_mbus_code = rs300_enum_mbus_code,
    .enum_frame_size = rs300_enum_frame_sizes,
    .enum_frame_interval = rs300_enum_frame_interval,
    .get_fmt = rs300_get_fmt,
    .set_fmt = rs300_set_fmt,
    .get_mbus_config = rs300_g_mbus_config,
};

static const struct v4l2_subdev_ops rs300_subdev_ops = {
    .core = &rs300_core_ops,
    .video = &rs300_video_ops,
    .pad = &rs300_pad_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int rs300_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
    struct rs300 *rs300 = to_rs300(sd);
    struct v4l2_mbus_framefmt *try_fmt;

    try_fmt = v4l2_subdev_get_try_format(sd, fh->state, 0);
    rs300_fill_format(rs300, try_fmt);
    return 0;
}

static const struct v4l2_subdev_internal_ops rs300_internal_ops = {
    .open = rs300_open,
};
#endif

static int rs300_probe(struct i2c_client *client,
           const struct i2c_device_id *id)
{
    struct device *dev = &client->dev;
    struct device_node *node = dev->of_node;
    struct v4l2_subdev *sd;
    struct rs300 *rs300;
    char facing[2];
    int ret;

    dev_info(dev, "driver version %02x.%02x.%02x\n",
         DRIVER_VERSION >> 16,
         (DRIVER_VERSION & 0xff00) >> 8,
         DRIVER_VERSION & 0x00ff);

    rs300 = devm_kzalloc(dev, sizeof(*rs300), GFP_KERNEL);
    if (!rs300)
        return -ENOMEM;

    ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
                   &rs300->module_index);
    if (ret)
        rs300->module_index = 0;

    ret = of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
                      &rs300->module_facing);
    if (ret)
        rs300->module_facing = "back";

    ret = of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
                      &rs300->module_name);
    if (ret)
        rs300->module_name = "RS300";

    ret = of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
                      &rs300->len_name);
    if (ret)
        rs300->len_name = "NC";

    rs300->client = client;
    rs300->cur_mode = &rs300_modes[mode % ARRAY_SIZE(rs300_modes)];

    /* Optional xvclk, pwdn, reset */
    rs300->xvclk = devm_clk_get_optional(dev, "xvclk");
    if (IS_ERR(rs300->xvclk)) {
        ret = PTR_ERR(rs300->xvclk);
        dev_err(dev, "failed to get xvclk %d\n", ret);
        return ret;
    }

    rs300->pwdn_gpio = devm_gpiod_get_optional(dev, "pwdn", GPIOD_OUT_LOW);
    if (IS_ERR(rs300->pwdn_gpio)) {
        ret = PTR_ERR(rs300->pwdn_gpio);
        dev_err(dev, "failed to get pwdn gpio %d\n", ret);
        return ret;
    }

    rs300->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(rs300->reset_gpio)) {
        ret = PTR_ERR(rs300->reset_gpio);
        dev_err(dev, "failed to get reset gpio %d\n", ret);
        return ret;
    }

    // ret = devm_regulator_bulk_get(dev, rs300_NUM_SUPPLIES, rs300->supplies);
    // if (ret && ret != -ENODEV) {
        // dev_err(dev, "failed to get regulators %d\n", ret);
        // return ret;
    //     dev_dbg(dev, "regulators not available, skipping (%d)\n", ret);
    //     /* 清零，后续不使用 */
    //     memset(rs300->supplies, 0, sizeof(rs300->supplies));
    // }

    /* --- 连通性检测 -------------------------------------------- */
    ret = rs300_check_i2c(client);
    if (ret) {
        dev_err(dev, "rs300 I2C connectivity check failed, aborting probe\n");
        return ret;
    }

    // ret = rs300_check_mipi_endpoint(dev);
    // if (ret) {
    //     dev_err(dev, "rs300 MIPI endpoint check failed, aborting probe\n");
    //     return ret;
    // }
    /* ----------------------------------------------------------- */

    mutex_init(&rs300->lock);

    ret = rs300_init_controls(rs300);
    if (ret)
        goto err_mutex;

    sd = &rs300->sd;
    v4l2_i2c_subdev_init(sd, client, &rs300_subdev_ops);

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
    sd->internal_ops = &rs300_internal_ops;
    sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif

#if defined(CONFIG_MEDIA_CONTROLLER)
    rs300->pad.flags = MEDIA_PAD_FL_SOURCE;
    sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
    ret = media_entity_pads_init(&sd->entity, 1, &rs300->pad);
    if (ret < 0)
        goto err_ctrls;
#endif

    rs300_fill_format(rs300, &rs300->format);

    memset(facing, 0, sizeof(facing));
    if (strcmp(rs300->module_facing, "back") == 0)
        facing[0] = 'b';
    else
        facing[0] = 'f';

    snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
         rs300->module_index, facing,
         DRIVER_NAME, dev_name(sd->dev));
    dev_info(dev, "%s probe success\n", sd->name);

    ret = v4l2_async_register_subdev_sensor(sd);
    if (ret < 0)
        goto err_media;

    return 0;

err_media:
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
err_ctrls:
    v4l2_ctrl_handler_free(&rs300->ctrls);
err_mutex:
    mutex_destroy(&rs300->lock);
    return ret;
}

static void rs300_remove(struct i2c_client *client)
{
    struct v4l2_subdev *sd = i2c_get_clientdata(client);
    struct rs300 *rs300 = to_rs300(sd);

    v4l2_async_unregister_subdev(sd);
    v4l2_ctrl_handler_free(&rs300->ctrls);
#if defined(CONFIG_MEDIA_CONTROLLER)
    media_entity_cleanup(&sd->entity);
#endif
    mutex_destroy(&rs300->lock);
}

static const struct i2c_device_id rs300_id[] = {
    { "rs300", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, rs300_id);

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id rs300_of_match[] = {
    { .compatible = "infisense,rs300-mipi" },
    { }
};
MODULE_DEVICE_TABLE(of, rs300_of_match);
#endif

static struct i2c_driver rs300_i2c_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = of_match_ptr(rs300_of_match),
    },
    .probe = rs300_probe,
    .remove = rs300_remove,
    .id_table = rs300_id,
};

module_i2c_driver(rs300_i2c_driver);

MODULE_AUTHOR("infisense");
MODULE_DESCRIPTION("rs300 ir camera driver for RK3588");
MODULE_LICENSE("GPL v2");