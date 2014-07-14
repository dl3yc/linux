/*
 * PGA2310 SPI Digital Programmable Gain Amplifier
 *
 * Copyright 2013 Sebastian Weiss <dl3yc@darc.de>
 *
 * based on AD8366
 * Copyright 2012 Analog Devices Inc.
 *
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/bitrev.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

enum pga2310_supported_devices{
	pga2310,
	pga4311,
};

struct pga231x {
	struct spi_device *spi;
	u8 volume[4];
	u8 tx_buf[4] ____cacheline_aligned;
};

static int pga2310_write(struct iio_dev *indio_dev)
{
	struct pga231x *pga = iio_priv(indio_dev);
	int ret;
	int i;

	for(i=0; i<indio_dev->num_channels; i++)
		pga->tx_buf[i] = pga->volume[i];	// muss das sein?

	ret = spi_write(pga->spi, pga->tx_buf, indio_dev->num_channels);

	if (ret < 0)
		dev_err(&indio_dev->dev, "write failed (%d)", ret);

	return ret;
}

static void pga2310_to_frac(u8 volume, int *integer, int *fractional)
{
	int code;
	code = 5 * volume - 960;
	*integer = code / 10;
	*fractional = abs((code - *integer * 10) * 100000);
}

static int pga2310_read_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *integer, int *fractional, long mask)
{
	struct pga231x *pga = iio_priv(indio_dev);
	int ret = -EINVAL;

	mutex_lock(&indio_dev->mlock);

	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		pga2310_to_frac(pga->volume[chan->channel], integer, fractional);
		ret = IIO_VAL_INT_MINUS_MICRO_DB;
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&indio_dev->mlock);

	return ret;
};

static int pga2310_write_raw(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int integer, int fractional, long mask)
{
	struct pga231x *pga = iio_priv(indio_dev);
	int code;
	int ret;

	/* Values in dB */
	code = ((integer * 10) + (fractional / 100000));

	/* Range: -95.5 .. 35.5 dB */
	if ((code < -960) || (code > 315)) // -96.0 dB is mute condition
		return -EINVAL;

	code = (code + 960) / 5;

	mutex_lock(&indio_dev->mlock);
	switch (mask) {
	case IIO_CHAN_INFO_HARDWAREGAIN:
		pga->volume[chan->channel] = code;
		ret = pga2310_write(indio_dev);
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int pga2310_write_raw_get_fmt(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     long mask)
{
	return IIO_VAL_INT_MINUS_MICRO;
}

static const struct iio_info pga2310_info = {
	.read_raw = &pga2310_read_raw,
	.write_raw = &pga2310_write_raw,
	.write_raw_get_fmt = &pga2310_write_raw_get_fmt,
	.driver_module = THIS_MODULE,
};

#define PGA2310_CHAN(_channel) {					\
	.type = IIO_VOLTAGE,						\
	.output = 1,							\
	.indexed = 1,							\
	.channel = _channel,						\
	.info_mask_separate = BIT(IIO_CHAN_INFO_HARDWAREGAIN),		\
}

static const struct iio_chan_spec pga2310_channels[] = {
	PGA2310_CHAN(0),
	PGA2310_CHAN(1),
};

static const struct iio_chan_spec pga4311_channels[] = {
	PGA2310_CHAN(0),
	PGA2310_CHAN(1),
	PGA2310_CHAN(2),
	PGA2310_CHAN(3),
};

struct pga2310_chip_info {
	const struct iio_chan_spec *channels;
	unsigned int num_channels;
};

static const struct pga2310_chip_info pga2310_chip_infos[] = {
	[pga2310] = {
		.channels = pga2310_channels,
		.num_channels = ARRAY_SIZE(pga2310_channels)
	},
	[pga4311] = {
		.channels = pga4311_channels,
		.num_channels = ARRAY_SIZE(pga4311_channels)
	},
};

static int pga2310_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct pga231x *pga;
	const struct pga2310_chip_info *chip_info;
	int ret;
	int i;

	indio_dev = iio_device_alloc(sizeof(*pga));
	if (!indio_dev)
		return -ENOMEM;

	pga = iio_priv(indio_dev);
	pga->volume[0] = 0;
	pga->spi = spi;

	spi_set_drvdata(spi, indio_dev);

	indio_dev->dev.parent = &spi->dev;
	indio_dev->name = spi_get_device_id(spi)->name;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->info = &pga2310_info;

	chip_info = &pga2310_chip_infos[spi_get_device_id(spi)->driver_data];
	indio_dev->channels = chip_info->channels;
	indio_dev->num_channels = chip_info->num_channels;

	for(i=0; i<indio_dev->num_channels; i++)
		pga->volume[i] = 0;

	ret = iio_device_register(indio_dev);

	return ret;
}

static int pga2310_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	iio_device_unregister(indio_dev);

	return 0;
}

static const struct spi_device_id pga2310_id[] = {
	{ "pga2310", pga2310 },
	{ "pga2311", pga2310 },
	{ "pga2320", pga2310 },
	{ "pga4311", pga4311 },
	{}
};
MODULE_DEVICE_TABLE(spi, pga2310_id);

static struct spi_driver pga2310_driver = {
	.driver = {
		.name = "pga2310",
		.owner = THIS_MODULE,
	},
	.probe = pga2310_probe,
	.remove = pga2310_remove,
	.id_table = pga2310_id,
};

module_spi_driver(pga2310_driver);

MODULE_AUTHOR("Sebastian Weiss <dl3yc@darc.de>");
MODULE_DESCRIPTION("Burr Brown PGA2310");
MODULE_LICENSE("GPL");
