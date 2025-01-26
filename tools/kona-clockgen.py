# kona-clockgen - given a BCM Kona clock definition from clock.c, generate a mainline struct for it.

import argparse
from dataclasses import dataclass, fields
from typing import Optional, Union, Self
import os.path
import re

# World's Worst C parser
rex = re.compile(r'\W+')
def unroll_macro(header_str: str, macro_name: str):
    """Find a macro in the header and unroll it."""
    out = ""
    for line in header_str.split('\n'):
        if "#define" in line and macro_name in line:
            out = line.strip().replace('\t', ' ')
            if line.strip().endswith(r'/'):
                out = out[:-1]
                continue
            break
        if out:
            out += line.strip()
            if not line.strip().endswith(r'/'):
                break
            else:
                out = out[:-1]
    macro_value = ' '.join(rex.sub(' ', out).split(" ")[3:])

    if '0x' in macro_value:
        return int(macro_value, 16)
    elif macro_value.isnumeric():
        return int(macro_value)
    return macro_value

def struct_to_dict(struct_str: str):
    """Convert a C struct to a dictionary."""
    out = {}
    nest = []

    # Step 1. Skip first line and last line.
    struct_str = '\n'.join(struct_str.split('\n')[1:-1])

    # Step 2. Strip out any and all whitespace.
    struct_str = struct_str.replace(' ', '')
    struct_str = struct_str.replace('\n', '')
    struct_str = struct_str.replace('\t', '')
    struct_str = struct_str.strip()

    # Step 3. Identify key/value pairs.
    for item in struct_str.split('.'):
        if not item:
            continue
        try:
            key, value = [x.strip() for x in item.split('=')]
        except:
            raise ValueError(item)
        if value == '{':
            nest.append(key)
            if len(nest) > 1:
                raise ValueError("TODO")
            out[key] = {}
        elif '}' in value:
            nest.pop()
        else:
            if nest:
                out[nest[-1]][key] = value.split(',')[0] # TODO
            else:
                out[key] = value.split(',')[0]

    # Step 4. Return finished struct.
    return out

def find_clock(clock_str: str, name: str) -> Optional[Union[str, str]]:
    """Find the struct containing clock data. Returns union of (typestr, struct)."""
    out = []
    clk_type = None
    for line in clock_str.split('\n'):
        if f"CLK_NAME({name})" in line:
            if "bus_clk" in line:
                clk_type = "bus"
            elif "peri_clk" in line:
                clk_type = "peri"
            elif "ref_clk" in line:
                clk_type = "ref"
            else:
                raise ValueError(f"Unknown clock type for {line}")

        if clk_type is not None:
            out.append(line)
            if "};" in line:
                break

    if clk_type is None:
        return None

    return (clk_type, '\n'.join(out))

# Dataclasses go brrr

def unroll_all_macros(clock_dict: dict):
    out = clock_dict.copy()
    for key, value in clock_dict.items():
        if isinstance(value, dict):
            out[key] = unroll_all_macros(value)
        elif isinstance(value, str) and "CLK_MGR_REG" in value:
            header_name = 'brcm_rdb_' + value.split('_')[0].lower() + '_clk_mgr_reg.h'
            header_path = os.path.join(args.kernel_path, 'arch', 'arm', f'mach-{args.mach}', 'include', 'mach', 'rdb', header_name)
            with open(header_path) as header_raw:
                out[key] = unroll_macro(header_raw.read(), value)

    return out

def mask_to_width(mask: int):
    return bin(mask).count('1')

@dataclass
class ClockInfo:
    flags: str = ""

    @classmethod
    def from_dict(cls, payload: dict) -> Self:
        return cls(flags=payload.get('flags', ""))

@dataclass
class ClockDiv:
    div_offset: Optional[int] = None
    div_mask: Optional[int] = None
    div_shift: Optional[int] = None
    pre_div_offset: Optional[int] = None
    pre_div_mask: Optional[int] = None
    pre_div_shift: Optional[int] = None
    div_trig_offset: Optional[int] = None
    div_trig_mask: Optional[int] = None
    div_trig_shift: Optional[int] = None
    prediv_trig_offset: Optional[int] = None
    prediv_trig_mask: Optional[int] = None
    prediv_trig_shift: Optional[int] = None
    diether_bits: Optional[int] = None
    pll_select_offset: Optional[int] = None
    pll_select_mask: Optional[int] = None
    pll_select_shift: Optional[int] = None

    @classmethod
    def from_dict(cls, payload: dict) -> Self:
        kwargs = {}
        for field in fields(cls):
            if field.name in payload:
                kwargs[field.name] = payload.get(field.name)
                if field.name.endswith('mask') and field.name[:-4] + 'shift' not in payload:
                    kwargs[field.name[:-4] + 'shift'] = payload[field.name][:-4] + 'SHIFT'

        kwargs = unroll_all_macros(kwargs)

        return cls(**kwargs)


@dataclass
class PeriClock:
    __clk_type__ = 'peri'

    clk_gate_offset: Optional[int] = None
    clk_en_mask: Optional[int] = None
    gating_sel_mask: Optional[int] = None
    hyst_val_mask: Optional[int] = None
    hyst_en_mask: Optional[int] = None
    stprsts_mask: Optional[int] = None
    volt_lvl_mask: Optional[int] = None
    clk_div: Optional[ClockDiv] = None
    policy_bit_mask: Optional[int] = None
    policy_bit_shift: Optional[int] = None
    policy_bit_init: Optional[str] = None

    # Downstream does not have these, but mainline needs them!
    clk_en_shift: Optional[int] = None
    gating_sel_shift: Optional[int] = None
    hyst_val_shift: Optional[int] = None
    hyst_en_shift: Optional[int] = None
    stprsts_shift: Optional[int] = None
    volt_lvl_shift: Optional[int] = None

    src: Optional[str] = None
    clk: Optional[ClockInfo] = None
    name: Optional[str] = None

    mask_set: Optional[int] = None

    @classmethod
    def from_dict(cls, payload: dict, name: Optional[str] = None) -> Self:
        kwargs = {}
        for field in fields(cls):
            if field.name in payload:
                if field.name == 'clk_div':
                    kwargs[field.name] = ClockDiv.from_dict(payload[field.name])
                elif field.name == 'clk':
                    kwargs[field.name] = ClockInfo.from_dict(payload[field.name])
                else:
                    kwargs[field.name] = payload[field.name]
                    if field.name.endswith('mask'):
                        kwargs[field.name[:-4] + 'shift'] = payload[field.name][:-4] + 'SHIFT'

        kwargs = unroll_all_macros(kwargs)
        kwargs['name'] = name

        return cls(**kwargs)

    def to_mainline_struct(self):
        """Returns the mainline struct."""
        out = [f'static struct {self.__clk_type__}_clk_data {self.name}_data = {{']

        # .clocks: CLOCKS(), fill with names of source clocks; TODO
        has_clocks = False
        if self.src:
            print("CLOCKS todo", self.src)
            has_clocks = True

        # .policy:
        if self.policy_bit_mask is not None:
            out.append(f'\t.policy\t\t= POLICY(0xFIXME_{self.mask_set}, {self.policy_bit_shift}),')

        # .gate:
        if self.gating_sel_shift is not None:
            auto = ''
            if 'AUTO_GATE' in self.clk.flags:
                auto = '_AUTO'
            out.append(f'\t.gate\t\t= HW_SW_GATE{auto}(0x{self.clk_gate_offset:04x}, {self.stprsts_shift}, {self.gating_sel_shift}, {self.clk_en_shift}),')
        else:
            hwsw = 'SW'
            if 'AUTO_GATE' in self.clk.flags:
                hwsw = 'HW'
            out.append(f'\t.gate\t\t= {hwsw}_ONLY_GATE(0x{self.clk_gate_offset:04x}, {self.stprsts_shift}, {self.clk_en_shift}),')

        # .hyst:
        if self.hyst_val_mask is not None and int(self.hyst_en_mask):
            out.append(f'\t.hyst\t\t= HYST(0x{self.clk_gate_offset:04x}, {self.hyst_val_shift}, {self.hyst_en_shift}),')

        if self.clk_div is not None:
            # .sel:
            if self.clk_div.pll_select_offset is not None:
                out.append(f'\t.sel\t\t= SELECTOR(0x{self.clk_div.pll_select_offset:04x}, {self.clk_div.pll_select_shift}, {mask_to_width(self.clk_div.pll_select_mask)}),')
            # .div:
            if self.clk_div.div_offset is not None:
                if self.clk_div.diether_bits is not None:
                    print("TODO verify diether bits")
                    out.append(f'\t.div\t\t= FRAC_DIVIDER(0x{self.clk_div.div_offset:04x}, {self.clk_div.div_shift}, {mask_to_width(self.clk_div.div_mask)}, {self.clk_div.diether_bits}),')
                else:
                    out.append(f'\t.div\t\t= DIVIDER(0x{self.clk_div.div_offset:04x}, {self.clk_div.div_shift}, {mask_to_width(self.clk_div.div_mask)}),')
            # .trig:
            if self.clk_div.div_trig_offset is not None:
                out.append(f'\t.trig\t\t= TRIGGER(0x{self.clk_div.div_trig_offset:04x}, {self.clk_div.div_trig_shift}),')

        out.append('};')

        return '\n'.join(out)

class BusClock(PeriClock):
    __clk_type__ = 'bus'

    freq_tbl_index: Optional[int] = None

class RefClock(PeriClock):
    __clk_type__ = 'ref'

# Actual program logic

parser = argparse.ArgumentParser(
                    prog='kona-clockgen.py',
                    description='Generate clock data struct')

parser.add_argument("kernel_path", help="Path to downstream kernel source")
parser.add_argument("mach", help="Machine (mach-XXX folder)")
parser.add_argument("name", help="Name of the clock to extract")

args = parser.parse_args()


with open(os.path.join(args.kernel_path, 'arch', 'arm', f'mach-{args.mach}', 'clock.c')) as clock_in:
    clock_raw = find_clock(clock_in.read(), args.name)
    if not clock_raw:
        print(f"Failed to find clock: {args.name}")
        quit(1)
    clock_dict = struct_to_dict(clock_raw[1])

    if clock_raw[0] == 'peri':
        clk = PeriClock.from_dict(clock_dict, name=args.name)
    elif clock_raw[0] == 'bus':
        clk = BusClock.from_dict(clock_dict, name=args.name)
    elif clock_raw[0] == 'ref':
        clk = RefClock.from_dict(clock_dict, name=args.name)

    print(clk.to_mainline_struct())
