/*
 * HD44780 Display Driver
 *
 * Author:	Sebastian Weiss <dl3yc@darc.de>
 *		Copyright 2014
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/hd44780.h>

#define DRV_NAME "hd44780"

static void hd44780_command(struct hd44780_platform_data *pdata, unsigned char cmd);
static void hd44780_data(struct hd44780_platform_data *pdata, unsigned char data);
static void hd44780_read(struct hd44780_platform_data *pdata, unsigned char *data, int mode);

static ssize_t read_display(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);
	int i,j;

	/* XXX: failure on reading first character! */
	//hd44780_command(pdata, HD44780_GOTO_HOME);
	hd44780_command(pdata, (1 << HD44780_DDRAM) | (HD44780_LINE1_START));

	for(i = 0; i < pdata->format.width; i++)
		hd44780_read(pdata, &buf[i], HD44780_DATA_MODE);

	/* TODO: implement more than 2 lines */
	if (pdata->format.height > 1) {
		hd44780_command(pdata, HD44780_GOTO_LINE2);
		buf[i++] = '\n';
		j = 0;
		while(j < pdata->format.width) {
			hd44780_read(pdata, &buf[i], HD44780_DATA_MODE);
			i++;
			j++;
		}
	}
	buf[i++] = '\n';
	buf[i] = '\0';
	return strlen(buf)+1;
}

static ssize_t write_display(struct device *dev, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);
	int i;
	int line = 1;

	hd44780_command(pdata, HD44780_GOTO_HOME);

	i=0;
	while ((buf[i] != '\0') && (i  < count)) {
		if ((buf[i] == '\n') || (i == pdata->format.width)) {
			if (line == pdata->format.height) {
				hd44780_command(pdata, HD44780_GOTO_HOME);
				line = 1;
			} else {
				/* TODO: implement more lines than 2 */
				hd44780_command(pdata, HD44780_GOTO_LINE2);
				line++;
			}
			i++;
		} else {
			hd44780_data(pdata, buf[i++]);
		}
	}
	return strlen(buf)+1;
}

static ssize_t read_char(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);
	hd44780_read(pdata, &buf[0], HD44780_DATA_MODE);
	buf[1] = '\n';
	buf[2] = '\0';
	return strlen(buf)+1;
}

static ssize_t write_char(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);
	hd44780_data(pdata, buf[0]);
	return strlen(buf)+1;
}

static ssize_t read_cursor(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);

	int pos;

	hd44780_read(pdata, (char *) &pos,  HD44780_CMD_MODE);
	pos &= 0x7F;

	if (pos > HD44780_LINE2_START) {
		pos -= HD44780_LINE2_START;
		pos += pdata->format.width;
		if (pos > 2* pdata->format.width-1)
			pos = -1;
	} else if (pos > pdata->format.width-1){
		pos = -1;
	}

	snprintf(buf, 5, "%d\n", pos);
	return strlen(buf)+1;
}

static ssize_t write_cursor(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);
	int pos;
	int line;
	int err = kstrtol(buf, 10, (long *) &pos);

	if (err)
		return err;

	if (pos < 0)
		return -EINVAL;

	if (pos >=  pdata->format.width *  pdata->format.height)
		return -EINVAL;

	line = pos / pdata->format.width + 1;
	pos = pos % pdata->format.width;

	/* TODO: implement more than 2 lines */
	if (line == 1)
		hd44780_command(pdata, (1 << HD44780_DDRAM) | (HD44780_LINE1_START+pos));
	else
		hd44780_command(pdata, (1 << HD44780_DDRAM) | (HD44780_LINE2_START+pos));

	return strlen(buf)+1;
}

static DEVICE_ATTR(display, S_IRUGO|S_IWUGO, read_display, write_display);
static DEVICE_ATTR(character, S_IRUGO|S_IWUGO, read_char, write_char);
static DEVICE_ATTR(cursor, S_IRUGO|S_IWUGO, read_cursor, write_cursor);

static void hd44780_read(struct hd44780_platform_data *pdata, unsigned char *data, int mode)
{
	unsigned char get;
	int i;

	gpio_direction_output(pdata->gpio.rw, 1);
	gpio_direction_output(pdata->gpio.rs, mode);

	if (pdata->mode & HD44780_MODE_8BIT) {
		gpio_direction_output(pdata->gpio.en,1);

		for(i=0; i<8; i++)
			gpio_direction_input(pdata->gpio.data[i]);

		get = 0;
		for(i=7; i>-1; i--) {
			get = (get << 1);
			get += gpio_get_value_cansleep(pdata->gpio.data[i]);
		}

		gpio_direction_output(pdata->gpio.en,0);

	} else {

		gpio_direction_output(pdata->gpio.en,1);

		for(i=4; i<8; i++)
			gpio_direction_input(pdata->gpio.data[i]);

		get = 0;
		for(i=7; i>3; i--) {
			get = (get << 1);
			get += gpio_get_value_cansleep(pdata->gpio.data[i]);
		}

		gpio_direction_output(pdata->gpio.en,0);
		udelay(100);
		gpio_direction_output(pdata->gpio.en,1);
		udelay(100);

		for(i=7; i>3; i--) {
			get = (get << 1);
			get += gpio_get_value_cansleep(pdata->gpio.data[i]);
		}
		gpio_direction_output(pdata->gpio.en,0);
	}
	*data = get;
}

static void hd44780_write(struct hd44780_platform_data *pdata, unsigned char data, int mode)
{
	int i;

	gpio_direction_output(pdata->gpio.rw, 0);
	gpio_direction_output(pdata->gpio.rs, mode);

	if (pdata->mode & HD44780_MODE_8BIT) {
		for(i=0; i<8; i++)
			gpio_direction_output(pdata->gpio.data[i],data & (1<<i));
		gpio_direction_output(pdata->gpio.en,1);
		udelay(100);
		gpio_direction_output(pdata->gpio.en,0);
		udelay(100);
	} else {
		for(i=4; i<8; i++)
			gpio_direction_output(pdata->gpio.data[i],data & (1<<i));
		gpio_direction_output(pdata->gpio.en,1);
		udelay(100);
		gpio_direction_output(pdata->gpio.en,0);
		udelay(100);
		for(i=4; i<8; i++)
			gpio_direction_output(pdata->gpio.data[i],data & (1<<(i-4)));
		gpio_direction_output(pdata->gpio.en,1);
		udelay(100);
		gpio_direction_output(pdata->gpio.en,0);
		udelay(100);
	}
}

static void hd44780_command(struct hd44780_platform_data *pdata, unsigned char cmd)
{
	hd44780_write(pdata, cmd, HD44780_CMD_MODE);
}

static void hd44780_data(struct hd44780_platform_data *pdata, unsigned char data)
{
	hd44780_write(pdata, data, HD44780_DATA_MODE);
}

static void hd44780_set_lines_font(struct hd44780_platform_data *pdata)
{
	int mode = pdata->mode;
	int lines = pdata->format.height-1;
	int font = pdata->font;
	int cmd = (1<<5) | (mode << 4) | (lines << 3) | (font << 2);

	hd44780_command(pdata, cmd);
}

static void hd44780_free_gpio(struct hd44780_platform_data *pdata)
{
	int i;
	int first_gpio = (pdata->mode == HD44780_MODE_8BIT) ? 0 : 4;

	for(i=first_gpio; i<8; i++)
		gpio_free(pdata->gpio.data[i]);
	gpio_free(pdata->gpio.rw);
	gpio_free(pdata->gpio.rs);
	gpio_free(pdata->gpio.en);
}

static void hd44780_init(struct hd44780_platform_data *pdata)
{
	int i;
	unsigned char *text = pdata->init_text;
	int len = strlen(text);

	hd44780_command(pdata, HD44780_INIT);
	msleep(4);
	hd44780_command(pdata, HD44780_INIT);
	if (!(pdata->mode & HD44780_MODE_8BIT))
		 hd44780_command(pdata, HD44780_4BIT_MODE);
	hd44780_set_lines_font(pdata);
	hd44780_command(pdata, HD44780_DISP_ON_CURS_OFF);
	hd44780_command(pdata, HD44780_CURS_DEC_SCROLL_OFF);
	hd44780_command(pdata, HD44780_CLR_SCRN);
	hd44780_command(pdata, HD44780_GOTO_HOME);

	for(i=0; i < len; i++)
		hd44780_data(pdata, text[i]);

}

static int hd44780_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);
	int first_gpio;
	int i;
	int ret;

	if (pdata == NULL) {
		dev_err(dev, "no platform data\n");
		return -EINVAL;
	}

	first_gpio = (pdata->mode == HD44780_MODE_8BIT) ? 0 : 4;

	for(i=first_gpio; i<8; i++) {
		ret = gpio_request(pdata->gpio.data[i], 0);
		if (ret != 0) {
			dev_err(dev, "gpio request of D%d(%d) failed\n", i, pdata->gpio.data[i]);
			goto gpio_err;
		}
	}


	ret = gpio_request(pdata->gpio.rw, 0);
	if (pdata->mode & HD44780_MODE_WRITEONLY) {
		if (ret == 0)
			gpio_direction_output(pdata->gpio.rw, 0);
	} else {
		if (ret != 0) {
			dev_err(dev, "gpio request of RW failed\n");
			goto gpio_err;
		}
	}

	ret = gpio_request(pdata->gpio.rs, 0);
	if (ret != 0) {
		dev_err(dev, "gpio request of RS failed\n");
		goto gpio_err;
	}

	ret = gpio_request(pdata->gpio.en, 0);
	if (ret != 0) {
		dev_err(dev, "gpio request of EN failed\n");
		goto gpio_err;
	}

	hd44780_init(pdata);

	if (pdata->mode & HD44780_MODE_WRITEONLY) {
		dev_attr_display.attr.mode = S_IWUGO;
		dev_attr_character.attr.mode = S_IWUGO;
		dev_attr_cursor.attr.mode = S_IWUGO;
	}

	device_create_file(dev, &dev_attr_display);
	device_create_file(dev, &dev_attr_character);
	device_create_file(dev, &dev_attr_cursor);

	dev_info(dev, "display initialized\n");
	return 0;

gpio_err:
	hd44780_free_gpio(pdata);
	return -EPROBE_DEFER;
}

static int hd44780_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct hd44780_platform_data *pdata = dev_get_platdata(dev);
	hd44780_free_gpio(pdata);
	device_remove_file(dev, &dev_attr_display);
	device_remove_file(dev, &dev_attr_character);
	device_remove_file(dev, &dev_attr_cursor);
	dev_info(dev, "device removed\n");
	return 0;
}

static struct platform_driver hd44780_driver = {
	.probe		= hd44780_probe,
	.remove		= hd44780_remove,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(hd44780_driver);

MODULE_ALIAS("platform:hd44780");
MODULE_DESCRIPTION("HD44780 display over gpio");
MODULE_AUTHOR ("Sebastian Weiss <dl3yc@darc.de>");
MODULE_LICENSE("GPL");
