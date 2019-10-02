#include "cpu.h"
#include "common.h"

#include "cpu_ops.h"

#include "cpu_ops_table.h"

void cpu_t::unhandled_opcode() { printf("oh man oh geez\n"); }

void cpu_t::init(cpu_type _type)
{
    type = _type;
    delayed_interrupt_enable = false;
    eflags.whole = 2;

    for (int i = 0; i < 6; i++)
    {
        segs[i].selector = 0;
        segs[i].base = 0;
        segs[i].limit = 0xffff;
        segs[i].flags = 0x0093;
    }

    segs[cs].selector = 0xf000;
    segs[cs].base = 0xff0000;
    segs[cs].flags = 0x009b;

    ip = 0x0000fff0;

    cr[0] = 0xfff0;

    for (int i = 0; i < 256; i++)
    {
        opcode_table_1byte_16[i] = &cpu_t::unhandled_opcode;
    }

    for (auto op_info : cpu_opcode_table)
    {
        //TODO
        if (op_info.flags & OP_2BYTE)
        {
        }
        else
        {
            opcode_table_1byte_16[op_info.opcode] = op_info.handler16;
        }
    }
}

u8 cpu_t::rb_phys(addr_t addr) { return rb_real(device, addr); }

u16 cpu_t::rw_phys(addr_t addr) { return rw_real(device, addr); }

u32 cpu_t::rl_phys(addr_t addr) { return rl_real(device, addr); }

void cpu_t::wb_phys(addr_t addr, u8 data) { wb_real(device, addr, data); }

void cpu_t::ww_phys(addr_t addr, u16 data) { ww_real(device, addr, data); }

void cpu_t::wl_phys(addr_t addr, u32 data) { wl_real(device, addr, data); }

u8 cpu_t::iorb(u16 addr) { return iorb_real(device, addr); }

u16 cpu_t::iorw(u16 addr) { return iorw_real(device, addr); }

u32 cpu_t::iorl(u16 addr) { return iorl_real(device, addr); }

void cpu_t::iowb(u16 addr, u8 data) { iowb_real(device, addr, data); }

void cpu_t::ioww(u16 addr, u16 data) { ioww_real(device, addr, data); }

void cpu_t::iowl(u16 addr, u32 data) { iowl_real(device, addr, data); }

addr_t cpu_t::translate_addr(x86seg *segment, u32 offset, translate_kind kind)
{
    return segment->base + offset;
}

void cpu_t::load_segment(int segment, u16 selector)
{
    bool protected_mode = cr[0] & 1;
    if (protected_mode)
    {
        unimplemented("Protected mode segment loads are not implemented yet.\n");
    }
    else
    {
        segs[segment].selector = selector;
        segs[segment].base = selector << 4;
        if (segment == cs)
        {
            segs[segment].flags = 0x009b;
        }
    }
}

u8 cpu_t::fetchb(u32 offset)
{
    addr_t addr = translate_addr(&segs[cs], offset, translate_kind::TRANSLATE_EXEC);
    return rb_phys(addr);
}

u16 cpu_t::fetchw(u32 offset)
{
    addr_t addr = translate_addr(&segs[cs], offset + 1, translate_kind::TRANSLATE_EXEC) - 1;
    return rw_phys(addr);
}

u32 cpu_t::fetchl(u32 offset)
{
    addr_t addr = translate_addr(&segs[cs], offset + 3, translate_kind::TRANSLATE_EXEC) - 3;
    return rl_phys(addr);
}

u8 cpu_t::rb(x86seg *segment, u32 offset)
{
    addr_t addr = translate_addr(segment, offset, translate_kind::TRANSLATE_READ);
    return rb_phys(addr);
}

u16 cpu_t::rw(x86seg *segment, u32 offset)
{
    addr_t addr = translate_addr(segment, offset + 1, translate_kind::TRANSLATE_READ) - 1;
    return rw_phys(addr);
}

u32 cpu_t::rl(x86seg *segment, u32 offset)
{
    addr_t addr = translate_addr(segment, offset + 3, translate_kind::TRANSLATE_READ) - 3;
    return rl_phys(addr);
}

void cpu_t::wb(x86seg *segment, u32 offset, u8 data)
{
    addr_t addr = translate_addr(segment, offset, translate_kind::TRANSLATE_WRITE);
    wb_phys(addr, data);
}

void cpu_t::ww(x86seg *segment, u32 offset, u16 data)
{
    addr_t addr = translate_addr(segment, offset + 1, translate_kind::TRANSLATE_WRITE) - 1;
    ww_phys(addr, data);
}

void cpu_t::wl(x86seg *segment, u32 offset, u32 data)
{
    addr_t addr = translate_addr(segment, offset + 3, translate_kind::TRANSLATE_WRITE) - 3;
    wl_phys(addr, data);
}

void cpu_t::decode_opcode()
{
    u8 opcode = fetchb(ip++);
    instruction[opcode_length] = opcode;
    opcode_length++;
    if (opcode_length == 10)
        throw cpu_exception(exception_type::FAULT, ABRT_GPF, 0, true);
    printf("Opcode:%02x\nCS:%04x\nIP:%04x\n", opcode, segs[cs].selector, ip - 1);
    if (opcode_table_1byte_16[opcode])
        (this->*opcode_table_1byte_16[opcode])();
}

u8 cpu_t::decode_modrm_reg16_size16()
{
    u8 modrm = fetchb(ip++);
    mod_reg = (modrm >> 3) & 7;
    u8 mod = (modrm >> 6) & 3;
    mod_addr = 0;
    if(mod == 3) mod_reg_mem = modrm & 7;
    else
    {
        switch(mod)
        {
            case 0:
            {
                if((modrm & 7) == 6)
                {
                    mod_addr = (s16)fetchw(ip);
                    ip += 2;
                    mod_seg = ds;
                    return modrm;
                }
                break;
            }
            case 1: mod_addr = (s8)fetchb(ip++); break;
            case 2: mod_addr = (s16)fetchw(ip); ip += 2; break;
        }
        switch(modrm & 7)
        {
            case 0: mod_addr += BX + SI; mod_seg = ds; break;
            case 1: mod_addr += BX + DI; mod_seg = ds; break;
            case 2: mod_addr += BP + SI; mod_seg = ss; break;
            case 3: mod_addr += BP + DI; mod_seg = ss; break;
            case 4: mod_addr += SI; mod_seg = ds; break;
            case 5: mod_addr += DI; mod_seg = ds; break;
            case 6: mod_addr += BP; mod_seg = ss; break;
            case 7: mod_addr += BX; mod_seg = ds; break;
        }
    }
    return modrm;
}

u8 cpu_t::decode_modrm(int register_size)
{
    if((register_size == REG_16BIT) && !address_size) return decode_modrm_reg16_size16();
    else return 0;
}

void cpu_t::tick()
{
    operand_size = address_size = (segs[cs].flags >> 14) & 1;
    opcode_length = 0;
    seg_prefix = maxseg;
    if (delayed_interrupt_enable)
    {
        eflags.intr = 1;
        delayed_interrupt_enable = false;
    }
    try
    {
        decode_opcode();
    }
    catch (const cpu_exception &e)
    {
        printf("A fault occurred!");
    }
}

void cpu_t::run(s64 cycles)
{
    for (s64 i = 0ll; i < cycles; i++)
    {
        tick();
    }
}