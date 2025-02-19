// license:BSD-3-Clause
// copyright-holders:David Haywood
#ifndef MAME_SHARED_MEGADRIV_H
#define MAME_SHARED_MEGADRIV_H

#pragma once

#include "cpu/m68000/m68000.h"
#include "cpu/z80/z80.h"
#include "sound/sn76496.h"
#include "sound/ymopn.h"
#include "video/315_5313.h"

#define MASTER_CLOCK_NTSC 53693175
#define MASTER_CLOCK_PAL  53203424

#define MD_CPU_REGION_SIZE 0x800000


/*----------- defined in machine/megadriv.cpp -----------*/

INPUT_PORTS_EXTERN( md_common );
INPUT_PORTS_EXTERN( megadriv );


class md_base_state : public driver_device
{
public:
	md_base_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this,"maincpu"),
		m_z80snd(*this,"genesis_snd_z80"),
		m_ymsnd(*this,"ymsnd"),
		m_scan_timer(*this, "md_scan_timer"),
		m_vdp(*this,"gen_vdp"),
		m_megadrive_ram(*this,"megadrive_ram"),
		m_screen(*this,"megadriv"),
		m_io_reset(*this, "RESET"),
		m_io_read_data_port_ptr(*this),
		m_io_write_data_port_ptr(*this)
	{ }

	required_device<m68000_base_device> m_maincpu;
	optional_device<cpu_device> m_z80snd;
	optional_device<ym_generic_device> m_ymsnd;
	optional_device<timer_device> m_scan_timer;
	required_device<sega315_5313_device> m_vdp;
	optional_shared_ptr<uint16_t> m_megadrive_ram;
	optional_device<screen_device> m_screen;

	optional_ioport m_io_reset;
	ioport_port *m_io_pad_3b[4];
	ioport_port *m_io_pad_6b[4];

	struct genesis_z80_vars
	{
		int z80_is_reset = 0;
		int z80_has_bus = 0;
		uint32_t z80_bank_addr = 0;
		std::unique_ptr<uint8_t[]> z80_prgram;
		emu_timer *z80_run_timer = nullptr;
	};

	genesis_z80_vars m_genz80;
	int m_version_hi_nibble;

	void init_megadrie();
	void init_megadriv();
	void init_megadrij();

	uint8_t megadriv_68k_YM2612_read(offs_t offset, uint8_t mem_mask = ~0);
	void megadriv_68k_YM2612_write(offs_t offset, uint8_t data, uint8_t mem_mask = ~0);
	IRQ_CALLBACK_MEMBER(genesis_int_callback);
	void megadriv_init_common();

	void megadriv_z80_bank_w(uint16_t data);
	void megadriv_68k_z80_bank_write(uint16_t data);
	void megadriv_z80_z80_bank_w(uint8_t data);
	uint16_t megadriv_68k_io_read(offs_t offset);
	void megadriv_68k_io_write(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t megadriv_68k_read_z80_ram(offs_t offset, uint16_t mem_mask = ~0);
	void megadriv_68k_write_z80_ram(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint16_t megadriv_68k_check_z80_bus(offs_t offset, uint16_t mem_mask = ~0);
	void megadriv_68k_req_z80_bus(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	void megadriv_68k_req_z80_reset(offs_t offset, uint16_t data, uint16_t mem_mask = ~0);
	uint8_t z80_read_68k_banked_data(offs_t offset);
	void z80_write_68k_banked_data(offs_t offset, uint8_t data);
	void megadriv_z80_vdp_write(offs_t offset, uint8_t data);
	uint8_t megadriv_z80_vdp_read(offs_t offset);
	uint8_t megadriv_z80_unmapped_read();
	TIMER_CALLBACK_MEMBER(megadriv_z80_run_state);

	/* Megadrive / Genesis has 3 I/O ports */
	emu_timer *m_io_timeout[3];
	int m_io_stage[3];
	uint8_t m_io_data_regs[3];
	uint8_t m_io_ctrl_regs[3];
	uint8_t m_io_tx_regs[3];
	read8sm_delegate m_io_read_data_port_ptr;
	write16sm_delegate m_io_write_data_port_ptr;

	WRITE_LINE_MEMBER(vdp_sndirqline_callback_genesis_z80);
	WRITE_LINE_MEMBER(vdp_lv6irqline_callback_genesis_68k);
	WRITE_LINE_MEMBER(vdp_lv4irqline_callback_genesis_68k);

	TIMER_CALLBACK_MEMBER( io_timeout_timer_callback );
	void megadrive_reset_io();
	uint8_t megadrive_io_read_data_port_6button(offs_t offset);
	uint8_t megadrive_io_read_data_port_3button(offs_t offset);
	uint8_t megadrive_io_read_ctrl_port(int portnum);
	uint8_t megadrive_io_read_tx_port(int portnum);
	uint8_t megadrive_io_read_rx_port(int portnum);
	uint8_t megadrive_io_read_sctrl_port(int portnum);

	void megadrive_io_write_data_port_3button(offs_t offset, uint16_t data);
	void megadrive_io_write_data_port_6button(offs_t offset, uint16_t data);
	void megadrive_io_write_ctrl_port(int portnum, uint16_t data);
	void megadrive_io_write_tx_port(int portnum, uint16_t data);
	void megadrive_io_write_rx_port(int portnum, uint16_t data);
	void megadrive_io_write_sctrl_port(int portnum, uint16_t data);

	void megadriv_stop_scanline_timer();

	uint32_t screen_update_megadriv(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	DECLARE_WRITE_LINE_MEMBER(screen_vblank_megadriv);

	void megadriv_tas_callback(offs_t offset, uint8_t data);

	void megadriv_timers(machine_config &config);
	void md_ntsc(machine_config &config);
	void md2_ntsc(machine_config &config);
	void md_pal(machine_config &config);
	void md2_pal(machine_config &config);
	void md_bootleg(machine_config &config);

	void dcat16_megadriv_base(machine_config &config);
	void dcat16_megadriv_map(address_map &map);
	void megadriv_map(address_map &map);
	void megadriv_z80_io_map(address_map &map);
	void megadriv_z80_map(address_map &map);

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override;
	virtual void video_start() override;
};

#endif // MAME_SHARED_MEGADRIV_H
