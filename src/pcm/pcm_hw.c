/*
 *  PCM - Hardware
 *  Copyright (c) 2000 by Abramo Bagnara <abramo@alsa-project.org>
 *
 *
 *   This library is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 of
 *   the License, or (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details.
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this library; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
  
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include "pcm_local.h"

#ifndef F_SETSIG
#define F_SETSIG 10
#endif

typedef struct {
	int fd;
	int card, device, subdevice;
	volatile snd_pcm_mmap_status_t *mmap_status;
	snd_pcm_mmap_control_t *mmap_control;
} snd_pcm_hw_t;

#define SND_FILE_PCM_STREAM_PLAYBACK		"/dev/snd/pcmC%iD%ip"
#define SND_FILE_PCM_STREAM_CAPTURE		"/dev/snd/pcmC%iD%ic"
#define SND_PCM_VERSION_MAX	SND_PROTOCOL_VERSION(2, 0, 0)

static int snd_pcm_hw_nonblock(snd_pcm_t *pcm, int nonblock)
{
	long flags;
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;

	if ((flags = fcntl(fd, F_GETFL)) < 0) {
		SYSERR("F_GETFL failed");
		return -errno;
	}
	if (nonblock)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		SYSERR("F_SETFL for O_NONBLOCK failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_async(snd_pcm_t *pcm, int sig, pid_t pid)
{
	long flags;
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;

	if ((flags = fcntl(fd, F_GETFL)) < 0) {
		SYSERR("F_GETFL failed");
		return -errno;
	}
	if (sig >= 0)
		flags |= O_ASYNC;
	else
		flags &= ~O_ASYNC;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		SYSERR("F_SETFL for O_ASYNC failed");
		return -errno;
	}
	if (sig < 0)
		return 0;
	if (sig == 0)
		sig = SIGIO;
	if (fcntl(fd, F_SETSIG, sig) < 0) {
		SYSERR("F_SETSIG failed");
		return -errno;
	}
	if (pid == 0)
		pid = getpid();
	if (fcntl(fd, F_SETOWN, pid) < 0) {
		SYSERR("F_SETOWN failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_info(snd_pcm_t *pcm, snd_pcm_info_t * info)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_INFO, info) < 0) {
		SYSERR("SND_PCM_IOCTL_INFO failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_params_info(snd_pcm_t *pcm, snd_pcm_params_info_t * info)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_PARAMS_INFO, info) < 0) {
		SYSERR("SND_PCM_IOCTL_PARAMS_INFO failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_params_t * params)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_PARAMS, params) < 0) {
		SYSERR("SND_PCM_IOCTL_PARAMS failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_setup(snd_pcm_t *pcm, snd_pcm_setup_t * setup)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_SETUP, setup) < 0) {
		SYSERR("SND_PCM_IOCTL_SETUP failed");
		return -errno;
	}
	if (setup->mmap_shape == SND_PCM_MMAP_UNSPECIFIED) {
		if (setup->xfer_mode == SND_PCM_XFER_INTERLEAVED)
			setup->mmap_shape = SND_PCM_MMAP_INTERLEAVED;
		else
			setup->mmap_shape = SND_PCM_MMAP_NONINTERLEAVED;
	}
	return 0;
}

static int snd_pcm_hw_channel_info(snd_pcm_t *pcm, snd_pcm_channel_info_t * info)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_CHANNEL_INFO, info) < 0) {
		SYSERR("SND_PCM_IOCTL_CHANNEL_INFO failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_channel_params(snd_pcm_t *pcm, snd_pcm_channel_params_t * params)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_CHANNEL_PARAMS, params) < 0) {
		SYSERR("SND_PCM_IOCTL_CHANNEL_PARAMS failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_channel_setup(snd_pcm_t *pcm, snd_pcm_channel_setup_t * setup)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_CHANNEL_SETUP, setup) < 0) {
		SYSERR("SND_PCM_IOCTL_CHANNEL_SETUP failed");
		return -errno;
	}
	if (!pcm->mmap_info)
		return 0;
	if (pcm->setup.mmap_shape == SND_PCM_MMAP_UNSPECIFIED) {
		if (pcm->setup.xfer_mode == SND_PCM_XFER_INTERLEAVED) {
			setup->running_area.addr = pcm->mmap_info->addr;
			setup->running_area.first = setup->channel * pcm->bits_per_sample;
			setup->running_area.step = pcm->bits_per_frame;
		} else {
			setup->running_area.addr = pcm->mmap_info->addr + setup->channel * pcm->setup.buffer_size * pcm->bits_per_sample / 8;
			setup->running_area.first = 0;
			setup->running_area.step = pcm->bits_per_sample;
		}
		setup->stopped_area = setup->running_area;
	} else {
		setup->running_area.addr = pcm->mmap_info->addr + (long)setup->running_area.addr;
		setup->stopped_area.addr = setup->running_area.addr;
	}
	return 0;
}

static int snd_pcm_hw_status(snd_pcm_t *pcm, snd_pcm_status_t * status)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_STATUS, status) < 0) {
		SYSERR("SND_PCM_IOCTL_STATUS failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_state(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	return hw->mmap_status->state;
}

static int snd_pcm_hw_delay(snd_pcm_t *pcm, ssize_t *delayp)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_DELAY, delayp) < 0) {
		SYSERR("SND_PCM_IOCTL_DELAY failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_prepare(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_PREPARE) < 0) {
		SYSERR("SND_PCM_IOCTL_PREPARE failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_start(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_START) < 0) {
		SYSERR("SND_PCM_IOCTL_START failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_drop(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_DROP) < 0) {
		SYSERR("SND_PCM_IOCTL_DROP failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_drain(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_DRAIN) < 0) {
		SYSERR("SND_PCM_IOCTL_DRAIN failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_pause(snd_pcm_t *pcm, int enable)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	if (ioctl(fd, SND_PCM_IOCTL_PAUSE, enable) < 0) {
		SYSERR("SND_PCM_IOCTL_PAUSE failed");
		return -errno;
	}
	return 0;
}

static ssize_t snd_pcm_hw_rewind(snd_pcm_t *pcm, size_t frames)
{
	ssize_t hw_avail;
	if (pcm->setup.xrun_mode == SND_PCM_XRUN_ASAP) {
		ssize_t d;
		int err = snd_pcm_hw_delay(pcm, &d);
		if (err < 0)
			return 0;
	}
	hw_avail = snd_pcm_mmap_hw_avail(pcm);
	if (hw_avail <= 0)
		return 0;
	if (frames > (size_t)hw_avail)
		frames = hw_avail;
	snd_pcm_mmap_appl_backward(pcm, frames);
	return frames;
}

static ssize_t snd_pcm_hw_writei(snd_pcm_t *pcm, const void *buffer, size_t size)
{
	ssize_t result;
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	snd_xferi_t xferi;
	xferi.buf = (char*) buffer;
	xferi.frames = size;
	result = ioctl(fd, SND_PCM_IOCTL_WRITEI_FRAMES, &xferi);
	if (result < 0)
		return -errno;
	return xferi.result;
}

static ssize_t snd_pcm_hw_writen(snd_pcm_t *pcm, void **bufs, size_t size)
{
	ssize_t result;
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	snd_xfern_t xfern;
	xfern.bufs = bufs;
	xfern.frames = size;
	result = ioctl(fd, SND_PCM_IOCTL_WRITEN_FRAMES, &xfern);
	if (result < 0)
		return -errno;
	return xfern.result;
}

static ssize_t snd_pcm_hw_readi(snd_pcm_t *pcm, void *buffer, size_t size)
{
	ssize_t result;
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	snd_xferi_t xferi;
	xferi.buf = buffer;
	xferi.frames = size;
	result = ioctl(fd, SND_PCM_IOCTL_READI_FRAMES, &xferi);
	if (result < 0)
		return -errno;
	return xferi.result;
}

ssize_t snd_pcm_hw_readn(snd_pcm_t *pcm, void **bufs, size_t size)
{
	ssize_t result;
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	snd_xfern_t xfern;
	xfern.bufs = bufs;
	xfern.frames = size;
	result = ioctl(fd, SND_PCM_IOCTL_READN_FRAMES, &xfern);
	if (result < 0)
		return -errno;
	return xfern.result;
}

static int snd_pcm_hw_mmap_status(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	void *ptr;
	ptr = mmap(NULL, sizeof(snd_pcm_mmap_status_t), PROT_READ, MAP_FILE|MAP_SHARED, 
		   hw->fd, SND_PCM_MMAP_OFFSET_STATUS);
	if (ptr == MAP_FAILED || ptr == NULL) {
		SYSERR("status mmap failed");
		return -errno;
	}
	hw->mmap_status = ptr;
	pcm->hw_ptr = &hw->mmap_status->hw_ptr;
	return 0;
}

static int snd_pcm_hw_mmap_control(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	void *ptr;
	ptr = mmap(NULL, sizeof(snd_pcm_mmap_control_t), PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, 
		   hw->fd, SND_PCM_MMAP_OFFSET_CONTROL);
	if (ptr == MAP_FAILED || ptr == NULL) {
		SYSERR("control mmap failed");
		return -errno;
	}
	hw->mmap_control = ptr;
	pcm->appl_ptr = &hw->mmap_control->appl_ptr;
	return 0;
}

static int snd_pcm_hw_mmap(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	snd_pcm_mmap_info_t *i = calloc(1, sizeof(*i));
	int err;
	if (!i)
		return -ENOMEM;
	if (pcm->setup.mmap_shape == SND_PCM_MMAP_UNSPECIFIED) {
		err = snd_pcm_alloc_user_mmap(pcm, i);
		if (err < 0) {
			free(i);
			return err;
		}
	} else {
		err = snd_pcm_alloc_kernel_mmap(pcm, i, hw->fd);
		if (err < 0) {
			free(i);
			return err;
		}
	}
	pcm->mmap_info = i;
	pcm->mmap_info_count = 1;
	return 0;
}

static int snd_pcm_hw_munmap_status(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	if (munmap((void*)hw->mmap_status, sizeof(*hw->mmap_status)) < 0) {
		SYSERR("status munmap failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_munmap_control(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	if (munmap(hw->mmap_control, sizeof(*hw->mmap_control)) < 0) {
		SYSERR("control munmap failed");
		return -errno;
	}
	return 0;
}

static int snd_pcm_hw_munmap(snd_pcm_t *pcm)
{
	int err = snd_pcm_free_mmap(pcm, pcm->mmap_info);
	if (err < 0)
		return err;
	pcm->mmap_info_count = 0;
	free(pcm->mmap_info);
	pcm->mmap_info = 0;
	return 0;
}

static int snd_pcm_hw_close(snd_pcm_t *pcm)
{
	snd_pcm_hw_t *hw = pcm->private;
	int fd = hw->fd;
	free(hw);
	if (close(fd)) {
		SYSERR("close failed\n");
		return -errno;
	}
	snd_pcm_hw_munmap_status(pcm);
	snd_pcm_hw_munmap_control(pcm);
	return 0;
}

static ssize_t snd_pcm_hw_mmap_forward(snd_pcm_t *pcm, size_t size)
{
	if (pcm->setup.mmap_shape == SND_PCM_MMAP_UNSPECIFIED && pcm->stream == SND_PCM_STREAM_PLAYBACK)
		return snd_pcm_write_mmap(pcm, size);
	snd_pcm_mmap_appl_forward(pcm, size);
	return size;
}

static ssize_t snd_pcm_hw_avail_update(snd_pcm_t *pcm)
{
	size_t avail;
	ssize_t err;
	if (pcm->setup.ready_mode == SND_PCM_READY_ASAP ||
	    pcm->setup.xrun_mode == SND_PCM_XRUN_ASAP) {
		ssize_t d;
		int err = snd_pcm_hw_delay(pcm, &d);
		if (err < 0)
			return err;
	}
	if (pcm->stream == SND_PCM_STREAM_PLAYBACK) {
		avail = snd_pcm_mmap_playback_avail(pcm);
	} else {
		avail = snd_pcm_mmap_capture_avail(pcm);
		if (avail > 0 && pcm->setup.mmap_shape == SND_PCM_MMAP_UNSPECIFIED) {
			err = snd_pcm_read_mmap(pcm, avail);
			if (err < 0)
				return err;
			assert((size_t)err == avail);
			return err;
		}
	}
	if (avail > pcm->setup.buffer_size)
		return -EPIPE;
	return avail;
}

static int snd_pcm_hw_set_avail_min(snd_pcm_t *pcm, size_t frames)
{
	snd_pcm_hw_t *hw = pcm->private;
	hw->mmap_control->avail_min = frames;
	return 0;
}

static void snd_pcm_hw_dump(snd_pcm_t *pcm, FILE *fp)
{
	snd_pcm_hw_t *hw = pcm->private;
	char *name = "Unknown";
	snd_card_get_name(hw->card, &name);
	fprintf(fp, "Hardware PCM card %d '%s' device %d subdevice %d\n",
		hw->card, name, hw->device, hw->subdevice);
	free(name);
	if (pcm->valid_setup) {
		fprintf(fp, "\nIts setup is:\n");
		snd_pcm_dump_setup(pcm, fp);
	}
}

snd_pcm_ops_t snd_pcm_hw_ops = {
	close: snd_pcm_hw_close,
	info: snd_pcm_hw_info,
	params_info: snd_pcm_hw_params_info,
	params: snd_pcm_hw_params,
	setup: snd_pcm_hw_setup,
	channel_info: snd_pcm_hw_channel_info,
	channel_params: snd_pcm_hw_channel_params,
	channel_setup: snd_pcm_hw_channel_setup,
	dump: snd_pcm_hw_dump,
	nonblock: snd_pcm_hw_nonblock,
	async: snd_pcm_hw_async,
	mmap: snd_pcm_hw_mmap,
	munmap: snd_pcm_hw_munmap,
};

snd_pcm_fast_ops_t snd_pcm_hw_fast_ops = {
	status: snd_pcm_hw_status,
	state: snd_pcm_hw_state,
	delay: snd_pcm_hw_delay,
	prepare: snd_pcm_hw_prepare,
	start: snd_pcm_hw_start,
	drop: snd_pcm_hw_drop,
	drain: snd_pcm_hw_drain,
	pause: snd_pcm_hw_pause,
	rewind: snd_pcm_hw_rewind,
	writei: snd_pcm_hw_writei,
	writen: snd_pcm_hw_writen,
	readi: snd_pcm_hw_readi,
	readn: snd_pcm_hw_readn,
	avail_update: snd_pcm_hw_avail_update,
	mmap_forward: snd_pcm_hw_mmap_forward,
	set_avail_min: snd_pcm_hw_set_avail_min,
};

int snd_pcm_hw_open_subdevice(snd_pcm_t **pcmp, int card, int device, int subdevice, int stream, int mode)
{
	char filename[32];
	char *filefmt;
	int ver;
	int ret = 0, fd = -1;
	int attempt = 0;
	snd_pcm_info_t info;
	int fmode;
	snd_ctl_t *ctl;
	snd_pcm_t *pcm = NULL;
	snd_pcm_hw_t *hw = NULL;

	assert(pcmp);

	if ((ret = snd_ctl_hw_open(&ctl, NULL, card)) < 0)
		return ret;

	switch (stream) {
	case SND_PCM_STREAM_PLAYBACK:
		filefmt = SND_FILE_PCM_STREAM_PLAYBACK;
		break;
	case SND_PCM_STREAM_CAPTURE:
		filefmt = SND_FILE_PCM_STREAM_CAPTURE;
		break;
	default:
		assert(0);
	}
	sprintf(filename, filefmt, card, device);

      __again:
      	if (attempt++ > 3) {
		snd_ctl_close(ctl);
		return -EBUSY;
	}
	ret = snd_ctl_pcm_prefer_subdevice(ctl, subdevice);
	if (ret < 0) {
		snd_ctl_close(ctl);
		return ret;
	}
	fmode = O_RDWR;
	if (mode & SND_PCM_NONBLOCK)
		fmode |= O_NONBLOCK;
	if (mode & SND_PCM_ASYNC)
		fmode |= O_ASYNC;
	if ((fd = open(filename, fmode)) < 0) {
		SYSERR("open %s failed", filename);
		snd_ctl_close(ctl);
		return -errno;
	}
	if (ioctl(fd, SND_PCM_IOCTL_PVERSION, &ver) < 0) {
		SYSERR("SND_PCM_IOCTL_PVERSION failed");
		ret = -errno;
		goto _err;
	}
	if (SND_PROTOCOL_INCOMPATIBLE(ver, SND_PCM_VERSION_MAX)) {
		ret = -SND_ERROR_INCOMPATIBLE_VERSION;
		goto _err;
	}
	if (subdevice >= 0) {
		memset(&info, 0, sizeof(info));
		if (ioctl(fd, SND_PCM_IOCTL_INFO, &info) < 0) {
			SYSERR("SND_PCM_IOCTL_INFO failed");
			ret = -errno;
			goto _err;
		}
		if (info.subdevice != subdevice) {
			close(fd);
			goto __again;
		}
	}
	hw = calloc(1, sizeof(snd_pcm_hw_t));
	if (!hw) {
		ret = -ENOMEM;
		goto _err;
	}
	hw->card = card;
	hw->device = device;
	hw->subdevice = subdevice;
	hw->fd = fd;

	pcm = calloc(1, sizeof(snd_pcm_t));
	if (!pcm) {
		ret = -ENOMEM;
		goto _err;
	}
	pcm->type = SND_PCM_TYPE_HW;
	pcm->stream = stream;
	pcm->mode = mode;
	pcm->ops = &snd_pcm_hw_ops;
	pcm->op_arg = pcm;
	pcm->fast_ops = &snd_pcm_hw_fast_ops;
	pcm->fast_op_arg = pcm;
	pcm->private = hw;
	pcm->poll_fd = fd;
	*pcmp = pcm;
	ret = snd_pcm_hw_mmap_status(pcm);
	if (ret < 0) {
		snd_pcm_close(pcm);
		snd_ctl_close(ctl);
		return ret;
	}
	ret = snd_pcm_hw_mmap_control(pcm);
	if (ret < 0) {
		snd_pcm_close(pcm);
		snd_ctl_close(ctl);
		return ret;
	}
	return 0;
	
 _err:
	if (hw)
		free(hw);
	if (pcm)
		free(pcm);
	close(fd);
	snd_ctl_close(ctl);
	return ret;
}

int snd_pcm_hw_open_device(snd_pcm_t **pcmp, int card, int device, int stream, int mode)
{
	return snd_pcm_hw_open_subdevice(pcmp, card, device, -1, stream, mode);
}

int snd_pcm_hw_open(snd_pcm_t **pcmp, char *name, int card, int device, int subdevice, int stream, int mode)
{
	int err = snd_pcm_hw_open_subdevice(pcmp, card, device, subdevice, stream, mode);
	if (err < 0)
		return err;
	if (name)
		(*pcmp)->name = strdup(name);
	return 0;
}

int _snd_pcm_hw_open(snd_pcm_t **pcmp, char *name, snd_config_t *conf,
		     int stream, int mode)
{
	snd_config_iterator_t i;
	long card = -1, device = 0, subdevice = -1;
	char *str;
	int err;
	snd_config_foreach(i, conf) {
		snd_config_t *n = snd_config_entry(i);
		if (strcmp(n->id, "comment") == 0)
			continue;
		if (strcmp(n->id, "type") == 0)
			continue;
		if (strcmp(n->id, "stream") == 0)
			continue;
		if (strcmp(n->id, "card") == 0) {
			err = snd_config_integer_get(n, &card);
			if (err < 0) {
				err = snd_config_string_get(n, &str);
				if (err < 0)
					return -EINVAL;
				card = snd_card_get_index(str);
				if (card < 0)
					return card;
			}
			continue;
		}
		if (strcmp(n->id, "device") == 0) {
			err = snd_config_integer_get(n, &device);
			if (err < 0)
				return err;
			continue;
		}
		if (strcmp(n->id, "subdevice") == 0) {
			err = snd_config_integer_get(n, &subdevice);
			if (err < 0)
				return err;
			continue;
		}
		return -EINVAL;
	}
	if (card < 0)
		return -EINVAL;
	return snd_pcm_hw_open(pcmp, name, card, device, subdevice, stream, mode);
}
				
