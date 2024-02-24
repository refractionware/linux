# Tool for getting the values of Exynos 4 DSIM registers and converting them
# to DT properties for the dsi_0 node.
# See https://knuxify.github.io/blog/2023/04/tab3-display.html for more information.
# You can use the code from https://github.com/torvalds/linux/blob/v6.3-rc7/drivers/gpu/drm/exynos/exynos_drm_dsi.c#L800-L866
# to convert these bits to actual mipi data.
print("""Instructions:
In the downstream kernel, run:
   cat /sys/devices/platform/s5p-dsim.0/dsim_dump""")

dsim_cfg = input("Then copy out the value from \"[DSIM]0x11C8_0010\" and paste it here, with the 0x prefix: ")
dsim_cfg = int(dsim_cfg, 16)

dsim_cfg_bits=[('DSIM_HSA_DISABLE_MODE', 20), ('DSIM_HBP_DISABLE_MODE', 21), ('DSIM_HFP_DISABLE_MODE', 22), ('DSIM_HSE_DISABLE_MODE', 23), ('DSIM_AUTO_MODE', 24), ('DSIM_VIDEO_MODE', 25), ('DSIM_BURST_MODE', 26), ('DSIM_SYNC_INFORM', 27), ('DSIM_EOT_DISABLE', 28), ('DSIM_MFLUSH_VS', 29)]
for name, bit in dsim_cfg_bits:
    print(name, bool(dsim_cfg & (1<<bit)))
