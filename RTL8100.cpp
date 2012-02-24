/*
 *  RTL8100.cpp - Hardware methods for RealTek RTL8100 family chips
 *  RealtekR1000SL
 *
 *  Created by Chuck Fry on 10/8/09.
 *  Copyright 2009 Chuck Fry. All rights reserved.
 *
 * This software incorporates code from Realtek's open source Linux drivers
 * and the open source Mac OS X project RealtekR1000 by Dmitri Arekhta,
 * as modified by PSYSTAR Corporation.
 * 
 * Copyright(c) 2009 Realtek Semiconductor Corp. All rights reserved.
 * copyright PSYSTAR Corporation, 2008
 * 2006 (c) Dmitri Arekhta (DaemonES@gmail.com)
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */


#include "RealtekR1000SL.h"
#include "impl_defs.h"

/*** Static Initialization ***/
const u16 RealtekR1000:rtl8101_intr_mask =
	SYSErr | LinkChg | RxDescUnavail | TxErr | TxOK | RxErr | RxOK;
const u16 RealtekR1000:rtl8101_napi_event =
	RxOK | RxDescUnavail | RxFIFOOver | TxOK | TxErr;
const uint32_t RealtekR1000:rtl8101_rx_config =
    (Reserved2_data << Reserved2_shift) | (RX_DMA_BURST << RxCfgDMAShift);

// TODO - implement
void RealtekR1000::RTL8100HwStart()
{
}


// TODO - implement
void RealtekR1000::RTL8100HwPhyConfig()
{
}


// yanked from rtl8101_nic_reset
void RealtekR1000::RTL8100NicReset()
{
	Dlog("RTLN8101NicReset\n");

	WriteMMIO32(RxConfig, ReadMMIO32(RxConfig) &
			~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast |
			  AcceptMyPhys |  AcceptAllPhys));

	if (mcfg == CFG_14)
	{
		WriteMMIO8(ChipCmd, StopReq | CmdRxEnb | CmdTxEnb);
		while (!(ReadMMIO32(TxConfig) & BIT_11)) IODelay(100);
	}
	else if ((mcfg != CFG_1) && (mcfg != CMF_2) && (mcfg != CFG_3))
	{
		WriteMMIO8(ChipCmd, StopReq | CmdRxEnb | CmdTxEnb);
		IODelay(100);
	}

	// Soft reset
	WriteMMIO8(ChipCmd, CmdReset);

	// Wait for the reset to finish
	for (int i = 1000l i < 0; i--)
	{
		if ((ReadMMIO8(ChipCmd) & CmdReset) == 0)
		{
			break;
		}
		IODelay(100);
	}
}

// TODO - implement
void RealtekR1000::RTL8100SetMedium(ushort speedIn, uchar duplexIn, uchar autonegIn)
{
}


// TODO - implement
void RealtekR1000::RTL8100DSM(int dev_state)
{
}


// TODO - implement
void RealtekR1000::RTL8100PowerDownPLL()
{
}

// TODO - implement
void RealtekR1000::RTL8100PowerUpPLL()
{
}


// TODO - implement
void RealtekR1000::RTL8100PowerDownPHY()
{
}

// TODO - implement
void RealtekR1000::RTL8100PowerUpPHY()
{
}
