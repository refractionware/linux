#!/usr/bin/python3

# MODIFY WITH YOUR VALUES:
CHG_CNFG_01 = 0x34  # 0xB8
CHG_CNFG_02 = 0xb6  # 0xB9
CHG_CNFG_03 = 0x00  # 0xBA
CHG_CNFG_04 = 0x04  # 0xBB
CHG_CNFG_07 = 0x9e  # 0xBE
CHG_CNFG_09 = 0x2d  # 0xC0
CHG_CNFG_12 = 0x07  # 0xC3

# Input current limit
chgin_current_limit_raw = CHG_CNFG_09 & 0x7F
if chgin_current_limit_raw < 3:
    chgin_current_limit = 60000
else:
    chgin_current_limit = chgin_current_limit_raw * 20000

# Fast charge current limit
cc_raw = CHG_CNFG_02 & 0x3F
cc = cc_raw * 33300

# Charging constant voltage
chg_constant_volt_raw = CHG_CNFG_04 & 0x1f
if chg_constant_volt_raw < 0x1c:
    chg_constant_volt = 3650000 + chg_constant_volt_raw * 25000
elif chg_constant_volt_raw == 0x1c:
    chg_constant_volt = 4340000
else:
    chg_constant_volt = 4350000 + ((chg_constant_volt_raw - 0x1d) * 25000)

# Minimum system regulation voltage
minvsys_raw = (CHG_CNFG_04 & (0x7 << 5)) >> 5
minvsys = 3000000 + minvsys_raw * 100000

# Thermal regulation temp
regtemp_raw = (CHG_CNFG_07 & (0x3 << 5)) >> 5
regtemp = 70 + regtemp_raw * 15

# Battery overcurrent
bat_overcurrent_raw = CHG_CNFG_12 & 0x7
if bat_overcurrent_raw != 0:
    bat_overcurrent = 2000000 + (bat_overcurrent_raw - 1) * 250000
else:
    bat_overcurrent = 0

# Charge input threshold
charge_input_threshold_raw = (CHG_CNFG_12 & (0x3 << 3)) >> 3
if charge_input_threshold_raw == 0:
    charge_input_threshold = 4300000
else:
    charge_input_threshold = 4700000 + (charge_input_threshold_raw - 1) * 100000

# --- #

print("Charger parameters:")
print(f"maxim,constant-microvolt: <{chg_constant_volt}>;")
print(f"maxim,min-system-microvolt: <{minvsys}>;")
print(f"maxim,thermal-regulation-celsius: <{regtemp}>;")
print(f"maxim,battery-overcurrent-microamp: <{bat_overcurrent}>;")
print(f"maxim,charge-input-threshold-microvolt: <{charge_input_threshold}>;")
print()
print("Battery node parameters:")
print(f"constant-charge-current-max-microamp: <{cc}>;")

