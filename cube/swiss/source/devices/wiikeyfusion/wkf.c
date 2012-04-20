/**
 * Wiikey Fusion Driver for Gamecube & Wii
 *
 * Written by emu_kidid
**/

#include <stdio.h>
#include <gccore.h>		/*** Wrapper to include common libogc headers ***/
#include <ogcsys.h>		/*** Needed for console support ***/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <debug.h>
#include <ogc/exi.h>
#include <wkf.h>
#include "main.h"
#include "swiss.h"
#include "gui/FrameBufferMagic.h"
#include "gui/IPLFontWrite.h"

u8 wkfBuffer[0x8000] ATTRIBUTE_ALIGN (32);    // One DVD Sector

static int wkfInitialized = 0;
static volatile unsigned int* const wkf = (unsigned int*)0xCC006000;
static volatile unsigned int* const pireg = (unsigned int*)0xCC003000;

void __wkfReset() {
	u32 val;
	val = pireg[9];
	pireg[9] = ((val&~4)|1);
	usleep(12);
	val |= 1 | 4;
	pireg[9] = val;
}

unsigned int __wkfCmdImm(unsigned int cmd, unsigned int p1, unsigned int p2) {
  wkf[2] = cmd;
  wkf[3] = p1;
  wkf[4] = p2;
  wkf[6] = 0;
  wkf[8] = 0;
  wkf[7] = 1;
  while(wkf[7] & 1);	// Wait for IMM command to finish up
  return wkf[8];
}

// Not sure what this does.
unsigned int wkfReadSpecial3() {
	return __wkfCmdImm(0xDF000000,	0x00030000, 0x00000000);
}

unsigned int wkfReadSwitches() {
	return ((__wkfCmdImm(0xDF000000,	0x00030000, 0x00000000) >> 17) & 3);
}

// Write to the WKF RAM (0xFFFF is the max offset)
void wkfWriteRam(int offset, int data) {
	__wkfCmdImm(0xDD000000,	offset, data);
}

// Write the WKF base offset to base all reads from
void wkfWriteOffset(int offset) {
	__wkfCmdImm(0xDE000000, offset, 0x5A000000);
}

// Returns SD slot status
unsigned int wkfGetSlotStatus() {
	return __wkfCmdImm(0xDF000000, 0x00010000, 0x00000000);
}

// Reads DVD sectors (returns 0 on success)
void __wkfReadSectors(void* dst, unsigned int len, u32 offset) {
	wkf[2] = 0xA8000000;
	wkf[3] = (u32)(offset>>2);
	wkf[4] = len;
	wkf[5] = (u32)dst;
	wkf[6] = len;
	wkf[7] = 3; // DMA | START
	DCInvalidateRange(dst, len);
	while (wkf[7] & 1);
}

void wkfRead(void* dst, int len, u32 offset)
{
	u8 *sector_buffer = &wkfBuffer[0];
	while (len)
	{
		u32 sector = offset / 2048;
		__wkfReadSectors(sector_buffer, 2048, sector * 2048);
		u32 off = offset & 2047;

		int rl = 2048 - off;
		if (rl > len)
			rl = len;
		memcpy(dst, sector_buffer + off, rl);	

		offset += rl;
		len -= rl;
		dst += rl;
	}
}

void wkfInit() {

	//for (i=0; i<16; i++)	serial[i]=spi_read_uc(0x1f0000+i);
	//serial[16] = 0;
   
	unsigned int special_3 = wkfReadSpecial3();
	print_gecko("GOT Special_3: %08X", special_3);

	// New DVD (aka put it back to WKF mode)
	__wkfReset();
	
	// one chunk at 0, offset 0
	wkfWriteRam(0, 0x0000);
	wkfWriteRam(2, 0x0000);
	// last chunk sig
	wkfWriteRam(4, 0xFFFF);
	wkfWriteOffset(0);
	
	unsigned int switches = wkfReadSwitches();
	print_gecko("GOT Switches: %08X", switches);

	// SD card detect
	if ((wkfGetSlotStatus() & 0x000F0000)==0x00070000) {
		DrawFrameStart();
		DrawMessageBox(D_INFO,"No SD Card");
		DrawFrameFinish();
		sleep(3);
		// no SD card
		wkfInitialized = 0;
	}
	else {
		// If there's an SD, reset DVD (why?)
		__wkfReset();
		udelay(300000);
					
		// one chunk at 0, offset 0
		wkfWriteRam(0, 0x0000);
		wkfWriteRam(2, 0x0000);
		// last chunk sig
		wkfWriteRam(4, 0xFFFF);
		wkfWriteOffset(0);

		// Read first sector of SD card
		wkfRead(&wkfBuffer[0], 0x200, 0);
		if((wkfBuffer[0x1FF] != 0xAA)) {
			// No FAT!
			DrawFrameStart();
			DrawMessageBox(D_INFO,"No FAT Formatted SD found in Wiikey Fusion!");
			DrawFrameFinish();
			sleep(5);
			wkfInitialized = 0;
		} 
		else {
			wkfInitialized = 1;
		}
	}
}

// Wrapper to read a number of sectors
// 0 on Success, -1 on Error
int wkfReadSectors(int chn, u32 sector, unsigned int numSectors, unsigned char *dest) 
{
	wkfRead(dest, numSectors * 512, sector * 512);
	return 0;
}

// Is an SD Card inserted into the Wiikey Fusion?
bool wkfIsInserted(int chn) {
	if(!wkfInitialized) {
		wkfInit();
		if(!wkfInitialized) {
			return false;
		}
	}
	return true;
}

int wkfShutdown(int chn) {
	return 1;
}

static bool __wkf_startup(void)
{
	return wkfIsInserted(0);
}

static bool __wkf_isInserted(void)
{
	return wkfIsInserted(0);
}

static bool __wkf_readSectors(u32 sector, u32 numSectors, void *buffer)
{
	wkfReadSectors(0, sector, numSectors, buffer);
	return true;
}

static bool __wkf_writeSectors(u32 sector, u32 numSectors, void *buffer)
{
	return false;
}

static bool __wkf_clearStatus(void)
{
	return true;
}

static bool __wkf_shutdown(void)
{
	return true;
}


const DISC_INTERFACE __io_wkf = {
	DEVICE_TYPE_GC_WKF,
	FEATURE_MEDIUM_CANREAD | FEATURE_GAMECUBE_DVD,
	(FN_MEDIUM_STARTUP)&__wkf_startup,
	(FN_MEDIUM_ISINSERTED)&__wkf_isInserted,
	(FN_MEDIUM_READSECTORS)&__wkf_readSectors,
	(FN_MEDIUM_WRITESECTORS)&__wkf_writeSectors,
	(FN_MEDIUM_CLEARSTATUS)&__wkf_clearStatus,
	(FN_MEDIUM_SHUTDOWN)&__wkf_shutdown
} ;