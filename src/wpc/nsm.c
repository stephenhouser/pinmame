/************************************************************************************************
 NSM, Germany (aka Loewen)
 -------------------------
 NSM (along with Bell Games, Italy) developed an entirely new kind of pinball hardware,
 based almost entirely on the HC4094 serial decoder chip, in 1985.
 The trick was to use a total of NINE of these chips in order to handle (in sequence)
 the switch / lamp / display strobe, then the lamps, solenoids, and display segments.
 This method of using serial data for the displays was later copied by Juegos Populares.

	Hardware:
	---------
		CPU:     TMS9995 @ 11.052 MHz
		IO:      CPU ports, 9x HC4094 decoder chips
		DISPLAY: 5 x 8 Digit, 7-Segment panels
		SOUND:	 2 x AY8912 (same as AY8910 but one I/O port only)
************************************************************************************************/
#include "driver.h"
#include "cpu/tms9900/tms9900.h"
#include "machine/4094.h"
#include "core.h"
#include "sim.h"

static struct {
  core_tSeg segments;
  UINT8 strobe;
  UINT32 solenoids;
  int zc, sndflag;
} locals;

static INTERRUPT_GEN(vblank) {
  /*-------------------------------
  /  copy local data to interface
  /--------------------------------*/
  memcpy(coreGlobals.lampMatrix, coreGlobals.tmpLampMatrix, sizeof(coreGlobals.tmpLampMatrix));
  memcpy(coreGlobals.segments, locals.segments, sizeof(coreGlobals.segments));
  coreGlobals.solenoids = locals.solenoids;
  core_updateSw(TRUE);
}

static void nsm_zc(int data) {
  locals.zc = !locals.zc;
}

static WRITE_HANDLER(oe_w) {
  int i;
  for (i=0; i < 9; i++) {
    HC4094_oe_w(i, 1);
    HC4094_oe_w(i, 0);
  }
}

static WRITE_HANDLER(cru_w) {
  int i;
  HC4094_data_w(0, GET_BIT0);
  for (i=0; i < 9; i++) {
    HC4094_strobe_w(i, 1);
    HC4094_clock_w(i, 1);
    HC4094_clock_w(i, 0);
    HC4094_strobe_w(i, 0);
  }
}

static WRITE_HANDLER(parallel_0_out) {
  locals.strobe = core_BitColToNum(data);
}
static WRITE_HANDLER(parallel_1_out) {
  coreGlobals.tmpLampMatrix[locals.strobe] = ~data;
}
static WRITE_HANDLER(parallel_2_out) {
  locals.solenoids = (locals.solenoids & 0xff00) | data;
}
static WRITE_HANDLER(parallel_3_out) {
  locals.solenoids = (locals.solenoids & 0x00ff) | (data << 8);
}
static WRITE_HANDLER(parallel_4_out) {
  locals.segments[locals.strobe].w = ~data;
}
static WRITE_HANDLER(parallel_5_out) {
  locals.segments[8+locals.strobe].w = ~data;
}
static WRITE_HANDLER(parallel_6_out) {
  locals.segments[16+locals.strobe].w = ~data;
}
static WRITE_HANDLER(parallel_7_out) {
  locals.segments[24+locals.strobe].w = ~data;
}
static WRITE_HANDLER(parallel_8_out) {
  locals.segments[32+locals.strobe].w = ~data;
}
static WRITE_HANDLER(qspin_0_out) { HC4094_data_w(1, data); }
static WRITE_HANDLER(qspin_1_out) { HC4094_data_w(2, data); }
static WRITE_HANDLER(qspin_2_out) { HC4094_data_w(3, data); }
static WRITE_HANDLER(qspin_3_out) { HC4094_data_w(4, data); }
static WRITE_HANDLER(qspin_4_out) { HC4094_data_w(5, data); }
static WRITE_HANDLER(qspin_5_out) { HC4094_data_w(6, data); }
static WRITE_HANDLER(qspin_6_out) { HC4094_data_w(7, data); }
static WRITE_HANDLER(qspin_7_out) { HC4094_data_w(8, data); }
/*
Flow of the serial data line:

Page HC# Clk Load What?
1    0   out out  Strobe
1    1   out out  Lamps
4    2   out out  Solenoids
4    3   out out  Solenoids
3    4   out out  Display
3    5   out out  Display
3    6   out out  Display
3    7   out out  Display
3    8   out out  Display
2    -   in  in   Switches
7    -   in  in   Switches
7    -   in  in   Switches
*/
static HC4094interface hc4094nsm = {
  9, // 9 chips!
  { parallel_0_out, parallel_1_out, parallel_2_out, parallel_3_out, parallel_4_out, parallel_5_out, parallel_6_out, parallel_7_out, parallel_8_out },
  { qspin_0_out, qspin_1_out, qspin_2_out, qspin_3_out, qspin_4_out, qspin_5_out, qspin_6_out, qspin_7_out }
};

static WRITE_HANDLER(ay8912_0_ctrl_w)   { AY8910Write(0,0,data); }
static WRITE_HANDLER(ay8912_0_data_w)   { AY8910Write(0,1,data); }
static WRITE_HANDLER(ay8912_0_port_w)	{ logerror("AY8912 #0: port write=%02x\n", data); }
static WRITE_HANDLER(ay8912_1_ctrl_w)   { AY8910Write(1,0,data); }
static WRITE_HANDLER(ay8912_1_data_w)   { AY8910Write(1,1,data); }
static WRITE_HANDLER(ay8912_1_port_w)	{ logerror("AY8912 #1: port write=%02x\n", data); locals.sndflag = data >> 3; }
static struct AY8910interface nsm_ay8912Int = {
  2,			/* 2 chips */
  11052000 / 8,		/* 1.3815 MHz */
  { MIXER(30,MIXER_PAN_LEFT), MIXER(30,MIXER_PAN_RIGHT) },	/* Volume */
  { 0 }, { 0 },
  { ay8912_0_port_w },
  { ay8912_1_port_w },
};

static READ_HANDLER(read_snd) {
  return locals.sndflag;
}
static READ_HANDLER(read_0) {
  return 0;
}
static READ_HANDLER(read_1) {
  return 1;
}
static READ_HANDLER(read_zc) {
  return locals.zc;
}

static READ_HANDLER(cru_r) {
  return 0xffff ^ (coreGlobals.swMatrix[1 + (offset & 7)] | (coreGlobals.swMatrix[9 + (offset & 7)] << 8));
}

static MEMORY_READ_START(readmem)
  { 0x0000, 0x7fff, MRA_ROM },
  { 0xe000, 0xefff, MRA_RAM },
MEMORY_END

static MEMORY_WRITE_START(writemem)
  { 0xe000, 0xe7ff, MWA_RAM, &generic_nvram, &generic_nvram_size},
  { 0xe800, 0xefff, MWA_RAM },
  { 0xffec, 0xffec, ay8912_0_ctrl_w },
  { 0xffed, 0xffed, ay8912_0_data_w },
  { 0xffee, 0xffee, ay8912_1_ctrl_w },
  { 0xffef, 0xffef, ay8912_1_data_w },
MEMORY_END

static PORT_WRITE_START( writeport )
  { 0x0f70, 0x0f7f, MWA_NOP },  // internal registers
  { 0x7fb0, 0x7fbf, cru_w },
  { 0x7fd0, 0x7fd0, oe_w },
PORT_END

static PORT_READ_START( readport )
  { 0x00,  0x00,  read_1 },		// undervolt perc.
  { 0x10,  0x10,  read_zc },	// antenna
  { 0x30,  0x30,  read_1 },		// J702 pin 7 (unknown service, default hi)
  { 0x40,  0x40,  read_1 },		// J702 pin 11 (unknown service, default hi)
  { 0x50,  0x50,  read_1 },		// batt. test
  { 0x60,  0x60,  read_snd },	// J703 pin 13 (sound chip #2 analog output, default hi)
  { 0x1f0, 0x1f0, MRA_NOP },	// ???
  { 0xfe4, 0xfe4, MRA_NOP },	// ???
  { 0xff0, 0xfff, cru_r },
PORT_END

static MACHINE_INIT(nsm) {
  HC4094_init(&hc4094nsm);
}

static MACHINE_RESET(nsm) {
  memset(&locals, 0, sizeof(locals));
  // Disable auto wait state generation on reset
  tms9995reset_param param = { 0 };
  cpunum_reset(0, &param, NULL);
}

static core_tLCDLayout dispNsm[] = {
  {0, 0, 0,8,CORE_SEG8D}, {0,18, 8,8,CORE_SEG8D},
  {3, 0,16,8,CORE_SEG8D}, {3,18,24,8,CORE_SEG8D},
#ifdef MAME_DEBUG
  {6, 9,32,8,CORE_SEG8D},
#else
  {6,11,33,2,CORE_SEG8D}, {6,19,37,2,CORE_SEG8D},
#endif
  {0}
};
static core_tGameData nsmGameData = {0,dispNsm};
static void init_nsm(void) {
  core_gameData = &nsmGameData;
}

static SWITCH_UPDATE(nsm) {
  if (inports) {
    CORE_SETKEYSW(inports[CORE_COREINPORT], 0x01, 1);
  }
  cpu_set_irq_line(0, 0, keyboard_pressed_memory_repeat(KEYCODE_M, 1) ? ASSERT_LINE : CLEAR_LINE);
  cpu_set_irq_line(0, 1, keyboard_pressed_memory_repeat(KEYCODE_COMMA, 1) ? CLEAR_LINE : ASSERT_LINE);
}

MACHINE_DRIVER_START(nsm)
  MDRV_IMPORT_FROM(PinMAME)
  MDRV_CORE_INIT_RESET_STOP(nsm,nsm,NULL)
  MDRV_CPU_ADD_TAG("mcpu", TMS9995, 11052000)
  MDRV_CPU_MEMORY(readmem, writemem)
  MDRV_CPU_PORTS(readport, writeport)
  MDRV_CPU_VBLANK_INT(vblank, 1)
  MDRV_DIAGNOSTIC_LEDH(1)
  MDRV_SWITCH_UPDATE(nsm)
  MDRV_NVRAM_HANDLER(generic_0fill)
  MDRV_TIMER_ADD(nsm_zc, 100)

  MDRV_SOUND_ADD(AY8910, nsm_ay8912Int)
  MDRV_SOUND_ATTRIBUTES(SOUND_SUPPORTS_STEREO)
MACHINE_DRIVER_END


// Cosmic Flash (10/85)

// Hot Fire Birds (12/85)

INPUT_PORTS_START(firebird)
  CORE_PORTS
  SIM_PORTS(1)
  PORT_START /* 0 */
    COREPORT_BIT(0x0001, "Key 1", KEYCODE_1)
INPUT_PORTS_END

ROM_START(firebird)
  NORMALREGION(0x10000, REGION_CPU1)
    ROM_LOAD("nsmf02.764", 0x0000, 0x2000, CRC(236b5780) SHA1(19ef6e1fc900e5d94f615a4316f0383ed5ee939c))
    ROM_LOAD("nsmf03.764", 0x2000, 0x2000, CRC(d88c6ef5) SHA1(00edeefaab7e1141741aa132e6f7e56a911573be))
    ROM_LOAD("nsmf04.764", 0x4000, 0x2000, CRC(38a8add4) SHA1(74f781edc31aad07411feacad53c5f6cc73d09f4))
ROM_END
#define init_firebird init_nsm
CORE_GAMEDEFNV(firebird,"Hot Fire Birds",1985,"NSM (Germany)",nsm,GAME_NOT_WORKING)


















