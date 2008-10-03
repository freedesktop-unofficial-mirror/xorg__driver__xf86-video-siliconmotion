/* Header:   //Mercury/Projects/archives/XFree86/4.0/smi_accel.c-arc   1.16   03 Jan 2001 13:29:06   Frido  $ */

/*
Copyright (C) 1994-1999 The XFree86 Project, Inc.  All Rights Reserved.
Copyright (C) 2000 Silicon Motion, Inc.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the names of the XFree86 Project and
Silicon Motion shall not be used in advertising or otherwise to promote the
sale, use or other dealings in this Software without prior written
authorization from the XFree86 Project and silicon Motion.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "smi.h"
#include "smi_501.h"

void
SMI_GEReset(ScrnInfoPtr pScrn, int from_timeout, int line, char *file)
{
    SMIPtr pSmi = SMIPTR(pScrn);
    unsigned int	iTempVal;
    CARD8 tmp;

    ENTER();

    if (from_timeout) {
	if (pSmi->GEResetCnt++ < 10 || xf86GetVerbosity() > 1) {
	    xf86DrvMsg(pScrn->scrnIndex,X_INFO,"\tSMI_GEReset called from %s line %d\n", file, line);
	}
    } else {
	WaitIdleEmpty();
    }

    if (IS_MSOC(pSmi)) {
	iTempVal = READ_SCR(pSmi, 0x0000) & ~0x00003000;
	WRITE_SCR(pSmi, 0x0000, iTempVal | 0x00003000);
	WRITE_SCR(pSmi, 0x0000, iTempVal);
    }
    else {
	tmp = VGAIN8_INDEX(pSmi, VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x15);
	VGAOUT8_INDEX(pSmi, VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x15, tmp | 0x30);
    }

    WaitIdleEmpty();

    if (!IS_MSOC(pSmi))
	VGAOUT8_INDEX(pSmi, VGA_SEQ_INDEX, VGA_SEQ_DATA, 0x15, tmp);
    SMI_EngineReset(pScrn);

    LEAVE();
}

/* The sync function for the GE */
void
SMI_AccelSync(ScrnInfoPtr pScrn)
{
    SMIPtr pSmi = SMIPTR(pScrn);

    ENTER();

    WaitIdleEmpty(); /* #161 */
    if (IS_MSOC(pSmi)) {
	int	i, status;

	for (i = 0x1000000; i > 0; i--) {
	    status = READ_SCR(pSmi, CMD_STATUS);
	    /* bit 0:	2d engine idle if *not set*
	     * bit 1:	2d fifo empty if *set*
	     * bit 2:	2d setup idle if if *not set*
	     * bit 18:  color conversion idle if *not set*
	     * bit 19:  command fifo empty if *set*
	     * bit 20:  2d memory fifo empty idle if *set*
	     */
	    if ((status & 0x1C0007) == 0x180002)
		break;
	}
    }

    LEAVE();
}

void
SMI_EngineReset(ScrnInfoPtr pScrn)
{
    SMIPtr pSmi = SMIPTR(pScrn);
    CARD32 DEDataFormat = 0;
    int i;
    int xyAddress[] = { 320, 400, 512, 640, 800, 1024, 1280, 1600, 2048 };

    ENTER();

    pSmi->Stride = ((pSmi->width * pSmi->Bpp + 15) & ~15) / pSmi->Bpp;
    if(pScrn->bitsPerPixel==24)
       pSmi->Stride *= 3;
    DEDataFormat = SMI_DEDataFormat(pScrn->bitsPerPixel);

    for (i = 0; i < sizeof(xyAddress) / sizeof(xyAddress[0]); i++) {
	if (pSmi->rotate) {
	    if (xyAddress[i] == pSmi->height) {
		DEDataFormat |= i << 16;
		break;
	    }
	} else {
	    if (xyAddress[i] == pSmi->width) {
		DEDataFormat |= i << 16;
		break;
	    }
	}
    }

    WaitIdleEmpty();
    WRITE_DPR(pSmi, 0x10, (pSmi->Stride << 16) | pSmi->Stride);
    WRITE_DPR(pSmi, 0x1C, DEDataFormat);
    WRITE_DPR(pSmi, 0x24, 0xFFFFFFFF);
    WRITE_DPR(pSmi, 0x28, 0xFFFFFFFF);
    WRITE_DPR(pSmi, 0x3C, (pSmi->Stride << 16) | pSmi->Stride);
    if(pSmi->shadowFB){
       WRITE_DPR(pSmi, 0x40, 0);
       WRITE_DPR(pSmi, 0x44, 0);  /* The shadow framebuffer is located at offset 0 */
    }else{
       WRITE_DPR(pSmi, 0x40, pSmi->FBOffset >> 3);
       WRITE_DPR(pSmi, 0x44, pSmi->FBOffset >> 3);
    }
    CHECK_SECONDARY(pSmi);

    SMI_DisableClipping(pScrn);

    LEAVE();
}

/******************************************************************************/
/*  Clipping								      */
/******************************************************************************/

void
SMI_SetClippingRectangle(ScrnInfoPtr pScrn, int left, int top, int right,
			 int bottom)
{
    SMIPtr pSmi = SMIPTR(pScrn);

    ENTER();
    DEBUG("left=%d top=%d right=%d bottom=%d\n", left, top, right, bottom);

    /* CZ 26.10.2001: this code prevents offscreen pixmaps being drawn ???
	left   = max(left, 0);
	top    = max(top, 0);
	right  = min(right, pSmi->width);
	bottom = min(bottom, pSmi->height);
    */

    if (pScrn->bitsPerPixel == 24) {
	left  *= 3;
	right *= 3;

	if (pSmi->Chipset == SMI_LYNX) {
	    top    *= 3;
	    bottom *= 3;
	}
    }

    if (IS_MSOC(pSmi)) {
	++right;
	++bottom;
    }

    pSmi->ScissorsLeft = (top << 16) | (left & 0xFFFF) | 0x2000;
    pSmi->ScissorsRight = (bottom << 16) | (right & 0xFFFF);

    pSmi->ClipTurnedOn = FALSE;

    WaitQueue(2);
    WRITE_DPR(pSmi, 0x2C, pSmi->ScissorsLeft);
    WRITE_DPR(pSmi, 0x30, pSmi->ScissorsRight);

    LEAVE();
}

void
SMI_DisableClipping(ScrnInfoPtr pScrn)
{
    SMIPtr pSmi = SMIPTR(pScrn);

    ENTER();

    pSmi->ScissorsLeft = 0;
    if (pScrn->bitsPerPixel == 24) {
	if (pSmi->Chipset == SMI_LYNX) {
	    pSmi->ScissorsRight = ((pSmi->height * 3) << 16) | (pSmi->width * 3);
	} else {
	    pSmi->ScissorsRight = (pSmi->height << 16) | (pSmi->width * 3);
	}
    } else {
	pSmi->ScissorsRight = (pSmi->height << 16) | pSmi->width;
    }

    pSmi->ClipTurnedOn = FALSE;

    WaitQueue(2);
    WRITE_DPR(pSmi, 0x2C, pSmi->ScissorsLeft);
    WRITE_DPR(pSmi, 0x30, pSmi->ScissorsRight);

    LEAVE();
}

CARD32
SMI_DEDataFormat(int bpp) {
    CARD32 DEDataFormat = 0;

    switch (bpp) {
    case 8:
	DEDataFormat = 0x00000000;
	break;
    case 16:
	DEDataFormat = 0x00100000;
	break;
    case 24:
	DEDataFormat = 0x00300000;
	break;
    case 32:
	DEDataFormat = 0x00200000;
	break;
    }
    return DEDataFormat;
}

