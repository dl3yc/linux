#
# Makefile for ALSA
#

obj-$(CONFIG_SND_ARMAACI)	+= snd-aaci.o
snd-aaci-objs			:= aaci.o

obj-$(CONFIG_SND_PXA2XX_PCM)	+= snd-pxa2xx-pcm.o
snd-pxa2xx-pcm-objs		:= pxa2xx-pcm.o

obj-$(CONFIG_SND_PXA2XX_LIB)	+= snd-pxa2xx-lib.o
snd-pxa2xx-lib-y		:= pxa2xx-pcm-lib.o
snd-pxa2xx-lib-$(CONFIG_SND_PXA2XX_LIB_AC97)	+= pxa2xx-ac97-lib.o

obj-$(CONFIG_SND_PXA2XX_AC97)	+= snd-pxa2xx-ac97.o
snd-pxa2xx-ac97-objs		:= pxa2xx-ac97.o

obj-$(CONFIG_SND_BCM2835)	+= snd-bcm2835.o
snd-bcm2835-objs		:= bcm2835.o bcm2835-ctl.o bcm2835-pcm.o bcm2835-vchiq.o

obj-$(CONFIG_SND_BCM2708_MIDI)	+= snd-bcm2708-midi.o
snd-bcm2708-midi-objs		:= bcm2708_midi.o

ccflags-y += -Idrivers/misc/vc04_services -Idrivers/misc/vc04_services/interface/vcos/linuxkernel -D__VCCOREVER__=0x04000000
