// license:BSD-3-Clause
// copyright-holders:Ryan Holtz, R. Belmont
/****************************************************************************

    drivers/palm.c
    Palm (MC68328) emulation

    Driver by Ryan Holtz

    Additional bug fixing by R. Belmont

****************************************************************************/

#include "emu.h"
#include "cpu/m68000/m68000.h"
#include "machine/mc68328.h"
#include "machine/ram.h"
#include "sound/dac.h"
#include "emupal.h"
#include "screen.h"
#include "speaker.h"

#include "pilot1k.lh"

namespace {

class palm_state : public driver_device
{
public:
	palm_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_ram(*this, RAM_TAG),
		m_screen(*this, "screen"),
		m_palette(*this, "palette"),
		m_io_penx(*this, "PENX"),
		m_io_peny(*this, "PENY"),
		m_io_penb(*this, "PENB"),
		m_io_portd(*this, "PORTD")
	{ }

	void palmiii(machine_config &config);
	void pilot1k(machine_config &config);
	void palmvx(machine_config &config);
	void palmv(machine_config &config);
	void palm(machine_config &config);
	void palmpro(machine_config &config);
	void pilot5k(machine_config &config);

	DECLARE_INPUT_CHANGED_MEMBER(pen_check);
	DECLARE_INPUT_CHANGED_MEMBER(button_check);

protected:
	virtual void machine_start() override;
	virtual void machine_reset() override;

	enum : uint8_t
	{
		PORTF_Y_VCCN_BIT		= 0,
		PORTF_Y_GND_BIT			= 1,
		PORTF_X_VCCN_BIT		= 2,
		PORTF_X_GND_BIT			= 3,
		PORTF_LCD_EN_BIT		= 4,
		PORTF_LCD_VCCN_BIT		= 5,
		PORTF_LCD_VEE_BIT		= 6,
		PORTF_ADC_CSN_BIT		= 7,

		PORTF_PEN_MASK			= 0x8f,
		PORTF_X_MASK			= (1 << PORTF_X_VCCN_BIT) | (1 << PORTF_Y_GND_BIT),
		PORTF_Y_MASK			= (1 << PORTF_Y_VCCN_BIT) | (1 << PORTF_X_GND_BIT),
	};

private:
	offs_t dasm_override(std::ostream &stream, offs_t pc, const util::disasm_interface::data_buffer &opcodes, const util::disasm_interface::data_buffer &params);
	void mem_map(address_map &map);

	void flm_out(int state);
	void llp_out(int state);
	void lsclk_out(int state);
	void ld_out(uint8_t data);
	void lcd_info_changed(double refresh_hz, int width, int height);
	uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void init_palette(palette_device &palette) const;

	void port_f_out(uint8_t data);
	uint8_t port_c_in();
	uint8_t port_f_in();

	int spi_in();

	required_device<mc68328_device> m_maincpu;
	required_device<ram_device> m_ram;
	required_device<screen_device> m_screen;
	required_device<palette_device> m_palette;
	required_ioport m_io_penx;
	required_ioport m_io_peny;
	required_ioport m_io_penb;
	required_ioport m_io_portd;

	uint8_t m_port_f_latch;

	uint16_t m_spim_data;

	bitmap_rgb32 m_lcd_bitmap;
	int m_lcd_first_line;
	int m_lcd_line_pulse;
	int m_lcd_shift_clk;
	uint8_t m_lcd_data;
	int m_lcd_scan_x;
	int m_lcd_scan_y;

	static const int EXTRA_ARTWORK_HEIGHT = 60;
};


/***************************************************************************
    MACHINE HARDWARE
***************************************************************************/

INPUT_CHANGED_MEMBER(palm_state::pen_check)
{
	uint8_t button = m_io_penb->read();

	if(button)
		m_maincpu->set_penirq_line(1);
	else
		m_maincpu->set_penirq_line(0);
}

INPUT_CHANGED_MEMBER(palm_state::button_check)
{
	uint8_t button_state = m_io_portd->read();
	m_maincpu->set_port_d_lines(button_state, (int)param);
}

void palm_state::port_f_out(uint8_t data)
{
	const uint8_t old = m_port_f_latch;
	m_port_f_latch = data;
	const uint8_t changed = old ^ data;

	if (BIT(changed, PORTF_ADC_CSN_BIT) && !BIT(m_port_f_latch, PORTF_ADC_CSN_BIT))
	{
		switch (m_port_f_latch & PORTF_PEN_MASK)
		{
			case PORTF_X_MASK:
				m_spim_data = (0xff - m_io_penx->read()) * 2;
				break;

			case PORTF_Y_MASK:
				m_spim_data = (0xff - m_io_peny->read()) * 2;
				break;
		}
	}
}

uint8_t palm_state::port_c_in()
{
	return 0x10;
}

uint8_t palm_state::port_f_in()
{
	return m_port_f_latch;
}

int palm_state::spi_in()
{
	int out_state = BIT(m_spim_data, 15);
	m_spim_data <<= 1;
	m_spim_data |= 1;
	return out_state;
}

void palm_state::machine_start()
{
	address_space &space = m_maincpu->space(AS_PROGRAM);
	space.install_ram(0x000000, m_ram->size() - 1, m_ram->pointer());

	save_item(NAME(m_port_f_latch));
	save_item(NAME(m_spim_data));

	save_item(NAME(m_lcd_first_line));
	save_item(NAME(m_lcd_line_pulse));
	save_item(NAME(m_lcd_shift_clk));
	save_item(NAME(m_lcd_data));
	save_item(NAME(m_lcd_scan_x));
	save_item(NAME(m_lcd_scan_y));
}

void palm_state::machine_reset()
{
	// Copy boot ROM
	uint8_t* bios = memregion("bios")->base();
	memset(m_ram->pointer(), 0, m_ram->size());
	memcpy(m_ram->pointer(), bios, 0x20000);

	m_spim_data = 0xffff;

	m_lcd_first_line = 1;
	m_lcd_line_pulse = 0;
	m_lcd_shift_clk = 0;
	m_lcd_data = 0;
	m_lcd_scan_x = 0;
	m_lcd_scan_y = 0;
}

/***************************************************************************
    LCD HARDWARE
***************************************************************************/

void palm_state::init_palette(palette_device &palette) const
{
	palette.set_pen_color(0, 0xbd, 0xbd, 0xaa);
	palette.set_pen_color(1, 0x40, 0x40, 0x40);
}

void palm_state::flm_out(int state)
{
	m_lcd_first_line = state;
}

void palm_state::llp_out(int state)
{
	const int old = m_lcd_line_pulse;
	m_lcd_line_pulse = state;
	if (!state && old)
	{
		m_lcd_scan_x = 0;
		if (m_lcd_first_line)
		{
			m_lcd_scan_y = 0;
		}
		else
		{
			m_lcd_scan_y++;
		}
	}
}

void palm_state::lsclk_out(int state)
{
	const int old = m_lcd_shift_clk;
	m_lcd_shift_clk = state;
	if (state && !old)
	{
		for (uint8_t i = 0; i < 4; i++)
		{
			m_lcd_bitmap.pix(m_lcd_scan_y, m_lcd_scan_x) = m_palette->pen_color(BIT(m_lcd_data, 3 - i));
			m_lcd_scan_x++;
		}
	}
}

void palm_state::ld_out(uint8_t data)
{
	m_lcd_data = data;
}

void palm_state::lcd_info_changed(double refresh_hz, int width, int height)
{
	m_screen->set_refresh_hz(refresh_hz);
	m_screen->set_size(width, height + EXTRA_ARTWORK_HEIGHT);
	m_screen->set_visarea(0, width - 1, 0, (height + EXTRA_ARTWORK_HEIGHT) - 1);
	m_lcd_bitmap.resize(width, height);
}

uint32_t palm_state::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	bitmap.fill(m_palette->pen_color(0));
	if (m_lcd_bitmap.valid())
	{
		uint32_t *src = &m_lcd_bitmap.pix(0);
		uint32_t *dst = &bitmap.pix(0);
		std::copy_n(src, m_lcd_bitmap.width() * m_lcd_bitmap.height(), dst);
	}
	return 0;
}


/***************************************************************************
    ADDRESS MAPS
***************************************************************************/

void palm_state::mem_map(address_map &map)
{
	map(0xc00000, 0xe07fff).rom().region("bios", 0);
}


/***************************************************************************
    MACHINE DRIVERS
***************************************************************************/

void palm_state::palm(machine_config &config)
{
	/* basic machine hardware */
	MC68328(config, m_maincpu, 32768*506);        /* 16.580608 MHz */
	m_maincpu->set_addrmap(AS_PROGRAM, &palm_state::mem_map);
	m_maincpu->set_dasm_override(FUNC(palm_state::dasm_override));
	m_maincpu->out_port_f().set(FUNC(palm_state::port_f_out));
	m_maincpu->in_port_c().set(FUNC(palm_state::port_c_in));
	m_maincpu->in_port_f().set(FUNC(palm_state::port_f_in));
	m_maincpu->out_pwm().set("dac", FUNC(dac_bit_interface::write));
	m_maincpu->in_spim().set(FUNC(palm_state::spi_in));
	m_maincpu->out_flm().set(FUNC(palm_state::flm_out));
	m_maincpu->out_llp().set(FUNC(palm_state::llp_out));
	m_maincpu->out_lsclk().set(FUNC(palm_state::lsclk_out));
	m_maincpu->out_ld().set(FUNC(palm_state::ld_out));
	m_maincpu->set_lcd_info_changed(FUNC(palm_state::lcd_info_changed));

	config.set_maximum_quantum(attotime::from_hz(60));

	/* video hardware */
	SCREEN(config, m_screen, SCREEN_TYPE_LCD);
	m_screen->set_refresh_hz(60);
	m_screen->set_vblank_time(ATTOSECONDS_IN_USEC(0));
	m_screen->set_video_attributes(VIDEO_UPDATE_BEFORE_VBLANK);
	m_screen->set_size(160, 160);
	m_screen->set_visarea(0, 159, 0, 159);
	m_screen->set_screen_update(FUNC(palm_state::screen_update));

	PALETTE(config, m_palette, FUNC(palm_state::init_palette), 2);

	/* audio hardware */
	SPEAKER(config, "speaker").front_center();
	DAC_1BIT(config, "dac", 0).add_route(ALL_OUTPUTS, "speaker", 0.25);
}

static INPUT_PORTS_START( palm )
	PORT_START( "PENX" )
	PORT_BIT( 0xff, 0x50, IPT_LIGHTGUN_X ) PORT_NAME("Pen X") PORT_MINMAX(0, 0xa0) PORT_SENSITIVITY(50) PORT_CROSSHAIR(X, 1.0, 0.0, 0)

	PORT_START( "PENY" )
	PORT_BIT( 0xff, 0x50, IPT_LIGHTGUN_Y ) PORT_NAME("Pen Y") PORT_MINMAX(0, 0xa0) PORT_SENSITIVITY(50) PORT_CROSSHAIR(Y, 1.0, 0.0, 0)

	PORT_START( "PENB" )
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON1 ) PORT_NAME("Pen Button") PORT_CODE(MOUSECODE_BUTTON1) PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, pen_check, 0)

	PORT_START( "PORTD" )
	PORT_BIT( 0x01, IP_ACTIVE_HIGH, IPT_BUTTON2 ) PORT_NAME("Power") PORT_CODE(KEYCODE_D)   PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, button_check, 0)
	PORT_BIT( 0x02, IP_ACTIVE_HIGH, IPT_BUTTON3 ) PORT_NAME("Up") PORT_CODE(KEYCODE_Y)    PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, button_check, 1)
	PORT_BIT( 0x04, IP_ACTIVE_HIGH, IPT_BUTTON4 ) PORT_NAME("Down") PORT_CODE(KEYCODE_H)    PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, button_check, 2)
	PORT_BIT( 0x08, IP_ACTIVE_HIGH, IPT_BUTTON5 ) PORT_NAME("Button 1") PORT_CODE(KEYCODE_F)   PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, button_check, 3)
	PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON6 ) PORT_NAME("Button 2") PORT_CODE(KEYCODE_G)   PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, button_check, 4)
	PORT_BIT( 0x20, IP_ACTIVE_HIGH, IPT_BUTTON7 ) PORT_NAME("Button 3") PORT_CODE(KEYCODE_J)   PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, button_check, 5)
	PORT_BIT( 0x40, IP_ACTIVE_HIGH, IPT_BUTTON8 ) PORT_NAME("Button 4") PORT_CODE(KEYCODE_K)   PORT_CHANGED_MEMBER(DEVICE_SELF, palm_state, button_check, 6)
	PORT_BIT( 0x80, IP_ACTIVE_HIGH, IPT_UNUSED )
INPUT_PORTS_END


/***************************************************************************

  Game driver(s)

***************************************************************************/

#define PALM_68328_BIOS \
	ROM_REGION16_BE( 0x208000, "bios", 0 )  \
	ROM_SYSTEM_BIOS( 0, "1.0e", "Palm OS 1.0 (English)" )   \
	ROMX_LOAD( "palmos10-en.rom", 0x000000, 0x080000, CRC(82030062) SHA1(00d85c6a0588133cc4651555e9605a61fc1901fc), ROM_GROUPWORD | ROM_BIOS(0) ) \
	ROM_SYSTEM_BIOS( 1, "2.0eper", "Palm OS 2.0 Personal (English)" ) \
	ROMX_LOAD( "palmos20-en-pers.rom", 0x000000, 0x100000, CRC(40ea8baa) SHA1(8e26e213de42da1317c375fb1f394bb945b9d178), ROM_GROUPWORD | ROM_BIOS(1) ) \
	ROM_SYSTEM_BIOS( 2, "2.0epro", "Palm OS 2.0 Professional (English)" ) \
	ROMX_LOAD( "palmos20-en-pro.rom", 0x000000, 0x100000, CRC(baa5b36a) SHA1(535bd9548365d300f85f514f318460443a021476), ROM_GROUPWORD | ROM_BIOS(2) ) \
	ROM_SYSTEM_BIOS( 3, "2.0eprod", "Palm OS 2.0 Professional (English, Debug)" ) \
	ROMX_LOAD( "palmis20-en-pro-dbg.rom", 0x000000, 0x100000, CRC(0d1d3a3b) SHA1(f18a80baa306d4d46b490589ee9a2a5091f6081c), ROM_GROUPWORD | ROM_BIOS(3) ) \
	ROM_SYSTEM_BIOS( 4, "3.0e", "Palm OS 3.0 (English)" ) \
	ROMX_LOAD( "palmos30-en.rom", 0x008000, 0x200000, CRC(6f461f3d) SHA1(7fbf592b4dc8c222be510f6cfda21d48ebe22413), ROM_GROUPWORD | ROM_BIOS(4) ) \
	ROM_RELOAD(0x000000, 0x004000)  \
	ROM_SYSTEM_BIOS( 5, "3.0ed", "Palm OS 3.0 (English, Debug)" ) \
	ROMX_LOAD( "palmos30-en-dbg.rom", 0x008000, 0x200000, CRC(4deda226) SHA1(1c67d6fee2b6a4acd51cda6ef3490305730357ad), ROM_GROUPWORD | ROM_BIOS(5) ) \
	ROM_RELOAD(0x000000, 0x004000)  \
	ROM_SYSTEM_BIOS( 6, "3.0g", "Palm OS 3.0 (German)" ) \
	ROMX_LOAD( "palmos30-de.rom", 0x008000, 0x200000, CRC(b991d6c3) SHA1(73e7539517b0d931e9fa99d6f6914ad46fb857b4), ROM_GROUPWORD | ROM_BIOS(6) ) \
	ROM_RELOAD(0x000000, 0x004000)  \
	ROM_SYSTEM_BIOS( 7, "3.0f", "Palm OS 3.0 (French)" ) \
	ROMX_LOAD( "palmos30-fr.rom", 0x008000, 0x200000, CRC(a2a9ff6c) SHA1(7cb119f896017e76e4680510bee96207d9d28e44), ROM_GROUPWORD | ROM_BIOS(7) ) \
	ROM_RELOAD(0x000000, 0x004000)  \
	ROM_SYSTEM_BIOS( 8, "3.0s", "Palm OS 3.0 (Spanish)" ) \
	ROMX_LOAD( "palmos30-sp.rom", 0x008000, 0x200000, CRC(63a595be) SHA1(f6e03a2fedf0cbe6228613f50f8e8717e797877d), ROM_GROUPWORD | ROM_BIOS(8) ) \
	ROM_RELOAD(0x000000, 0x004000)  \
	ROM_SYSTEM_BIOS( 9, "3.3e", "Palm OS 3.3 (English)" ) \
	ROMX_LOAD( "palmos33-en-iii.rom", 0x008000, 0x200000, CRC(1eae0253) SHA1(e4626f1d33eca8368284d906b2152dcd28b71bbd), ROM_GROUPWORD | ROM_BIOS(9) ) \
	ROM_RELOAD(0x000000, 0x004000)  \
	ROM_SYSTEM_BIOS( 10, "3.3f", "Palm OS 3.3 (French)" ) \
	ROMX_LOAD( "palmos33-fr-iii.rom", 0x008000, 0x200000, CRC(d7894f5f) SHA1(c7c90df814d4f97958194e0bc28c595e967a4529), ROM_GROUPWORD | ROM_BIOS(10) ) \
	ROM_RELOAD(0x000000, 0x004000)  \
	ROM_SYSTEM_BIOS( 11, "3.3g", "Palm OS 3.3 (German)" ) \
	ROMX_LOAD( "palmos33-de-iii.rom", 0x008000, 0x200000, CRC(a5a99c45) SHA1(209b0154942dab80b56d5e6e68fa20b9eb75f5fe), ROM_GROUPWORD | ROM_BIOS(11) ) \
	ROM_RELOAD(0x000000, 0x004000)

ROM_START( pilot1k )
	PALM_68328_BIOS
	ROM_DEFAULT_BIOS( "1.0e" )
ROM_END

ROM_START( pilot5k )
	PALM_68328_BIOS
	ROM_DEFAULT_BIOS( "1.0e" )
ROM_END

ROM_START( palmpers )
	PALM_68328_BIOS
	ROM_DEFAULT_BIOS( "2.0eper" )
ROM_END

ROM_START( palmpro )
	PALM_68328_BIOS
	ROM_DEFAULT_BIOS( "2.0epro" )
ROM_END

ROM_START( palmiii )
	PALM_68328_BIOS
	ROM_DEFAULT_BIOS( "3.0e" )
ROM_END

ROM_START( palmv )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "3.1e", "Palm OS 3.1 (English)" )
	ROMX_LOAD( "palmv31-en.rom", 0x008000, 0x200000, CRC(4656b2ae) SHA1(ec66a93441fbccfd8e0c946baa5d79c478c83e85), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 1, "3.1g", "Palm OS 3.1 (German)" )
	ROMX_LOAD( "palmv31-de.rom", 0x008000, 0x200000, CRC(a9631dcf) SHA1(63b44d4d3fc2f2196c96d3b9b95da526df0fac77), ROM_GROUPWORD | ROM_BIOS(1) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 2, "3.1f", "Palm OS 3.1 (French)" )
	ROMX_LOAD( "palmv31-fr.rom", 0x008000, 0x200000, CRC(0d933a1c) SHA1(d0454f1159705d0886f8a68e1b8a5e96d2ca48f6), ROM_GROUPWORD | ROM_BIOS(2) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 3, "3.1s", "Palm OS 3.1 (Spanish)" )
	ROMX_LOAD( "palmv31-sp.rom", 0x008000, 0x200000, CRC(cc46ca1f) SHA1(93bc78ca84d34916d7e122b745adec1068230fcd), ROM_GROUPWORD | ROM_BIOS(3) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 4, "3.1j", "Palm OS 3.1 (Japanese)" )
	ROMX_LOAD( "palmv31-jp.rom", 0x008000, 0x200000, CRC(c786db12) SHA1(4975ff2af76892370c5d4d7d6fa87a84480e79d6), ROM_GROUPWORD | ROM_BIOS(4) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 5, "3.1e2", "Palm OS 3.1 (English) v2" )
	ROMX_LOAD( "palmv31-en-2.rom", 0x008000, 0x200000, CRC(caced2bd) SHA1(95970080601f72a77a4c338203ed8809fab17abf), ROM_GROUPWORD | ROM_BIOS(5) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_DEFAULT_BIOS( "3.1e2" )
ROM_END

ROM_START( palmvx )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "3.3e", "Palm OS 3.3 (English)" )
	ROMX_LOAD( "palmvx33-en.rom", 0x000000, 0x200000, CRC(3fc0cc6d) SHA1(6873d5fa99ac372f9587c769940c9b3ac1745a0a), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_SYSTEM_BIOS( 1, "4.0e", "Palm OS 4.0 (English)" )
	ROMX_LOAD( "palmvx40-en.rom", 0x000000, 0x200000, CRC(488e4638) SHA1(10a10fc8617743ebd5df19c1e99ca040ac1da4f5), ROM_GROUPWORD | ROM_BIOS(1) )
	ROM_SYSTEM_BIOS( 2, "4.1e", "Palm OS 4.1 (English)" )
	ROMX_LOAD( "palmvx41-en.rom", 0x000000, 0x200000, CRC(e59f4dff) SHA1(5e3000db318eeb8cd1f4d9729d0c9ebca560fa4a), ROM_GROUPWORD | ROM_BIOS(2) )
	ROM_DEFAULT_BIOS( "4.1e" )
ROM_END

ROM_START( palmiiic )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "3.5eb", "Palm OS 3.5 (English) beta" )
	ROMX_LOAD( "palmiiic350-en-beta.rom", 0x008000, 0x200000, CRC(d58521a4) SHA1(508742ea1e078737666abd4283cf5e6985401c9e), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 1, "3.5c", "Palm OS 3.5 (Chinese)" )
	ROMX_LOAD( "palmiiic350-ch.rom", 0x008000, 0x200000, CRC(a9779f3a) SHA1(1541102cd5234665233072afe8f0e052134a5334), ROM_GROUPWORD | ROM_BIOS(1) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 2, "4.0e", "Palm OS 4.0 (English)" )
	ROMX_LOAD( "palmiiic40-en.rom", 0x008000, 0x200000, CRC(6b2a5ad2) SHA1(54321dcaedcc80de57a819cfd599d8d1b2e26eeb), ROM_GROUPWORD | ROM_BIOS(2) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_DEFAULT_BIOS( "4.0e" )
ROM_END

ROM_START( palmm100 )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "3.51e", "Palm OS 3.5.1 (English)" )
	ROMX_LOAD( "palmm100-351-en.rom", 0x008000, 0x200000, CRC(ae8dda60) SHA1(c46248d6f05cb2f4337985610cedfbdc12ac47cf), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_DEFAULT_BIOS( "3.51e" )
ROM_END

ROM_START( palmm130 )
	ROM_REGION16_BE( 0x408000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "4.0e", "Palm OS 4.0 (English)" )
	ROMX_LOAD( "palmm130-40-en.rom", 0x008000, 0x400000, CRC(58046b7e) SHA1(986057010d62d5881fba4dede2aba0d4d5008b16), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_DEFAULT_BIOS( "4.0e" )
ROM_END

ROM_START( palmm505 )
	ROM_REGION16_BE( 0x408000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "4.0e", "Palm OS 4.0 (English)" )
	ROMX_LOAD( "palmos40-en-m505.rom", 0x008000, 0x400000, CRC(822a4679) SHA1(a4f5e9f7edb1926647ea07969200c5c5e1521bdf), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 1, "4.1e", "Palm OS 4.1 (English)" )
	ROMX_LOAD( "palmos41-en-m505.rom", 0x008000, 0x400000, CRC(d248202a) SHA1(65e1bd08b244c589b4cd10fe573e0376aba90e5f), ROM_GROUPWORD | ROM_BIOS(1) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_DEFAULT_BIOS( "4.1e" )
ROM_END

ROM_START( palmm515 )
	ROM_REGION16_BE( 0x408000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "4.1e", "Palm OS 4.1 (English)" )
	ROMX_LOAD( "palmos41-en-m515.rom", 0x008000, 0x400000, CRC(6e143436) SHA1(a0767ea26cc493a3f687525d173903fef89f1acb), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_DEFAULT_BIOS( "4.1e" )
ROM_END

ROM_START( visor )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "3.52e", "Palm OS 3.5.2 (English)" )
	ROMX_LOAD( "visor-352-en.rom", 0x008000, 0x200000, CRC(c9e55271) SHA1(749e9142f4480114c5e0d7f21ea354df7273ac5b), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_DEFAULT_BIOS( "3.52e" )
ROM_END

ROM_START( spt1500 )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "4.1pim", "Version 4.1 (pim)" )
	ROMX_LOAD( "spt1500v41-pim.rom",      0x008000, 0x200000, CRC(29e50eaf) SHA1(3e920887bdf74f8f83935977b02f22d5217723eb), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 1, "4.1pimn", "Version 4.1 (pimnoft)" )
	ROMX_LOAD( "spt1500v41-pimnoft.rom",  0x008000, 0x200000, CRC(4b44f284) SHA1(4412e946444706628b94d2303b02580817e1d370), ROM_GROUPWORD | ROM_BIOS(1) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 2, "4.1nopimn", "Version 4.1 (nopimnoft)" )
	ROMX_LOAD( "spt1500v41-nopimnoft.rom",0x008000, 0x200000, CRC(4ba19190) SHA1(d713c1390b82eb4e5fbb39aa10433757c5c49e02), ROM_GROUPWORD | ROM_BIOS(2) )
	ROM_RELOAD(0x000000, 0x004000)
ROM_END

ROM_START( spt1700 )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "1.03pim", "Version 1.03 (pim)" )
	ROMX_LOAD( "spt1700v103-pim.rom",    0x008000, 0x200000, CRC(9df4ee50) SHA1(243a19796f15219cbd73e116f7dfb236b3d238cd), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
ROM_END

ROM_START( spt1740 )
	ROM_REGION16_BE( 0x208000, "bios", 0 )
	ROM_SYSTEM_BIOS( 0, "1.03pim", "Version 1.03 (pim)" )
	ROMX_LOAD( "spt1740v103-pim.rom",    0x008000, 0x200000, CRC(c29f341c) SHA1(b56d7f8a0c15b1105972e24ed52c846b5e27b195), ROM_GROUPWORD | ROM_BIOS(0) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 1, "1.03pimn", "Version 1.03 (pimnoft)" )
	ROMX_LOAD( "spt1740v103-pimnoft.rom", 0x008000, 0x200000, CRC(b2d49d5c) SHA1(c133dc021b6797cdb93b666c5b315b00b5bb0917), ROM_GROUPWORD | ROM_BIOS(1) )
	ROM_RELOAD(0x000000, 0x004000)
	ROM_SYSTEM_BIOS( 2, "1.03nopim", "Version 1.03 (nopim)" )
	ROMX_LOAD( "spt1740v103-nopim.rom",   0x008000, 0x200000, CRC(8ea7e652) SHA1(2a4b5d6a426e627b3cb82c47109cfe2497eba29a), ROM_GROUPWORD | ROM_BIOS(2) )
	ROM_RELOAD(0x000000, 0x004000)
ROM_END

void palm_state::pilot1k(machine_config &config)
{
	palm(config);

	/* internal ram */
	RAM(config, RAM_TAG).set_default_size("128K").set_extra_options("512K,1M,2M,4M,8M");

	config.set_default_layout(layout_pilot1k);
}

void palm_state::pilot5k(machine_config &config)
{
	palm(config);

	/* internal ram */
	RAM(config, RAM_TAG).set_default_size("512K").set_extra_options("1M,2M,4M,8M");
}

void palm_state::palmpro(machine_config &config)
{
	palm(config);

	/* internal ram */
	RAM(config, RAM_TAG).set_default_size("1M").set_extra_options("2M,4M,8M");
}

void palm_state::palmiii(machine_config &config)
{
	palm(config);

	/* internal ram */
	RAM(config, RAM_TAG).set_default_size("2M").set_extra_options("4M,8M");
}

void palm_state::palmv(machine_config &config)
{
	palm(config);

	/* internal ram */
	RAM(config, RAM_TAG).set_default_size("2M").set_extra_options("4M,8M");
}

void palm_state::palmvx(machine_config &config)
{
	palm(config);

	/* internal ram */
	RAM(config, RAM_TAG).set_default_size("8M");
}

} // anonymous namespace

//    YEAR  NAME      PARENT   COMPAT  MACHINE  INPUT CLASS       INIT        COMPANY          FULLNAME               FLAGS
COMP( 1996, pilot1k,  0,       0,      pilot1k, palm, palm_state, empty_init, "U.S. Robotics", "Pilot 1000",          MACHINE_SUPPORTS_SAVE | MACHINE_NO_SOUND )
COMP( 1996, pilot5k,  pilot1k, 0,      pilot5k, palm, palm_state, empty_init, "U.S. Robotics", "Pilot 5000",          MACHINE_SUPPORTS_SAVE | MACHINE_NO_SOUND )
COMP( 1997, palmpers, pilot1k, 0,      pilot5k, palm, palm_state, empty_init, "U.S. Robotics", "Palm Pilot Personal", MACHINE_SUPPORTS_SAVE | MACHINE_NO_SOUND )
COMP( 1997, palmpro,  pilot1k, 0,      palmpro, palm, palm_state, empty_init, "U.S. Robotics", "Palm Pilot Pro",      MACHINE_SUPPORTS_SAVE | MACHINE_NO_SOUND )
COMP( 1998, palmiii,  pilot1k, 0,      palmiii, palm, palm_state, empty_init, "3Com",          "Palm III",            MACHINE_SUPPORTS_SAVE | MACHINE_NO_SOUND )
COMP( 1998, palmiiic, pilot1k, 0,      palmiii, palm, palm_state, empty_init, "Palm Inc",      "Palm IIIc",           MACHINE_NOT_WORKING )
COMP( 2000, palmm100, pilot1k, 0,      palmiii, palm, palm_state, empty_init, "Palm Inc",      "Palm m100",           MACHINE_NOT_WORKING )
COMP( 2000, palmm130, pilot1k, 0,      palmiii, palm, palm_state, empty_init, "Palm Inc",      "Palm m130",           MACHINE_NOT_WORKING )
COMP( 2001, palmm505, pilot1k, 0,      palmiii, palm, palm_state, empty_init, "Palm Inc",      "Palm m505",           MACHINE_NOT_WORKING )
COMP( 2001, palmm515, pilot1k, 0,      palmiii, palm, palm_state, empty_init, "Palm Inc",      "Palm m515",           MACHINE_NOT_WORKING )
COMP( 1999, palmv,    pilot1k, 0,      palmv,   palm, palm_state, empty_init, "3Com",          "Palm V",              MACHINE_NOT_WORKING )
COMP( 1999, palmvx,   pilot1k, 0,      palmvx,  palm, palm_state, empty_init, "Palm Inc",      "Palm Vx",             MACHINE_NOT_WORKING )
COMP( 2001, visor,    pilot1k, 0,      palmvx,  palm, palm_state, empty_init, "Handspring",    "Visor Edge",          MACHINE_NOT_WORKING )
COMP( 19??, spt1500,  pilot1k, 0,      palmvx,  palm, palm_state, empty_init, "Symbol",        "SPT 1500",            MACHINE_NOT_WORKING )
COMP( 19??, spt1700,  pilot1k, 0,      palmvx,  palm, palm_state, empty_init, "Symbol",        "SPT 1700",            MACHINE_NOT_WORKING )
COMP( 19??, spt1740,  pilot1k, 0,      palmvx,  palm, palm_state, empty_init, "Symbol",        "SPT 1740",            MACHINE_NOT_WORKING )

#include "palm_dbg.ipp"
