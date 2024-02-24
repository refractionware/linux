# Tool for getting the values of Exynos 4 FIMD registers and converting them
# to DT properties for the fimd node.
# See https://knuxify.github.io/blog/2023/04/tab3-display.html for more information.
# You can use the code from drivers/gpu/drm/exynos/exynos_drm_fimd.c
# to convert these bits to actual parameters.
print("""Instructions:
In the downstream kernel, run:
   cat /sys/class/graphics/fb0/device/fimd_dump
Then, from 11C00000 (the first line), copy out:""")

vidcon0_cfg = input("The first 8-digit value: ")
vidcon0_cfg = int('0x' + vidcon0_cfg if not vidcon0_cfg.startswith('0x') else vidcon0_cfg, 16)

vidcon1_cfg = input("The second 8-digit value: ")
vidcon1_cfg = int('0x' + vidcon1_cfg if not vidcon1_cfg.startswith('0x') else vidcon1_cfg, 16)

# include/video/samsung_fimd.h
vidcon1_regs = (
    ("VIDCON1_INV_VCLK", 7),
    ("VIDCON1_INV_HSYNC", 6),
    ("VIDCON1_INV_VSYNC", 5),
    ("VIDCON1_INV_VDEN", 4),
)

for regname, shift in vidcon1_regs:
    print(regname, vidcon1_cfg & (1 << shift))

print("VIDCON0_DSI_EN", vidcon0_cfg & (1 << 30))
