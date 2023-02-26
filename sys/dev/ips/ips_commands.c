/*-
 * Written by: David Jeffery
 * Copyright (c) 2002 Adaptec Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/ips/ips_commands.c,v 1.11.6.1 2005/01/13 00:46:40 scottl Exp $
 */

#include <dev/ips/ips.h>

/*
 * This is an interrupt callback.  It is called from
 * interrupt context when the adapter has completed the
 * command.  This very generic callback simply stores
 * the command's return value in command->arg and wake's
 * up anyone waiting on the command. 
 */
static void ips_wakeup_callback(ips_command_t *command)
{
	ips_cmd_status_t *status;
	status = command->arg;
	status->value = command->status.value;
	bus_dmamap_sync(command->sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_POSTWRITE);
	wakeup(status);
}
/* Below are a series of functions for sending an IO request
 * to the adapter.  The flow order is: start, send, callback, finish.
 * The caller must have already assembled an iorequest struct to hold
 * the details of the IO request. */
static void ips_io_request_finish(ips_command_t *command)
{

	struct buf *iobuf = command->arg;
	if(ips_read_request(iobuf)) {
		bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_POSTREAD);
	} else {
		bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_POSTWRITE);
	}
	bus_dmamap_unload(command->data_dmatag, command->data_dmamap);
	bus_dmamap_destroy(command->data_dmatag, command->data_dmamap);
	if(COMMAND_ERROR(&command->status)){
		iobuf->b_flags |=B_ERROR;
		iobuf->b_error = EIO;
	}
	ips_insert_free_cmd(command->sc, command);
	ipsd_finish(iobuf);
}

static void ips_io_request_callback(void *cmdptr, bus_dma_segment_t *segments,int segnum, int error)
{
	ips_softc_t *sc;
	ips_command_t *command = cmdptr;
	ips_sg_element_t *sg_list;
	ips_io_cmd *command_struct;
	struct buf *iobuf = command->arg;
	int i, length = 0;
	u_int8_t cmdtype;

	sc = command->sc;
	if(error){
		printf("ips: error = %d in ips_sg_request_callback\n", error);
		bus_dmamap_unload(command->data_dmatag, command->data_dmamap);
		bus_dmamap_destroy(command->data_dmatag, command->data_dmamap);
		iobuf->b_flags |= B_ERROR;
		iobuf->b_error = ENOMEM;
		ips_insert_free_cmd(sc, command);
		ipsd_finish(iobuf);
		return;
	}
	command_struct = (ips_io_cmd *)command->command_buffer;
	command_struct->id = command->id;
	command_struct->drivenum = (uintptr_t)iobuf->b_driver1;
	if(segnum != 1){
		if(ips_read_request(iobuf))
			cmdtype = IPS_SG_READ_CMD;
		else
			cmdtype = IPS_SG_WRITE_CMD;
		command_struct->segnum = segnum;
		sg_list = (ips_sg_element_t *)((u_int8_t *)
			   command->command_buffer + IPS_COMMAND_LEN);
		for(i = 0; i < segnum; i++){
			sg_list[i].addr = segments[i].ds_addr;
			sg_list[i].len = segments[i].ds_len;
			length += segments[i].ds_len;
		}
		command_struct->buffaddr = 
	    	    (u_int32_t)command->command_phys_addr + IPS_COMMAND_LEN;
	} else {
		if(ips_read_request(iobuf))
			cmdtype = IPS_READ_CMD;
		else
			cmdtype = IPS_WRITE_CMD;
		command_struct->buffaddr = segments[0].ds_addr;
		length = segments[0].ds_len;
	}
	command_struct->command = cmdtype;
	command_struct->lba = iobuf->b_pblkno;
	length = (length + IPS_BLKSIZE - 1)/IPS_BLKSIZE;
	command_struct->length = length;
	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	if(ips_read_request(iobuf)) {
		bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_PREREAD);
	} else {
		bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_PREWRITE);
	}
	PRINTF(10, "ips test: command id: %d segments: %d "
		"pblkno: %lld length: %d, ds_len: %d\n", command->id, segnum,
		iobuf->b_pblkno, length, segments[0].ds_len);

	sc->ips_issue_cmd(command);
	return;
}

static int ips_send_io_request(ips_command_t *command)
{
	ips_softc_t *sc = command->sc;
	struct buf *iobuf = command->arg;
	command->data_dmatag = sc->sg_dmatag;
	if(bus_dmamap_create(command->data_dmatag, 0, &command->data_dmamap)){
		device_printf(sc->dev, "dmamap failed\n");
		iobuf->b_flags |= B_ERROR;
		iobuf->b_error = ENOMEM;
		ips_insert_free_cmd(sc, command);
		ipsd_finish(iobuf);
		return 0;
	}
	command->callback = ips_io_request_finish;
	PRINTF(10, "ips test: : bcount %ld\n", iobuf->b_bcount);
	bus_dmamap_load(command->data_dmatag, command->data_dmamap,
			iobuf->b_data, iobuf->b_bcount,
			ips_io_request_callback, command, 0);
	return 0;
} 

void ips_start_io_request(ips_softc_t *sc)
{
	struct buf *iobuf;

	iobuf = bufq_first(&sc->queue);
	if(!iobuf) {
		return;
	}

	if(ips_get_free_cmd(sc, ips_send_io_request, iobuf, IPS_NOWAIT_FLAG)){
		return;
	}
	
	bufq_remove(&sc->queue, iobuf);
	return;
}

/* Below are a series of functions for sending an adapter info request 
 * to the adapter.  The flow order is: get, send, callback. It uses
 * the generic finish callback at the top of this file. 
 * This can be used to get configuration/status info from the card */
static void ips_adapter_info_callback(void *cmdptr, bus_dma_segment_t *segments,int segnum, int error)
{
	ips_softc_t *sc;
	ips_command_t *command = cmdptr;
	ips_adapter_info_cmd *command_struct;
	sc = command->sc;
	if(error){
		ips_cmd_status_t * status = command->arg;
		status->value = IPS_ERROR_STATUS; /* a lovely error value */
		ips_insert_free_cmd(sc, command);
		printf("ips: error = %d in ips_get_adapter_info\n", error);
		return;
	}
	command_struct = (ips_adapter_info_cmd *)command->command_buffer;
	command_struct->command = IPS_ADAPTER_INFO_CMD;
	command_struct->id = command->id;
	command_struct->buffaddr = segments[0].ds_addr;

	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
			BUS_DMASYNC_PREREAD);
	sc->ips_issue_cmd(command);
}



static int ips_send_adapter_info_cmd(ips_command_t *command)
{
	int error = 0;
	ips_softc_t *sc = command->sc;
	ips_cmd_status_t *status = command->arg;

	if (bus_dma_tag_create(	/* parent    */	sc->adapter_dmatag,
				/* alignemnt */	1,
				/* boundary  */	0,
				/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
				/* highaddr  */	BUS_SPACE_MAXADDR,
				/* filter    */	NULL,
				/* filterarg */	NULL,
				/* maxsize   */	IPS_ADAPTER_INFO_LEN,
				/* numsegs   */	1,
				/* maxsegsize*/	IPS_ADAPTER_INFO_LEN,
				/* flags     */	0,
				&command->data_dmatag) != 0) {
                printf("ips: can't alloc dma tag for adapter status\n");
		error = ENOMEM;
		goto exit;
        }
	if(bus_dmamem_alloc(command->data_dmatag, &command->data_buffer, 
	   BUS_DMA_NOWAIT, &command->data_dmamap)){
		error = ENOMEM;
		goto exit;
	}
	command->callback = ips_wakeup_callback;
	asleep(status, 0, "ips", 30*hz);
	bus_dmamap_load(command->data_dmatag, command->data_dmamap, 
			command->data_buffer,IPS_ADAPTER_INFO_LEN, 
			ips_adapter_info_callback, command, BUS_DMA_NOWAIT);

	if (await(-1, -1))
		error = ETIMEDOUT;
	else {
		bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_POSTREAD);
		memcpy(&(sc->adapter_info), command->data_buffer, 
			IPS_ADAPTER_INFO_LEN);
	}
	bus_dmamap_unload(command->data_dmatag, command->data_dmamap);

exit:
	/* I suppose I should clean up my memory allocations */
	bus_dmamem_free(command->data_dmatag, command->data_buffer, 
			command->data_dmamap);
	bus_dma_tag_destroy(command->data_dmatag);
	ips_insert_free_cmd(sc, command);
	return error;
}

int ips_get_adapter_info(ips_softc_t *sc)
{
	int error = 0;
	ips_cmd_status_t *status;
	status = malloc(sizeof(ips_cmd_status_t), M_IPSBUF, M_NOWAIT|M_ZERO);
	if(!status)
		return ENOMEM;
	if(ips_get_free_cmd(sc, ips_send_adapter_info_cmd, status, 
			    IPS_NOWAIT_FLAG) > 0){
		device_printf(sc->dev, "unable to get adapter configuration\n");
		free(status, M_IPSBUF);
		return ENXIO;
	}
	if (COMMAND_ERROR(status)){
		error = ENXIO;
	}
	free(status, M_IPSBUF);
	return error;
}

/* Below are a series of functions for sending a drive info request 
 * to the adapter.  The flow order is: get, send, callback. It uses
 * the generic finish callback at the top of this file. 
 * This can be used to get drive status info from the card */
static void ips_drive_info_callback(void *cmdptr, bus_dma_segment_t *segments,int segnum, int error)
{
	ips_softc_t *sc;
	ips_command_t *command = cmdptr;
	ips_drive_cmd *command_struct;
	sc = command->sc;
	if(error){
		ips_cmd_status_t * status = command->arg;
                status->value = IPS_ERROR_STATUS;
		ips_insert_free_cmd(sc, command);
		printf("ips: error = %d in ips_get_drive_info\n", error);
		return;
	}
	command_struct = (ips_drive_cmd *)command->command_buffer;
	command_struct->command = IPS_DRIVE_INFO_CMD;
	command_struct->id = command->id;
	command_struct->buffaddr = segments[0].ds_addr;

	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
			BUS_DMASYNC_PREREAD);
	sc->ips_issue_cmd(command);
}

static int ips_send_drive_info_cmd(ips_command_t *command)
{
	int error = 0;
	ips_softc_t *sc = command->sc;
	ips_cmd_status_t *status = command->arg;
	ips_drive_info_t *driveinfo;

	if (bus_dma_tag_create(	/* parent    */	sc->adapter_dmatag,
				/* alignemnt */	1,
				/* boundary  */	0,
				/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
				/* highaddr  */	BUS_SPACE_MAXADDR,
				/* filter    */	NULL,
				/* filterarg */	NULL,
				/* maxsize   */	IPS_DRIVE_INFO_LEN,
				/* numsegs   */	1,
				/* maxsegsize*/	IPS_DRIVE_INFO_LEN,
				/* flags     */	0,
				&command->data_dmatag) != 0) {
                printf("ips: can't alloc dma tag for drive status\n");
		error = ENOMEM;
		goto exit;
        }
	if(bus_dmamem_alloc(command->data_dmatag, &command->data_buffer, 
	   		    BUS_DMA_NOWAIT, &command->data_dmamap)){
		error = ENOMEM;
		goto exit;
	}
	command->callback = ips_wakeup_callback;
	asleep(status, 0, "ips", 10*hz);
	bus_dmamap_load(command->data_dmatag, command->data_dmamap, 
			command->data_buffer,IPS_DRIVE_INFO_LEN, 
			ips_drive_info_callback, command, BUS_DMA_NOWAIT);
	if (await(-1, -1))
		error = ETIMEDOUT;
	else {
		bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_POSTREAD);
		driveinfo = command->data_buffer;
		memcpy(sc->drives, driveinfo->drives, sizeof(ips_drive_t) * 8);	
		sc->drivecount = driveinfo->drivecount;
		device_printf(sc->dev, "logical drives: %d\n",sc->drivecount);
	}
	bus_dmamap_unload(command->data_dmatag, command->data_dmamap);

exit:
	/* I suppose I should clean up my memory allocations */
	bus_dmamem_free(command->data_dmatag, command->data_buffer, 
			command->data_dmamap);
	bus_dma_tag_destroy(command->data_dmatag);
	ips_insert_free_cmd(sc, command);
	return error;

}
int ips_get_drive_info(ips_softc_t *sc)
{
	int error = 0;
	ips_cmd_status_t *status;
	status = malloc(sizeof(ips_cmd_status_t), M_IPSBUF, M_NOWAIT|M_ZERO);
	if(!status)
		return ENOMEM;
	if(ips_get_free_cmd(sc, ips_send_drive_info_cmd, status, 
			    IPS_NOWAIT_FLAG) > 0){
		free(status, M_IPSBUF);
		device_printf(sc->dev, "unable to get drive configuration\n");
		return ENXIO;
	}
	if(COMMAND_ERROR(status)){
		error = ENXIO;
	}
	free(status, M_IPSBUF);
	return error;
}

/* Below is a pair of functions for making sure data is safely
 * on disk by flushing the adapter's cache. */
static int ips_send_flush_cache_cmd(ips_command_t *command)
{
	ips_softc_t *sc = command->sc;
	ips_cmd_status_t *status = command->arg;
	ips_generic_cmd *command_struct;

	PRINTF(10,"ips test: got a command, building flush command\n");
	command->callback = ips_wakeup_callback;
	command_struct = (ips_generic_cmd *)command->command_buffer;
	command_struct->command = IPS_CACHE_FLUSH_CMD;
	command_struct->id = command->id;
	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	asleep(status, 0, "slush2", 0);
	sc->ips_issue_cmd(command);
	await(-1, -1);
	ips_insert_free_cmd(sc, command);
	return 0;
}

int ips_flush_cache(ips_softc_t *sc)
{
	ips_cmd_status_t *status;
	status = malloc(sizeof(ips_cmd_status_t), M_IPSBUF, M_NOWAIT|M_ZERO);
	if(!status)
		return ENOMEM;
	device_printf(sc->dev, "flushing cache\n");
	if(ips_get_free_cmd(sc, ips_send_flush_cache_cmd, status, 
			    IPS_NOWAIT_FLAG)){
		free(status, M_IPSBUF);
		device_printf(sc->dev, "ERROR: unable to get a command! can't flush cache!\n");
	}
	if(COMMAND_ERROR(status)){
		device_printf(sc->dev, "ERROR: cache flush command failed!\n");
	}
	free(status, M_IPSBUF);
	return 0;
}

/* Simplified localtime to provide timevalues for ffdc.
 * Taken from libc/stdtime/localtime.c
 */
void static ips_ffdc_settime(ips_adapter_ffdc_cmd *command, time_t sctime)
{
	long	days, rem, y;
	int	yleap, *ip, month;
	int	year_lengths[2] = { IPS_DAYSPERNYEAR, IPS_DAYSPERLYEAR };
	int	mon_lengths[2][IPS_MONSPERYEAR] = {
		{ 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
		{ 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
	};

	days = sctime / IPS_SECSPERDAY;
	rem  = sctime % IPS_SECSPERDAY;

	command->hour = rem / IPS_SECSPERHOUR;
	rem           = rem % IPS_SECSPERHOUR;

	command->minute = rem / IPS_SECSPERMIN;
	command->second = rem % IPS_SECSPERMIN;

	y = IPS_EPOCH_YEAR;
	while (days < 0 || days >= (long) year_lengths[yleap = ips_isleap(y)]) {
		long    newy;

		newy = y + days / IPS_DAYSPERNYEAR;
		if (days < 0)
			--newy;
		days -= (newy - y) * IPS_DAYSPERNYEAR +
			IPS_LEAPS_THRU_END_OF(newy - 1) -
			IPS_LEAPS_THRU_END_OF(y - 1);
		y = newy;
	}
	command->yearH = y / 100;
	command->yearL = y % 100;
	ip = mon_lengths[yleap];
	for(month = 0; days >= (long) ip[month]; ++month)
		days = days - (long) ip[month];
	command->month = month + 1;
	command->day = days + 1;
}

static int ips_send_ffdc_reset_cmd(ips_command_t *command)
{
	ips_softc_t *sc = command->sc;
	ips_cmd_status_t *status = command->arg;
	ips_adapter_ffdc_cmd *command_struct;

	PRINTF(10,"ips test: got a command, building ffdc reset command\n");
	command->callback = ips_wakeup_callback;
	command_struct = (ips_adapter_ffdc_cmd *)command->command_buffer;
	command_struct->command = IPS_FFDC_CMD;
	command_struct->id = command->id;
	command_struct->reset_count = sc->ffdc_resetcount;
	command_struct->reset_type  = 0x0;
	ips_ffdc_settime(command_struct, sc->ffdc_resettime.tv_sec);

	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap,
			BUS_DMASYNC_PREWRITE);
	asleep(status, 0, "ips_ffdc", 0);
	sc->ips_issue_cmd(command);
	await(-1, -1);
	ips_insert_free_cmd(sc, command);
	return 0;
}

int ips_ffdc_reset(ips_softc_t *sc)
{
	ips_cmd_status_t *status;
	status = malloc(sizeof(ips_cmd_status_t), M_IPSBUF, M_NOWAIT|M_ZERO);
	if(!status)
		return ENOMEM;
	if(ips_get_free_cmd(sc, ips_send_ffdc_reset_cmd, status,
			    IPS_NOWAIT_FLAG)){
		free(status, M_IPSBUF);
		device_printf(sc->dev, "ERROR: unable to get a command! can't send ffdc reset!\n");
	}
	if(COMMAND_ERROR(status)){
		device_printf(sc->dev, "ERROR: ffdc reset command failed!\n");
	}
	free(status, M_IPSBUF);
	return 0;
}

static void ips_write_nvram(ips_command_t *command){
	ips_softc_t *sc = command->sc;
	ips_rw_nvram_cmd *command_struct;
	ips_nvram_page5 *nvram;

	/*FIXME check for error */
	command->callback = ips_wakeup_callback;
	command_struct = (ips_rw_nvram_cmd *)command->command_buffer;
	command_struct->command = IPS_RW_NVRAM_CMD;
	command_struct->id = command->id;
	command_struct->pagenum = 5;
	command_struct->rw	= 1; /*write*/
	bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_POSTREAD);
	nvram = command->data_buffer;
	/* retrieve adapter info and save in sc */
	sc->adapter_type = nvram->adapter_type;

	strncpy(nvram->driver_high, IPS_VERSION_MAJOR, 4);
	strncpy(nvram->driver_low, IPS_VERSION_MINOR, 4);
	nvram->operating_system = IPS_OS_FREEBSD;	
	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	sc->ips_issue_cmd(command);
}

static void ips_read_nvram_callback(void *cmdptr, bus_dma_segment_t *segments,int segnum, int error)
{
	ips_softc_t *sc;
	ips_command_t *command = cmdptr;
	ips_rw_nvram_cmd *command_struct;
	sc = command->sc;
	if(error){
		ips_cmd_status_t * status = command->arg;
                status->value = IPS_ERROR_STATUS;
		ips_insert_free_cmd(sc, command);
		printf("ips: error = %d in ips_read_nvram_callback\n", error);
		return;
	}
	command_struct = (ips_rw_nvram_cmd *)command->command_buffer;
	command_struct->command = IPS_RW_NVRAM_CMD;
	command_struct->id = command->id;
	command_struct->pagenum = 5;
	command_struct->rw = 0;
	command_struct->buffaddr = segments[0].ds_addr;

	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
			BUS_DMASYNC_PREREAD);
	sc->ips_issue_cmd(command);
}

static int ips_read_nvram(ips_command_t *command){
	int error = 0;
	ips_softc_t *sc = command->sc;
	ips_cmd_status_t *status = command->arg;

	if (bus_dma_tag_create(	/* parent    */	sc->adapter_dmatag,
				/* alignemnt */	1,
				/* boundary  */	0,
				/* lowaddr   */	BUS_SPACE_MAXADDR_32BIT,
				/* highaddr  */	BUS_SPACE_MAXADDR,
				/* filter    */	NULL,
				/* filterarg */	NULL,
				/* maxsize   */	IPS_NVRAM_PAGE_SIZE,
				/* numsegs   */	1,
				/* maxsegsize*/	IPS_NVRAM_PAGE_SIZE,
				/* flags     */	0,
				&command->data_dmatag) != 0) {
                printf("ips: can't alloc dma tag for nvram\n");
		error = ENOMEM;
		goto exit;
        }
	if(bus_dmamem_alloc(command->data_dmatag, &command->data_buffer, 
	   		    BUS_DMA_NOWAIT, &command->data_dmamap)){
		error = ENOMEM;
		goto exit;
	}
	command->callback = ips_write_nvram;
	asleep(status, 0, "ips", 0);
	bus_dmamap_load(command->data_dmatag, command->data_dmamap, 
			command->data_buffer,IPS_NVRAM_PAGE_SIZE, 
			ips_read_nvram_callback, command, BUS_DMA_NOWAIT);
	if (await(-1, -1))
		error = ETIMEDOUT;
	else {
		bus_dmamap_sync(command->data_dmatag, command->data_dmamap, 
				BUS_DMASYNC_POSTWRITE);
	}
	bus_dmamap_unload(command->data_dmatag, command->data_dmamap);

exit:
	bus_dmamem_free(command->data_dmatag, command->data_buffer, 
			command->data_dmamap);
	bus_dma_tag_destroy(command->data_dmatag);
	ips_insert_free_cmd(sc, command);
	return error;
}

int ips_update_nvram(ips_softc_t *sc)
{
	ips_cmd_status_t *status;
	status = malloc(sizeof(ips_cmd_status_t), M_IPSBUF, M_NOWAIT|M_ZERO);
	if(!status)
		return ENOMEM;
	if(ips_get_free_cmd(sc, ips_read_nvram, status, IPS_NOWAIT_FLAG)){
		free(status, M_IPSBUF);
		device_printf(sc->dev, "ERROR: unable to get a command! can't update nvram\n");
		return 1;
	}
	if(COMMAND_ERROR(status)){
		device_printf(sc->dev, "ERROR: nvram update command failed!\n");
	}
	free(status, M_IPSBUF);
	return 0;


}


static int ips_send_config_sync_cmd(ips_command_t *command)
{
	ips_softc_t *sc = command->sc;
	ips_cmd_status_t *status = command->arg;
	ips_generic_cmd *command_struct;

	PRINTF(10,"ips test: got a command, building flush command\n");
	command->callback = ips_wakeup_callback;
	command_struct = (ips_generic_cmd *)command->command_buffer;
	command_struct->command = IPS_CONFIG_SYNC_CMD;
	command_struct->id = command->id;
	command_struct->reserve2 = IPS_POCL;
	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	asleep(status, 0, "ipssyn", 0);
	sc->ips_issue_cmd(command);
	await(-1, -1);
	ips_insert_free_cmd(sc, command);
	return 0;
}

static int ips_send_error_table_cmd(ips_command_t *command)
{
	ips_softc_t *sc = command->sc;
	ips_cmd_status_t *status = command->arg;
	ips_generic_cmd *command_struct;

	PRINTF(10,"ips test: got a command, building errortable command\n");
	command->callback = ips_wakeup_callback;
	command_struct = (ips_generic_cmd *)command->command_buffer;
	command_struct->command = IPS_ERROR_TABLE_CMD;
	command_struct->id = command->id;
	command_struct->reserve2 = IPS_CSL;
	bus_dmamap_sync(sc->command_dmatag, command->command_dmamap, 
			BUS_DMASYNC_PREWRITE);
	asleep(status, 0, "ipsetc", 0);
	sc->ips_issue_cmd(command);
	await(-1, -1);
	ips_insert_free_cmd(sc, command);
	return 0;
}


int ips_clear_adapter(ips_softc_t *sc)
{
	ips_cmd_status_t *status;
	status = malloc(sizeof(ips_cmd_status_t), M_IPSBUF, M_NOWAIT|M_ZERO);
	if(!status)
		return ENOMEM;
	device_printf(sc->dev, "syncing config\n");
	if(ips_get_free_cmd(sc, ips_send_config_sync_cmd, status, 
			    IPS_NOWAIT_FLAG)){
		free(status, M_IPSBUF);
		device_printf(sc->dev, "ERROR: unable to get a command! can't sync cache!\n");
		return 1;
	}
	if(COMMAND_ERROR(status)){
		free(status, M_IPSBUF);
		device_printf(sc->dev, "ERROR: cache sync command failed!\n");
		return 1;
	}

	device_printf(sc->dev, "clearing error table\n");
	if(ips_get_free_cmd(sc, ips_send_error_table_cmd, status, 
			    IPS_NOWAIT_FLAG)){
		free(status, M_IPSBUF);
		device_printf(sc->dev, "ERROR: unable to get a command! can't sync cache!\n");
		return 1;
	}
	if(COMMAND_ERROR(status)){
		device_printf(sc->dev, "ERROR: etable command failed!\n");
		free(status, M_IPSBUF);
		return 1;
	}

	free(status, M_IPSBUF);
	return 0;
}
