#ifndef HD44780_H
#define HD44780_H

struct hd44780_gpio {
	int data[8];
	int rw;
	int rs;
	int en;
};

struct hd44780_format {
	int width;
	int height;
};

enum hd44780_mode {
	HD44780_MODE_8BIT,
	HD44780_MODE_WRITEONLY
};

enum hd44780_font {
	HD44780_FONT_5X7,
	HD44780_FONT_5X10
};

struct hd44780_platform_data {
	struct hd44780_gpio gpio;
	struct hd44780_format format;
	unsigned char *init_text;
	enum hd44780_mode mode;
	enum hd44780_font font;
};

enum {
	HD44780_CMD_MODE,
	HD44780_DATA_MODE
};

enum {
	HD44780_CURSOR_OFF,
	HD44780_CURSOR_ON,
	HD44780_CURSOR_BLINK
};

enum {
	HD44780_CURSOR_BLINK_STATE,
	HD44780_CURSOR_STATE,
	HD44780_DISPLAY_ON,
	HD44780_DISPLAY,
	HD44780_DDRAM = 7
};

#define HD44780_INIT 0x33
#define HD44780_4BIT_MODE 0x32

#define HD44780_DISP_ON_CURS_OFF 0x0C
#define HD44780_CURS_DEC_SCROLL_OFF 0x04
#define HD44780_CLR_SCRN 0x01
#define HD44780_GOTO_HOME 0x02
#define HD44780_GOTO_LINE2 0xC0

#define HD44780_LINE1_START 0x00
#define HD44780_LINE2_START 0x40

#endif
