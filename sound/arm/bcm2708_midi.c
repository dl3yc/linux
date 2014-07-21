/*
 * ALSA MIDI driver for Broadcom BCM2708 SoC
 *
 * Author:	Sebastian Weiss <dl3yc@darc.de>
 *		Copyright 2014
 *
 * Based on
 *	ALSA SoC I2S Audio Layer for Broadcom BCM2708 SoC
 *	Copyright (c) by Florian Meier 2013
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
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/kfifo.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/soc.h>
#include <sound/rawmidi.h>

#include <asm/io.h>

#define DRV_NAME "bcm2708-midi"

#define GPIOFSEL(x)	(0x00+(x)*4)
#define FSEL14		0x0C
#define ALT5		0x02
#define UART1EN		0x04
#define UART1IO		0x40
#define UART1IER	0x44
#define UART1IIR	0x48
#define UART1LCR	0x4C
#define UART1CNTL	0x60
#define UART1STAT	0x64
#define UART1BAUD	0x68
#define IRQEN1		0x10
#define ENIRQ29		(1<<29)

#define BCM2708_MIDI_BAUDRATE 31250
#define UART1_CLK	250000000

#define FIFO_SIZE	128

struct bcm2708_midi {
	struct platform_device *pdev;
	struct snd_card *card;
	struct snd_rawmidi *rmidi;
	struct snd_rawmidi_substream *output;
	unsigned char *fbuf;
	struct kfifo fifo;
};

static void bcm2708_midi_activate_irq(void)
{
	/* enable transmit interrupt */
	 writel(0x02, __io_address(UART1_BASE) + UART1IER);
}

static void bcm2708_midi_deactivate_irq(void)
{
	/* disable transmit interrupt */
	 writel(0x00, __io_address(UART1_BASE) + UART1IER);
}

static void bcm2708_midi_transmit(unsigned char *data, int num)
{
	int i;
	for(i=0;i<num;i++)
		writel(data[i], __io_address(UART1_BASE) + UART1IO);
}

static irqreturn_t bcm2708_midi_interrupt(int irq, void *dev_id)
{
	struct bcm2708_midi *midi = dev_id;
	unsigned char data[8];
	int get;

	if (!kfifo_is_empty(&midi->fifo)) {
		get = kfifo_out(&midi->fifo, data, 8);
		bcm2708_midi_transmit(data, get);
	} else {
		bcm2708_midi_deactivate_irq();
	}
	return IRQ_HANDLED;
}

static int bcm2708_midi_init(void)
{
	/* enable UART */
	writel(0x01, __io_address(UART1_BASE) + UART1EN);
	/* disable receiver */
	writel(0x02, __io_address(UART1_BASE) + UART1CNTL);
	/* flush recieve UART FIFO */
	writel(0x04, __io_address(UART1_BASE) + UART1IIR);
	/* set baudrate to 31.25kBd */
	writel(UART1_CLK/(BCM2708_MIDI_BAUDRATE*8)-1, __io_address(UART1_BASE) + UART1BAUD);
	/* set UART to 8-bit mode */
	writel(0x03, __io_address(UART1_BASE) + UART1LCR);
	/* flush transmit UART FIFO */
	writel(0x04, __io_address(UART1_BASE) + UART1IIR);
	/* set GPIO14 as TXD of UART1 */
	writel((ALT5 << FSEL14), __io_address(GPIO_BASE) + GPIOFSEL(1));
	return 0;
}

static int bcm2708_midi_open(struct snd_rawmidi_substream *substream)
{
	struct bcm2708_midi *midi = substream->rmidi->card->private_data;

	kfifo_reset(&midi->fifo);
	return 0;
}

static int bcm2708_midi_close(struct snd_rawmidi_substream *substream)
{
	return 0;
}

static void bcm2708_midi_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct bcm2708_midi *midi = substream->rmidi->card->private_data;
	struct platform_device *pdev = midi->pdev;
	unsigned char data[FIFO_SIZE];
	int av, get;

	av = kfifo_avail(&midi->fifo);

	if (av == 0) {
		dev_err(&pdev->dev, "dropout!\n");
		return;
	}

	get = snd_rawmidi_transmit(substream, data, av);

	if (av < get)
		dev_err(&pdev->dev, "%d samples lost\n", get - av);

	kfifo_in(&midi->fifo, data, min(get,av));
	bcm2708_midi_activate_irq();

}

static struct snd_rawmidi_ops bcm2708_midi_ops = {
	.open = 	bcm2708_midi_open,
	.close =	bcm2708_midi_close,
	.trigger =	bcm2708_midi_trigger,
};

static int bcm2708_midi_create(struct bcm2708_midi *midi)
{
	struct platform_device *pdev = midi->pdev;
	struct snd_rawmidi *rmidi;
	int err;

	err = snd_rawmidi_new(midi->card, DRV_NAME, 0, 1, 0, &rmidi);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot create device\n");
		return err;
	}

	rmidi->info_flags = SNDRV_RAWMIDI_INFO_OUTPUT;
	snd_rawmidi_set_ops(rmidi, SNDRV_RAWMIDI_STREAM_OUTPUT,
				&bcm2708_midi_ops);

	midi->rmidi = rmidi;

	strcpy(rmidi->name, "BCM2708 Serial MIDI OUT");

	bcm2708_midi_init();

	return 0;
}

static int bcm2708_midi_init_fifo(struct bcm2708_midi *midi)
{
	struct platform_device *pdev = midi->pdev;
	int err;

	err = kfifo_alloc(&midi->fifo, FIFO_SIZE * sizeof(unsigned char), GFP_KERNEL);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot init fifo\n");
		kfree(midi->fbuf);
		return err;
	}
	return 0;
}

static int bcm2708_midi_probe(struct platform_device *pdev)
{
	struct snd_card *card = NULL;
	struct bcm2708_midi *midi;
	int err;

	err = snd_card_create(0, 0, THIS_MODULE, sizeof(*midi), &card);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot create card instance\n");
		return err;
	}

	strlcpy(card->driver, DRV_NAME, sizeof(card->driver));
	strlcpy(card->shortname, "BCM2708 MIDI", sizeof(card->shortname));

	midi = card->private_data;
	midi->pdev = pdev;
	midi->card = card;

	err = bcm2708_midi_create(midi);
	if (err < 0) {
		snd_card_free(card);
		return err;
	}

	snd_card_set_dev(card, &pdev->dev);

	err = snd_card_register(card);
	if (err < 0) {
		dev_err(&pdev->dev, "cannot register sound card\n");
		goto free_snd_card;
	}

	platform_set_drvdata(pdev, card);

	dev_info(&pdev->dev, "MIDI port created\n");

	err = request_irq(IRQ_AUX, &bcm2708_midi_interrupt, IRQF_SHARED, DRV_NAME, midi);
	if (err != 0) {
		dev_err(&pdev->dev, "cannot request irq %d\n", IRQ_AUX);
		goto free_irq;
	}

	err = bcm2708_midi_init_fifo(midi);
	if (err != 0) {
		goto free_irq;
	}

	dev_info(&pdev->dev, "KFIFO initialized\n");

	return 0;

free_irq:
	free_irq(IRQ_AUX, pdev);

free_snd_card:
	snd_card_free(card);
	return err;
}

static int bcm2708_midi_remove(struct platform_device *pdev)
{
	struct snd_card *card = platform_get_drvdata(pdev);
	struct bcm2708_midi *midi = card->private_data;
	kfree(midi->fbuf);
	free_irq(IRQ_AUX, midi);
	snd_card_free(card);
	dev_info(&pdev->dev, "MIDI port & KFIFO removed\n");
	return 0;
}

static struct platform_driver bcm2708_midi_driver = {
	.probe		= bcm2708_midi_probe,
	.remove		= bcm2708_midi_remove,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

module_platform_driver(bcm2708_midi_driver);

MODULE_ALIAS("platform:bcm2708-midi");
MODULE_DESCRIPTION("BCM2708 MIDI interface");
MODULE_AUTHOR ("Sebastian Weiss <dl3yc@darc.de>");
MODULE_LICENSE("GPL");

