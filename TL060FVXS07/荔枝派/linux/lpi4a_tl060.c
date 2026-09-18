// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fixed-target TH1520 DSI0 / TL060FVXS07 terminal DRM bridge for bring-up.
 * Fixed-board display-only implementation; the video pattern generator is off.
 * Register definitions and D-PHY test sequence derived from RevyOS
 * drivers/gpu/drm/verisilicon/dw_mipi_dsi.c (VeriSilicon, 2020, GPL-2.0)
 * and drivers/phy/synopsys/phy-dw-mipi-dphy.c (GPL-2.0+).
 * Uses the existing VO clock provider. The CRTC owns the DPU pixel clock;
 * this bridge owns only DSI clocks/gates. Standalone Linux DRM adapter below
 * uses standard regulator/backlight APIs; no private DC or Android symbols.
 */
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/component.h>
#include <linux/regulator/consumer.h>
#include <linux/backlight.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_connector.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>
#include <uapi/linux/media-bus-format.h>
#include "lpi4a_tl060_dcs.h"

static bool backlight;
module_param(backlight, bool, 0400);
MODULE_PARM_DESC(backlight, "Initial backlight brightness: false=off, true=20 percent");

struct lpi4a_bridge {
    struct drm_bridge bridge;
    struct regulator_bulk_data supplies[2];
    struct backlight_device *backlight;
    bool powered;
};

static struct lpi4a_bridge *to_lpi4a(struct drm_bridge *b)
{
    return container_of(b, struct lpi4a_bridge, bridge);
}

#define DSI_BASE 0xffef500000ULL
#define DSI_SIZE 0x10000
#define VO_BASE  0xffef528000ULL
#define DSI_VERSION 0x00
#define DSI_PWR_UP 0x04
#define DSI_CLKMGR_CFG 0x08
#define DSI_PCKHDL_CFG 0x2c
#define DSI_GEN_VCID 0x30
#define DSI_MODE_CFG 0x34
#define DSI_CMD_MODE_CFG 0x68
#define DSI_GEN_HDR 0x6c
#define DSI_GEN_PLD_DATA 0x70
#define DSI_CMD_PKT_STATUS 0x74
#define DSI_TO_CNT_CFG 0x78
#define DSI_BTA_TO_CNT 0x8c
#define DSI_LPCLK_CTRL 0x94
#define DSI_PHY_TMR_LPCLK_CFG 0x98
#define DSI_PHY_TMR_CFG 0x9c
#define DSI_PHY_RSTZ 0xa0
#define DSI_PHY_IF_CFG 0xa4
#define DSI_PHY_STATUS 0xb0
#define DSI_PHY_TST_CTRL0 0xb4
#define DSI_PHY_TST_CTRL1 0xb8
#define DSI_INT_MSK0 0xc4
#define DSI_INT_MSK1 0xc8
#define DSI_PHY_TMR_RD_CFG 0xf4
#define GEN_RD_BUSY BIT(6)
#define GEN_RX_EMPTY BIT(4)
#define GEN_TX_FULL BIT(3)
#define GEN_TX_EMPTY BIT(2)
#define GEN_CMD_FULL BIT(1)
#define GEN_CMD_EMPTY BIT(0)
#define PHY_LOCK BIT(0)
#define PHY_CLK_STOP BIT(2)
#define PHY_LANE0_STOP BIT(4)
#define PHY_LANE1_STOP BIT(7)
#define PHY_LANE2_STOP BIT(9)
#define PHY_LANE3_STOP BIT(11)
#define PHY_STOP_ALL (PHY_CLK_STOP | PHY_LANE0_STOP | PHY_LANE1_STOP | \
                      PHY_LANE2_STOP | PHY_LANE3_STOP)
#define VO_DSI0_RESET 0x08
#define VO_DSI0_PHY_CFG 0x74
#define VO_PHY_FREQ_MASK GENMASK(15, 3)

static struct {
    void __iomem *base, *vo;
    struct clk *clks[3];
    unsigned int enabled;
    struct device *dev;
    struct mipi_dsi_host host;
    struct mipi_dsi_device *panel;
    bool region, host_registered, attached, modified, reset_touched, awake;
    u32 saved_reset, saved_phy_cfg;
    struct clk *pixel_gate;
    unsigned long pixel_rate;
    bool pixel_gate_enabled, streaming;
    struct drm_display_mode mode;
} diag;

/* Clock IDs are provider-specific DT ABI, NOT hardware gate-bit numbers.
 * RevyOS's vendor ABI differs from mainline even on the same SoC.
 * The gates' CCF
 * parent metadata is not the PHY's physical reference frequency; RevyOS
 * explicitly supplies separate 24 MHz oscillator references for that purpose.
 */
struct dsi_clock_layout {
    unsigned int control[3]; /* DSI0 PCLK, CFG, REF */
    unsigned int pixel;     /* DSI0 DPI gate, NOT the DPU0 pixel gate */
};
static const struct dsi_clock_layout mainline_clocks = {
    .control = {12, 14, 16}, .pixel = 28,
};
/* dt-bindings/clock/th1520-vosys.h, xuantie/ vosys-gate.c */
static const struct dsi_clock_layout xuantie_clocks = {
    .control = {5, 19, 21}, .pixel = 13,
};
static const u32 saved_offsets[] = {
    DSI_CLKMGR_CFG, DSI_PCKHDL_CFG, DSI_GEN_VCID, DSI_MODE_CFG,
    DSI_CMD_MODE_CFG, DSI_TO_CNT_CFG, DSI_BTA_TO_CNT, DSI_LPCLK_CTRL,
    DSI_PHY_TMR_LPCLK_CFG, DSI_PHY_TMR_CFG, DSI_PHY_IF_CFG,
    DSI_PHY_TST_CTRL0, DSI_PHY_TST_CTRL1, DSI_INT_MSK0, DSI_INT_MSK1,
    DSI_PHY_TMR_RD_CFG,
    0x0c, 0x10, 0x14, 0x18, 0x38, 0x3c, 0x40, 0x44,
    0x48, 0x4c, 0x50, 0x54, 0x58, 0x5c, 0x60,
};
static u32 saved_regs[ARRAY_SIZE(saved_offsets)];
static u32 rd(u32 off) { return readl(diag.base + off); }
static void wr(u32 off, u32 val) { writel(val, diag.base + off); }
static void update(u32 off, u32 mask, u32 val)
{
    wr(off, (rd(off) & ~mask) | (val & mask));
}

static void phy_test_write(u8 code, u8 data)
{
    /* Falling edge latches address; subsequent pulse latches data. */
    update(DSI_PHY_TST_CTRL0, BIT(1), BIT(1));
    update(DSI_PHY_TST_CTRL1, GENMASK(7, 0), code);
    update(DSI_PHY_TST_CTRL1, BIT(16), BIT(16));
    update(DSI_PHY_TST_CTRL0, BIT(1), 0);
    update(DSI_PHY_TST_CTRL1, BIT(16), 0);
    update(DSI_PHY_TST_CTRL1, GENMASK(7, 0), data);
    update(DSI_PHY_TST_CTRL0, BIT(1), BIT(1));
    update(DSI_PHY_TST_CTRL0, BIT(1), 0);
}

static int host_attach(struct mipi_dsi_host *host, struct mipi_dsi_device *dsi)
{
    return dsi->channel == 0 && dsi->lanes == 4 &&
           dsi->format == MIPI_DSI_FMT_RGB888 ? 0 : -EINVAL;
}
static int host_detach(struct mipi_dsi_host *host, struct mipi_dsi_device *dsi)
{
    return 0;
}
static ssize_t host_transfer(struct mipi_dsi_host *host,
                             const struct mipi_dsi_msg *msg)
{
    struct mipi_dsi_packet packet;
    u32 val;
    size_t i;
    int ret;

    if (msg->channel || msg->tx_len > 64 || msg->rx_len > 16)
        return -EINVAL;
    ret = mipi_dsi_create_packet(&packet, msg);
    if (ret) return ret;
    for (i = 0; i < packet.payload_length; i += sizeof(val)) {
        ret = readl_poll_timeout(diag.base + DSI_CMD_PKT_STATUS, val,
                                  !(val & GEN_TX_FULL), 10, 50000);
        if (ret) goto timeout;
        val = 0;
        memcpy(&val, packet.payload + i,
               min_t(size_t, sizeof(val), packet.payload_length - i));
        wr(DSI_GEN_PLD_DATA, le32_to_cpu((__force __le32)val));
    }
    ret = readl_poll_timeout(diag.base + DSI_CMD_PKT_STATUS, val,
                              !(val & GEN_CMD_FULL), 10, 50000);
    if (ret) goto timeout;
    wr(DSI_GEN_HDR, packet.header[0] | ((u32)packet.header[1] << 8) |
                    ((u32)packet.header[2] << 16));
    if (msg->rx_len) {
        ret = readl_poll_timeout(diag.base + DSI_CMD_PKT_STATUS, val,
                                  !(val & GEN_RD_BUSY), 10, 50000);
        if (ret) goto timeout;
        for (i = 0; i < msg->rx_len; i += sizeof(val)) {
            ret = readl_poll_timeout(diag.base + DSI_CMD_PKT_STATUS, val,
                                      !(val & GEN_RX_EMPTY), 10, 50000);
            if (ret) goto timeout;
            val = (__force u32)cpu_to_le32(rd(DSI_GEN_PLD_DATA));
            memcpy((u8 *)msg->rx_buf + i, &val,
                   min_t(size_t, sizeof(val), msg->rx_len - i));
        }
        return msg->rx_len;
    }
    ret = readl_poll_timeout(diag.base + DSI_CMD_PKT_STATUS, val,
                             (val & (GEN_CMD_EMPTY | GEN_TX_EMPTY)) ==
                              (GEN_CMD_EMPTY | GEN_TX_EMPTY), 10, 50000);
    if (ret) goto timeout;
    return msg->tx_len;
timeout:
    pr_warn("lpi4a-tl060-dsi: packet type=%02x tx=%zu rx=%zu timed out\n",
            msg->type, msg->tx_len, msg->rx_len);
    return ret;
}
static const struct mipi_dsi_host_ops host_ops = {
    .attach = host_attach, .detach = host_detach, .transfer = host_transfer,
};

static u32 horizontal_cycles(unsigned int pixels)
{
    return DIV_ROUND_UP_ULL((u64)pixels * 950000000ULL,
                            (u64)diag.pixel_rate * 8);
}

static int start_stream(void)
{
    const struct drm_display_mode *m = &diag.mode;
    int ret;

    if (diag.streaming) return 0;
    diag.pixel_rate = (unsigned long)m->crtc_clock * 1000;
    if (diag.pixel_rate != 148500000) return -EINVAL;
    if (!diag.awake) {
        ret = mipi_dsi_dcs_exit_sleep_mode(diag.panel);
        if (ret < 0) return ret;
        diag.awake = true;
        msleep(120);
        ret = mipi_dsi_dcs_set_display_on(diag.panel);
        if (ret < 0) return ret;
        msleep(30);
    }
    ret = clk_prepare_enable(diag.pixel_gate);
    if (ret) return ret;
    diag.pixel_gate_enabled = true;

    wr(DSI_PWR_UP, 0);
    wr(0x0c, 0); // DPI VC0.
    wr(0x10, 5); // RGB888.
    wr(0x14, BIT(1) | BIT(2)); // Negative VSYNC/HSYNC.
    wr(0x18, (4 << 16) | 4);
    wr(0x3c, m->hdisplay); // VID_PKT_SIZE.
    wr(0x40, 0); wr(0x44, 0); // Burst mode, no null chunks.
    wr(0x48, horizontal_cycles(m->hsync_end - m->hsync_start));
    wr(0x4c, horizontal_cycles(m->htotal - m->hsync_end));
    wr(0x50, horizontal_cycles(m->htotal));
    wr(0x54, m->vsync_end - m->vsync_start);
    wr(0x58, m->vtotal - m->vsync_end);
    wr(0x5c, m->vsync_start - m->vdisplay);
    wr(0x60, m->vdisplay);
    wr(0x38, GENMASK(13, 8) | 2); // Real DPI input, burst video; VPG OFF.
    wr(DSI_LPCLK_CTRL, BIT(0));
    wr(DSI_MODE_CFG, 0);
    wr(DSI_PWR_UP, 1);
    diag.streaming = true;
    return 0;
}

static void cleanup(void)
{
    unsigned int i;
    if (diag.streaming) {
        wr(DSI_PWR_UP, 0);
        wr(DSI_MODE_CFG, 1);
        wr(DSI_LPCLK_CTRL, 0);
        wr(DSI_PWR_UP, 1);
    }
    if (diag.awake && diag.panel) {
        mipi_dsi_dcs_set_display_off(diag.panel);
        mipi_dsi_dcs_enter_sleep_mode(diag.panel);
        msleep(120);
    }
    if (diag.attached) mipi_dsi_detach(diag.panel);
    if (diag.panel) mipi_dsi_device_unregister(diag.panel);
    if (diag.host_registered) mipi_dsi_host_unregister(&diag.host);
    if (diag.dev) root_device_unregister(diag.dev);
    if (diag.modified) {
        wr(DSI_PWR_UP, 0);
        wr(DSI_PHY_RSTZ, 0);
        for (i = 0; i < ARRAY_SIZE(saved_offsets); ++i)
            wr(saved_offsets[i], saved_regs[i]);
        writel((readl(diag.vo + VO_DSI0_PHY_CFG) & ~VO_PHY_FREQ_MASK) |
               (diag.saved_phy_cfg & VO_PHY_FREQ_MASK), diag.vo + VO_DSI0_PHY_CFG);
    }
    if (diag.reset_touched)
        writel((readl(diag.vo + VO_DSI0_RESET) & ~BIT(0)) |
               (diag.saved_reset & BIT(0)), diag.vo + VO_DSI0_RESET);
    if (diag.pixel_gate_enabled) clk_disable_unprepare(diag.pixel_gate);
    if (!IS_ERR_OR_NULL(diag.pixel_gate)) clk_put(diag.pixel_gate);
    while (diag.enabled) clk_disable_unprepare(diag.clks[--diag.enabled]);
    for (i = 0; i < ARRAY_SIZE(diag.clks); ++i)
        if (!IS_ERR_OR_NULL(diag.clks[i])) clk_put(diag.clks[i]);
    if (diag.base) iounmap(diag.base);
    if (diag.vo) iounmap(diag.vo);
    if (diag.region) release_mem_region(DSI_BASE, DSI_SIZE);
    memset(&diag, 0, sizeof(diag));
}

static int init_hw(void)
{
    struct device_node *vo;
    const struct dsi_clock_layout *layout = &mainline_clocks;
    struct of_phandle_args args = {.args_count = 1};
    struct mipi_dsi_device_info info = {.type = "tl060fvxs07-drm", .channel = 0};
    u32 version, val;
    unsigned int i;
    int ret;

    if (!of_machine_is_compatible("thead,th1520")) return -ENODEV;
    if (!request_mem_region(DSI_BASE, DSI_SIZE, "lpi4a-tl060-dsi")) return -EBUSY;
    diag.region = true;
    diag.base = ioremap(DSI_BASE, DSI_SIZE);
    diag.vo = ioremap(VO_BASE, 0x80);
    if (!diag.base || !diag.vo) { ret = -ENOMEM; goto fail; }
    vo = of_find_compatible_node(NULL, NULL, "thead,th1520-clk-vo");
    if (!vo) {
        vo = of_find_compatible_node(NULL, NULL, "xuantie,vosys-gate-controller");
        layout = &xuantie_clocks;
    }
    if (!vo) { ret = -ENODEV; goto fail; }
    args.np = vo;
    for (i = 0; i < ARRAY_SIZE(layout->control); ++i) {
        args.args[0] = layout->control[i];
        diag.clks[i] = of_clk_get_from_provider(&args);
        if (IS_ERR(diag.clks[i])) {
            ret = PTR_ERR(diag.clks[i]); of_node_put(vo); goto fail;
        }
        ret = clk_prepare_enable(diag.clks[i]);
        if (ret) { of_node_put(vo); goto fail; }
        ++diag.enabled;
        pr_info("lpi4a-tl060-dsi: control clock id=%u name=%s\n",
                args.args[0], __clk_get_name(diag.clks[i]));
    }
    args.args[0] = layout->pixel; // DSI0 DPI gate; DPU0 remains CRTC-owned.
    diag.pixel_gate = of_clk_get_from_provider(&args);
    of_node_put(vo);
    if (IS_ERR(diag.pixel_gate)) { ret = PTR_ERR(diag.pixel_gate); goto fail; }
    pr_info("lpi4a-tl060-dsi: pixel clock id=%u name=%s\n",
            args.args[0], __clk_get_name(diag.pixel_gate));
    diag.saved_reset = readl(diag.vo + VO_DSI0_RESET);
    writel(diag.saved_reset | BIT(0), diag.vo + VO_DSI0_RESET);
    diag.reset_touched = true;
    usleep_range(10, 20);
    version = rd(DSI_VERSION);
    if ((version & 0xff000000) != 0x31000000) { ret = -ENODEV; goto fail; }
    /* The standalone DT gives this driver exclusive DSI0/PHY ownership.
     * Boot firmware may have left it enabled. Save configuration, then reset
     * only this controller below before programming the validated sequence.
     */
    if ((rd(DSI_PWR_UP) & BIT(0)) || (rd(DSI_PHY_RSTZ) & GENMASK(3, 0))) {
        pr_info("lpi4a-tl060-dsi: taking owned controller from pwr=%08x phy_rstz=%08x phy_status=%08x\n",
               rd(DSI_PWR_UP), rd(DSI_PHY_RSTZ), rd(DSI_PHY_STATUS));
    }
    diag.saved_phy_cfg = readl(diag.vo + VO_DSI0_PHY_CFG);
    for (i = 0; i < ARRAY_SIZE(saved_offsets); ++i) saved_regs[i] = rd(saved_offsets[i]);
    diag.modified = true;
    wr(DSI_PWR_UP, 0);
    wr(DSI_PHY_RSTZ, 0);
    wr(DSI_INT_MSK0, ~0U); wr(DSI_INT_MSK1, ~0U);
    update(DSI_PHY_TST_CTRL0, BIT(0), BIT(0));
    update(DSI_PHY_TST_CTRL0, BIT(0), 0);
    /* Board-validated 950-Mbit/s lane setting; config oscillator=24 MHz
     * gives CFGCLKFREQRANGE=(24-17)*4=28. Only DSI0's register is changed. */
    val = FIELD_PREP(GENMASK(9, 3), 0x3a) |
          FIELD_PREP(GENMASK(15, 10), 28);
    writel((diag.saved_phy_cfg & ~VO_PHY_FREQ_MASK) | val, diag.vo + VO_DSI0_PHY_CFG);
    phy_test_write(0xa3, 0x00); // Disable slew-rate calibration, vendor sequence.
    phy_test_write(0xa0, 0x01);
    phy_test_write(0x4a, 0x40); // Enable lane-0 LP bias.
    phy_test_write(0x0e, 0x08); // Proportional charge pump, <1150 Mbps.
    phy_test_write(0x1c, 0x10); // Charge-pump bias.
    wr(DSI_PHY_IF_CFG, (2 << 8) | 3);
    wr(DSI_PHY_TMR_LPCLK_CFG, (62 << 16) | 120);
    wr(DSI_PHY_TMR_CFG, (36 << 16) | 93);
    wr(DSI_PHY_TMR_RD_CFG, 10000);
    wr(DSI_CLKMGR_CFG, (10 << 8) | 6);
    wr(DSI_TO_CNT_CFG, (1000 << 16) | 1000);
    wr(DSI_BTA_TO_CNT, 0xd00);
    wr(DSI_PCKHDL_CFG, BIT(5) | BIT(4) | BIT(3) | BIT(2) | BIT(0));
    wr(DSI_GEN_VCID, 0);
    wr(DSI_CMD_MODE_CFG, BIT(24) | GENMASK(19, 16) | GENMASK(14, 8));
    wr(DSI_MODE_CFG, 1);
    wr(DSI_LPCLK_CTRL, 0);
    wr(DSI_PHY_RSTZ, BIT(2));
    update(DSI_PHY_RSTZ, BIT(0), BIT(0));
    update(DSI_PHY_RSTZ, BIT(1), BIT(1));
    ret = readl_poll_timeout(diag.base + DSI_PHY_STATUS, val,
                              val & PHY_LOCK, 10, 100000);
    if (ret) goto fail;
    ret = readl_poll_timeout(diag.base + DSI_PHY_STATUS, val,
                              (val & PHY_STOP_ALL) == PHY_STOP_ALL, 10, 100000);
    if (ret) pr_warn("lpi4a-tl060-dsi: not all lanes in LP11: %08x\n", val);
    wr(DSI_PWR_UP, 1);
    diag.dev = root_device_register("th1520-dsi0-bridge");
    if (IS_ERR(diag.dev)) { ret = PTR_ERR(diag.dev); diag.dev = NULL; goto fail; }
    diag.host.dev = diag.dev;
    diag.host.ops = &host_ops;
    ret = mipi_dsi_host_register(&diag.host);
    if (ret) goto fail;
    diag.host_registered = true;
    diag.panel = mipi_dsi_device_register_full(&diag.host, &info);
    if (IS_ERR(diag.panel)) { ret = PTR_ERR(diag.panel); diag.panel = NULL; goto fail; }
    diag.panel->lanes = 4;
    diag.panel->format = MIPI_DSI_FMT_RGB888;
    diag.panel->mode_flags = MIPI_DSI_MODE_LPM;
    ret = mipi_dsi_attach(diag.panel);
    if (ret) goto fail;
    diag.attached = true;
    ret = mipi_dsi_dcs_exit_sleep_mode(diag.panel);
    if (ret < 0) goto fail;
    diag.awake = true;
    msleep(120);
    ret = mipi_dsi_dcs_set_display_on(diag.panel);
    if (ret < 0) goto fail;
    msleep(30);
    return 0;
fail:
    pr_err("lpi4a-tl060-dsi: probe failed: %d\n", ret);
    cleanup();
    return ret;
}

static const struct drm_display_mode panel_mode = {
    .clock = 148500,
    .hdisplay = 1080, .hsync_start = 1195, .hsync_end = 1198, .htotal = 1211,
    .vdisplay = 2160, .vsync_start = 2170, .vsync_end = 2172, .vtotal = 2182,
    .flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_NVSYNC,
    .type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int panel_get_modes(struct drm_bridge *bridge, struct drm_connector *connector)
{
    struct drm_display_mode *mode = drm_mode_duplicate(connector->dev, &panel_mode);
    static const u32 format = MEDIA_BUS_FMT_RGB888_1X24;
    int ret;

    if (!mode) return -ENOMEM;
    ret = drm_display_info_set_bus_formats(&connector->display_info, &format, 1);
    if (ret) { drm_mode_destroy(connector->dev, mode); return ret; }
    connector->display_info.bpc = 8;
    connector->display_info.bus_flags = DRM_BUS_FLAG_DE_HIGH | DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE;
    drm_mode_set_name(mode);
    drm_mode_probed_add(connector, mode);
    return 1;
}

static enum drm_connector_status panel_detect(struct drm_bridge *bridge)
{
    return connector_status_connected;
}

static enum drm_mode_status panel_mode_valid(struct drm_bridge *bridge,
        const struct drm_display_info *info, const struct drm_display_mode *mode)
{
    return drm_mode_equal(mode, &panel_mode) ? MODE_OK : MODE_BAD;
}

static void panel_mode_set(struct drm_bridge *bridge,
        const struct drm_display_mode *mode, const struct drm_display_mode *adjusted)
{
    diag.mode = *adjusted;
}

static void panel_pre_enable(struct drm_bridge *bridge, struct drm_bridge_state *state)
{
    int ret = start_stream();
    if (ret) pr_err("vs-dsi0: stream start failed %d\n", ret);
}

static void panel_enable(struct drm_bridge *bridge, struct drm_bridge_state *state)
{
    struct lpi4a_bridge *ctx = to_lpi4a(bridge);
    int ret;

    if (!diag.streaming || !ctx->backlight)
        return;
    /* Pair disable with enable and preserve the requested brightness across
     * DPMS and modesets. The module parameter is only an initial default;
     * reapplying it here would erase subsequent userspace brightness changes.
     */
    ret = backlight_enable(ctx->backlight);
    if (ret) {
        backlight_disable(ctx->backlight);
        dev_err(ctx->backlight->dev.parent, "TL060 backlight enable failed: %d\n", ret);
    }
}

static void panel_disable(struct drm_bridge *bridge, struct drm_bridge_state *state)
{
    struct lpi4a_bridge *ctx = to_lpi4a(bridge);
    if (ctx->backlight)
        backlight_disable(ctx->backlight);
    if (diag.streaming) {
        wr(DSI_PWR_UP, 0);
        wr(DSI_MODE_CFG, 1);
        wr(DSI_LPCLK_CTRL, 0);
        wr(DSI_PWR_UP, 1);
        diag.streaming = false;
    }
    if (diag.awake) {
        mipi_dsi_dcs_set_display_off(diag.panel);
        mipi_dsi_dcs_enter_sleep_mode(diag.panel);
        msleep(120);
        diag.awake = false;
    }
    if (diag.pixel_gate_enabled) {
        clk_disable_unprepare(diag.pixel_gate);
        diag.pixel_gate_enabled = false;
    }
}

static u32 *panel_input_formats(struct drm_bridge *bridge,
        struct drm_bridge_state *bridge_state, struct drm_crtc_state *crtc_state,
        struct drm_connector_state *conn_state, u32 output_fmt, unsigned int *count)
{
    return drm_atomic_helper_bridge_propagate_bus_fmt(bridge, bridge_state,
            crtc_state, conn_state, MEDIA_BUS_FMT_RGB888_1X24, count);
}

static int panel_attach(struct drm_bridge *bridge, enum drm_bridge_attach_flags flags)
{
    struct drm_connector *connector;
    int ret;
    if (!try_module_get(THIS_MODULE))
        return -ENODEV;
    if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR)
        return 0;
    connector = drm_bridge_connector_init(bridge->dev, bridge->encoder);
    if (IS_ERR(connector)) {
        ret = PTR_ERR(connector);
        goto fail;
    }
    ret = drm_connector_attach_encoder(connector, bridge->encoder);
    if (ret)
        goto fail;
    return 0;
fail:
    module_put(THIS_MODULE);
    return ret;
}

static void panel_detach(struct drm_bridge *bridge)
{
    module_put(THIS_MODULE);
}

static const struct drm_bridge_funcs panel_funcs = {
    .attach = panel_attach,
    .detach = panel_detach,
    .get_modes = panel_get_modes,
    .detect = panel_detect,
    .mode_valid = panel_mode_valid,
    .mode_set = panel_mode_set,
    .atomic_pre_enable = panel_pre_enable,
    .atomic_enable = panel_enable,
    .atomic_disable = panel_disable,
    .atomic_get_input_bus_fmts = panel_input_formats,
    .atomic_duplicate_state = drm_atomic_helper_bridge_duplicate_state,
    .atomic_destroy_state = drm_atomic_helper_bridge_destroy_state,
    .atomic_reset = drm_atomic_helper_bridge_reset,
};

/* Some DC implementations enlist the host graph node as a component. The
 * bridge itself is already registered and needs no private DC bind callback.
 */
static int component_bind(struct device *dev, struct device *master, void *data)
{
    return 0;
}
static void component_unbind(struct device *dev, struct device *master, void *data) { }
static const struct component_ops component_ops = {
    .bind = component_bind, .unbind = component_unbind,
};

static int standalone_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct lpi4a_bridge *ctx;
    struct device_node *bl;
    struct device_node *phy;
    u32 levels[3];
    u8 power_mode = 0;
    int ret;

    if (!dev->of_node || !dev->of_node->parent ||
        !of_machine_is_compatible("sipeed,lichee-pi-4a") || diag.dev)
        return -ENODEV;
    phy = of_get_child_by_name(dev->of_node->parent, "dsi0-dphy");
    if (phy && of_device_is_available(phy)) {
        of_node_put(phy);
        return dev_err_probe(dev, -EBUSY, "native DSI0 PHY must be disabled in DT\n");
    }
    of_node_put(phy);
    if (!of_find_property(dev->of_node, "vddio-supply", NULL) ||
        !of_find_property(dev->of_node, "vdd-supply", NULL))
        return dev_err_probe(dev, -EINVAL, "explicit panel supplies required\n");
    ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx) return -ENOMEM;
    ctx->supplies[0].supply = "vddio";
    ctx->supplies[1].supply = "vdd";
    ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies), ctx->supplies);
    if (ret) return dev_err_probe(dev, ret, "panel supplies\n");
    ctx->backlight = devm_of_find_backlight(dev);
    if (IS_ERR(ctx->backlight)) return PTR_ERR(ctx->backlight);
    if (ctx->backlight) {
        bl = of_parse_phandle(dev->of_node, "backlight", 0);
        ret = of_property_read_u32_array(bl, "brightness-levels", levels, 3);
        of_node_put(bl);
        if (ret || levels[0] || levels[1] != 20 || levels[2] != 100 ||
            ctx->backlight->props.max_brightness != 2)
            return dev_err_probe(dev, -EINVAL, "expected brightness levels0,20,100\n");
        ret = backlight_disable(ctx->backlight);
        if (ret) return dev_err_probe(dev, ret, "backlight disable\n");
        ctx->backlight->props.brightness = backlight ? 1 : 0;
    } else if (backlight) {
        return dev_err_probe(dev, -EINVAL, "backlight phandle missing\n");
    }
    ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
    if (ret) return ret;
    ctx->powered = true;
    msleep(40);
    ret = init_hw();
    if (ret) goto power_off;
    ret = mipi_dsi_dcs_get_power_mode(diag.panel, &power_mode);
    if (ret < 0 || !lpi4a_tl060_power_ready(power_mode)) {
        dev_err(dev, "panel status read failed: ret=%d power=%02x\n", ret, power_mode);
        ret = ret < 0 ? ret : -EIO;
        cleanup();
        goto power_off;
    }
    dev_info(dev, "standalone panel status=%02x;148.5MHz/950Mbps;backlight=%d\n",
             power_mode, backlight);
    diag.mode = panel_mode;
    drm_mode_set_crtcinfo(&diag.mode, 0);
    ctx->bridge.funcs = &panel_funcs;
    ctx->bridge.of_node = dev->of_node;
    ctx->bridge.type = DRM_MODE_CONNECTOR_DSI;
    ctx->bridge.ops = DRM_BRIDGE_OP_MODES | DRM_BRIDGE_OP_DETECT;
    platform_set_drvdata(pdev, ctx);
    drm_bridge_add(&ctx->bridge);
    ret = component_add(dev, &component_ops);
    if (!ret) return 0;
    drm_bridge_remove(&ctx->bridge);
    cleanup();
power_off:
    regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
    ctx->powered = false;
    return ret;
}

static int standalone_remove(struct platform_device *pdev)
{
    struct lpi4a_bridge *ctx = platform_get_drvdata(pdev);
    component_del(&pdev->dev, &component_ops);
    drm_bridge_remove(&ctx->bridge);
    if (ctx->backlight) backlight_disable(ctx->backlight);
    cleanup();
    if (ctx->powered)
        regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
    return 0;
}

static const struct of_device_id standalone_match[] = {
    { .compatible = "sipeed,lpi4a-tl060-dsi" }, { }
};
MODULE_DEVICE_TABLE(of, standalone_match);
static struct platform_driver standalone_driver = {
    .probe = standalone_probe, .remove = standalone_remove,
    .driver = { .name = "lpi4a-tl060", .of_match_table = standalone_match,
                .suppress_bind_attrs = true },
};
module_platform_driver(standalone_driver);
MODULE_AUTHOR("LoveSy <shana@zju.edu.cn>");
MODULE_DESCRIPTION("Standalone LPi4A TL060FVXS07 DRM bridge");
MODULE_LICENSE("GPL");
