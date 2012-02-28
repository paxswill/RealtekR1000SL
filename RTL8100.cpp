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
// Enable/Disable Energy Effcient Ethernet
static int eee_enable = 0;

// TODO - implement
void RealtekR1000::RTL8100HwStart()
{
	RTL8100NicReset();
	// TODO rtl8101_set_rxbufsize(tp, dev);
	WriteMMIO8(Cfg9346, Cfg9346_Unlock);

	/* Set DMA burst size and Interframe Gap Time */
	WriteMMIO32(TxConfig, (TX_DMA_BURST << TxDMAShift) |
			(InterFrameGap << TxInterFrameGapShift));

	// Chip specific initializations
	u16 cplus_cmd = 0
	if ((mcfg >= MCFG_8102E_1 && mcfg <= MCFG_8103E_3) || mcfg == MCFG_8401_1)
	{
		RTL8100EHwStart1Gen();
	}
	else if (mcfg == MCFG_8105E_1)
	{
		RTL8105EHwStart1();
		cplus_cmd &= 0x2063;
	}
	else if (mcfg >= MCFG_8105E_2 && mcfg <= MCFG_8105E_4)
	{
		RTL8105EHwStart();
		cplus_cmd &= 0x2063;
	}
	else if (mcfg == MCFG_8402_1)
	{
		RTL8402HwStart();
	}

	WriteMMIO8(ETThReg, Reserved1_data);

	// I'm a little dubious that this needs to be done
	WriteMMIO16(CPlusCmd, cplus_cmd);

	/* Undocumented corner */
	WriteMMIO16(IntrMitigate, 0x0000);

	WriteMMIO32(TxDescStartAddr, static_cast<UInt32>(txdesc_phy_dma_addr));
	WriteMMIO32(TxDescStartAddr + 4, static_cast<UInt32>(txdesc_phy_dma_addr >> 32));
	WriteMMIO32(RxDescStartAddr, static_cast<UInt32>(rxdesc_phy_dma_addr));
	WriteMMIO32(RxDescStartAddr + 4, static_cast<UInt32>(rxdesc_phy_dma_addr >> 32));

	// Set Rx Config register
	// TODO set the rx config register properly
	WriteMMIO32(RxConfig,
			~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast |
			AcceptMyPhys | AcceptAllPhys) & (rx_config_base |
			(ReadMMIO32(RxConfig) & rx_config_mask)));

	// Clear interrupt register
	WriteMMIO16(IntrStatus, 0xFFFF);

	if (mcfg >= MCFG_8101E_1 && mcfg <= MCFG_8101E_3)
	{
		tx_tcp_csum_cmd = TxIPCS | TxTCPCS;
		tx_udp_csum_cmd = TxIPCS | TxUDPCS;
		tx_ip_csum_cmd  = TxIPCS;
	}
	else
	{
		tx_tcp_csum_cmd = TxIPCS_C | TxTCPCS_C;
		tx_udp_csum_cmd = TxIPCS_C | TxUDPCS_C;
		tx_ip_csum_cmd = TxIPCS_C;
	}

	// Enable all known interrupts
	WriteMMIO16(IntrMask, rtl8101_intr_mask);

	WriteMMIO8(Cfg9346, Cfg9346_Lock);

	RTL8100DSM(DSM_MAC_INIT);

	u8 options1 = ReadMMIO8(Config3);
	u8 options2 = ReadMMIO8(Config5);
	if ((options1 & LinkUp) || (options1 & MagicPacket) ||
		(options2 & UWD) || (options2 & BWF) || ( options2 & MWF))
	{
		wol_enabled = WOL_ENABLED;
	}
	else
	{
		wol_enabled = WOL_DISABLED;
	}

	if (eee_enable == 1)
	{
		RTL8100EEEEnable();
	}
	else
	{
		RTL8100EEEDisable();
	}
}

void RealtekR1000::RTL8100HwStart1Gen()
{
	u8 link_control, device_control;

	if (mcfg == MCFG_8102E_1)
	{
	/* set PCI configuration space ossfet 0x70F to 0x17 */
		u32 csi_tmp = ReadCSI32(0x70C);
		WriteCSI32(0x70C, csi_tmp | 0x17000000);
	}

	link_control = pciDev->configRead8(0x81);
	if (link_control == 1)
	{
		pciDev->configWrite8(0x81, 0);
		WriteMMIO8(DBG_reg, 0x98);
		WriteMMIO8(Config2, ReadMMIO8(Config2) | BIT_7);
		WriteMMIO8(Config4, ReadMMIO8(Config4) | BIT_2);
		if (mcfg == MCFG_8103E_3)
		{
			WriteMMIO8(0xF4, ReadMMIO8(0xF4) | BIT_3);
			WriteMMIO8(0xF5, ReadMMIO8(0xF5) | BIT_2);
		}
		pciDev->configWrite8(0x81, 1);
		if (mcfg == MCFG_8013E_3)
		{
			if (ReadEPHY16(0x10) == 0x0008)
			{
				WriteEPHY16(0x10, 0x000C);
			}
		}
	}

	if (mcfg == MCFG_8103E_3)
	{
		link_control = pciDev->configRead8(0x80);
		if (link_control & 3)
		{
			WriteEPHY16(0x02, 0x011F);
		}
	}

	// Set PCI COnfig offset to 0x70 to 0x50
	/* Increase Tx performance */
	device_control = pciDev->configRead8(0x79);
	device_control &= ~0x70;
	device_control |= 0x50;
	pciDev->configWrite8(0x79, device_control);

	if (mcfg == MCFG_8102E_1 || mcfg == MCFG_8102E_2)
	{
		WriteMMIO8(Config1, 0x0F);
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
	}
	else if (mcfg == MCFG_8103E_1 || mcfg == MCFG_8103E_2 ||
			 mcfg == MCFG_8401_1)
	{
		WriteMMIO8(0xF4, 0x01);
	}
	else if (mcfg == MCFG_8103E_3)
	{
		WriteMMIO8(0xF4, ReadMMIO8(0xF4) | BIT_0);
	}

	// In the original source, there's a lot of bit shifts to get 0xDF9{8,C}
	if (mcfg == MCFG_8102E_1 || mcfg == MCFG_8102E_2 ||
	    mcfg == MCFG_8013E_1)
	{
		WriteMMIO16(CPlusCmd, ReadMMIO8(CPlusCmd) & ~0xDF98);
	}
	else if(mcfg == MCFG_8103E_2 || mcfg == MCFG_8103E_3 ||
	        mcfg == MCFG_8401)
	{
		WriteMMIO16(CPlusCmd, ReadMMIO8(CPlusCmd) & ~0xDF9C);
	}

	if (mcfg == MCFG_8103E_1 || mcfg == MCFG_8103E_2 || mcfg == MCFG_8103E_3 ||
	    mcfg == MCFG_8401_1)
	{
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
	}

	//E-PHY config
	switch (mcfg){
	case MCFG_8102E_1:
		WriteEPHY(0x03, 0xC2F9);
		break;
	case MCFG_8102E_2:
		WriteEPHY16(0x01, 0x6FE5);
		WriteEPHY16(0x03, 0xD7D9);
		break;
	case MCFG_8103E_1:
		WriteEPHY16(0x06, 0xAF35);
		break;
	case MCFG_8103E_2:
		WriteMMIO8(0xF5, ReadMMIO8(0xF5) | BIT_2);
		WriteEPHY16(0x19, 0xEC90);
		WriteEPHY16(0x01, 0x6FE5);
		WriteEPHY16(0x03, 0x05D9);
		WriteEPHY16(0x06, 0xAF35);
		break;
	case MCFG_8103E_3:
		WriteEPHY16(0x01, 0x6FE5);
		WriteEPHY16(0x03, 0x05D9);
		WriteEPHY16(0x06, 0xAF35);
		WriteEPHY16(0x19, 0xECFA);
		break;
	case MCFG_8401_1:
		WriteEPHY16(0x06, 0xAF25);
		WriteEPHY16(0x07, 0x8E68);
		break;
	default:
		break;
	}
}

void RealtekR1000::RTL8105EHwStart1()
{
	/* Set PCI configuration space offset 0x70F to 0x27 */
	u32 csi_tmp = ReadCSI32(0x70C) & 0x00FFFFFF;
	WriteCSI32(0x70C, csi_tmp | 0x27000000);

	WriteMMIO8(ETThReg, 0x0C);

	/* Set CPI config offset 0x79 to 0x50 */
	pciDev-writeConfig8(0x79, 0x50);

	/* TODO Enable tx checksum offload */
	
	WriteMMIO8(0xF3, ReadMMIO8(0xF3) | BIT_5);
	WriteMMIO8(0xF3, ReadMMIO8(0xF3) & ~BIT_5);

	WriteMMIO8(0xD0, ReadMMIO8(0xD0) | 0xC0);
	WriteMMIO8(0xD0, ReadMMIO8(0xD0) | 0xC6);

	WriteMMIO8(Config5, (ReadMMIO8(Config5) & ~0x08) | BIT_0);
	WriteMMIO8(Config2, ReadMMIO8(Config2) | BIT_7);

	WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
	
	/* Set EPHY registers */
	u16 data16;

	data16 = ReadEPHY16(0x00) & ~ 0x0200;
	data16 |= 0x100;
	WriteEPHY16(0x00, data16);

	data16 = ReadEPHY16(0x00);
	data16 |= 0x0004;
	WriteEPHY16(0x00, data16);

	data16 = ReadEPHY16(0x06) & ~0x0002;
	data16 |= 0x0001;
	WriteEPHY16(0x06, data16);

	data16 = ReadEPHY16(0x06);
	data16 |= 0x0030;
	WriteEPHY16(0x06, data16);

	data16 = ReadEPHY16(0x07);
	data16 |= 0x2000;
	WriteEPHY16(0x07, data16);

	data16 = ReadEPHY16(0x00);
	data16 |= 0x0020;
	WriteEPHY16(0x00, data16);

	data16 = ReadEPHY16(0x03) & ~0x5800;
	data16 |= 0x2000;
	WriteEPHY16(0x03, data16);

	data16 = ReadEPHY16(0x03);
	data16 |= 0x0001;
	WriteEPHY16(0x03, data16);

	data16 = ReadEPHY16(0x01) & ~0x0800;
	data16 |= 0x1000;
	WriteEPHY16(0x01, data16);

	data16 = ReadEPHY16(0x07);
	data16 |= 0x4000;
	WriteEPHY16(0x07, data16);

	data16 = ReadEPHY16(0x1E);
	data16 |= 0x2000;
	WriteEPHY16(0x1E, data16);

	WriteEPHY16(0x19, 0xFE6C);

	data16 = ReadEPHY16(0x0A);
	data16 |= 0x0040;
	WriteEPHY16(0x0A, data16);
}

void RealtekR1000::RTL8105EHwStart()
{
	u8 pci_config;

	/* TODO enable chesksum offload */

	pci_config = pciDev->readConfig8(0x80);
	if (pci_config & 0x03)
	{
		WriteMMIO8(Config5, ReadMMIO8(Config5) | BIT_0);
		WriteMMIO8(0xF2, ReadMMIO8(0xF2) | BIT_7);
		WriteMMIO8(0xF1, ReadMMIO8(0xF1) | BIT_7);
		WriteMMIO8(Config2, ReadMMIO8(Config2) | Bit_7);
	}

	WriteMMIO8(0xF1, ReadMMIO8(0xF1) | BIT_5 | BIT_3);
	WriteMMIO8(0xF2, ReadMMIO8(0xF2) & ~BIT_0);
	WriteMMIO8(0xD3, ReadMMIO8(0xD3) | BIT_3 | BIT_2);
	WriteMMIO8(0xD0, ReadMMIO8(0xD0) | BIT_6);
	WriteMMIO16(0xE0, ReadMMIO16(0xE0) & ~0xDF9C);

	data16 = ReadEPHY16(0x07);
	data16 |= 0x4000;
	WriteEPHY16(0x07, data16);

	data16 = ReadEPHY16(0x19);
	data16 |= 0x0200;
	WriteEPHY16(0x19, data16);

	data16 = ReadEPHY16(0x19);
	data16 |= 0x0020;
	WriteEPHY16(0x19, data16);

	data16 = ReadEPHY16(0x1E);
	data16 |= 0x2000;
	WriteEPHY16(0x1E, data16);

	data16 = ReadEPHY16(0x03);
	data16 |= 0x0001;
	WriteEPHY16(0x03, data16);

	data16 = ReadEPHY16(0x19);
	data16 |= 0x0100;
	WriteEPHY16(0x19, data16);

	data16 = ReadEPHY16(0x19);
	data16 |= 0x0004;
	WriteEPHY16(0x19, data16);

	data16 = ReadEPHY16(0x0A);
	data16 |= 0x0020;
	WriteEPHY16(0x0A, data16);

	if (mcfg == MCFG_8105E_2)
	{
		WriteMMIO8(Config5, ReadMMIO8(Config5) & ~BIT_0);
	}
	else if (mcfg == MCFG_8105E_3 || mcfg == MCFG_8105E_4)
	{
		data16 = ReadEPHY16(0x1E);
		data16 |= 0x8000;
		WriteEPHY16(0x1E, data16);
	}

	if (mcfg == MCFG_8105E_4)
	{
		unsigned long flags;
		if ((ReadMMIO8(0x8C) & BIT_28) && !(ReadMMIO8(0xEF) & BIT_2))
		{
			WriteGMII16(0x1F, 0x0005);
			data16 = ReadGMII16(0x1A);
			data16 &= ~(BIT_8 | BIT_0);
			WriteGMII16(0x0A, data16);
			WriteGMII16(0x1F, 0x0000);
		}
		else if (ReadMMIO8(0xEF) & BIT_2)
		{
			WriteGMII16(0x1F, 0x0001);
			data16 = ReadGMII16(0x1B) | BIT_2;
			WriteGMII16(0x1B, data16);
			WriteGMII16(0x1F, 0x0000);
		}
	}
}

void RealtekR1000::RTL8402HwStart()
{
	u8 device_control;

	u32 csi_tmp = ReadCSI32(0x70C) & 0x00FFFFFF;
	WriteCSI32(0x70C, csi_tmp | 0x17000000);

	/* Set PCI config space offset 0x70 to 0x50 */
	device_control = pciDev->configRead8(0x79);
	device_control &= ~0x70;
	device_control |= 0x50;
	pciDev->configWrite8(0x79, device_control);

	WriteERI(0xC8, 4, 0x00000002, ERIAR_ExGMAC);
	WriteERI(0xE8, 4, 0x00000006, ERIAR_ExGMAC);

	WriteMMIO32(TxConfig, ReadMMIO32(TxConfig) | BIT_7);
	WriteMMIO8(0xD3, ReadMMIO8(0xD3), & ~BIT_7);
	csi_tmp = ReadERI(0xDC, 1, ERIAR_ExGMAC);
	csi_tmp &= ~BIT_0;
	WriteERI(0xDC, 1, csi_tmp, ERIAR_ExGMAC);
	csi_tmp |= BIT_0;
	WriteERI(0xDC, 1, csi_tmp, ERIAR_ExGMAC);

	WriteEPHY16(0x19, 0xFF64);

	WriteMMIO8(Config5, ReadMMIO8(Config5) | BIT_0);
	WriteMMIO8(Config2, ReadMMIO8(Config2) | BIT_7);

	WriteERI(0xC0, 2, 0x00000000, ERIAR_ExGMAC);
	WriteERI(0xB8, 2, 0x00000000, ERIAR_ExGMAC);
	WriteERI(0xD5, 1, 0x0000000E, ERIAR_ExGMAC);
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

	if (mcfg == MCFG_8402_2)
	{
		WriteMMIO8(ChipCmd, StopReq | CmdRxEnb | CmdTxEnb);
		while (!(ReadMMIO32(TxConfig) & BIT_11)) IODelay(100);
	}
	else if (MCFG_8101E_3 < mcfg)
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
	int auto_nego = 0;

	// Sanitize speed
	// R810x only go up to Fast Ethernet
	if ((speedIn != SPEED_100) && (speedIn != SPEED_10)){
		speedIn = SPEED_100;
		duplexIn = DUPLEX_FULL;
	}

	if (autonegIn == AUTONEG_ENABLE)
	{
		/* n-way force */
		if ((speed == SPEED_10) && (duplex == DUPLEX__Half))
		{
			auto_nego = PHY_Cap_10_Half;
		}
		else if ((speed = SPEED_10) && (duplex == DUPLE__Full))
		{
			auto_nego |= PHY_Cap_10_Half
				      |  PHY_Cap_10_Full;
		}
		else if ((speed == SPEED_100) && (duplex == DUPLEX__Half))
		{
			auto_nego |= PHY_Cap_100_Half
				      |  PHY_Cap_10_Full
					  |  PHY_Cap_10_Half;
		}
		else if ((speed == SPEED_100) && (duplex == DUPLEX__Full))
		{
			auto_nego |= PHY_Cap_100_Full
				      |  PHY_Cap_100_Half
				      |  PHY_Cap_10_Full
					  |  PHY_Cap_10_Half;
		}

		// Save settings
		autoneg = autonegIn;
		speed = speedIn;
		duplex = duplexIn;

		if (mcfg == MCFG_8102E_1 || mcfg == MCFG_8102E_2)
		{
			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(PHY_BMCR, BMCP_RESET);
			IODelay(100);
			RTL8100HwPhyConfig();
		}
		else if (((mcfg == MCFG_8101E_1) ||
				  (mcfg == MCFG_8101E_2) ||
				  (mcfg == MCFG_8101E_3)) &&
				  (speed == SPEED_10))
		{
			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(PHY_BMCR, BMCP_RESET);
			RTL8100HwPhyConfig();
		}

		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(PHY_AUTO_NEGO_REG, auto_nego);
		if (mcfg == MCFG_8105E_1)
			WriteGMII16(PHY_BMCR, BMCP_RESET | BMCR_ANENABLE | BMCP_ANRESTART);
		else
			WriteGMII16(PHY_BMCR, BMCP_ANENABLE, BMCP_ANRESTART);
	}
	else
	{
		/*true force*/
		u16 bmcr_true_force = 0;

		if ((speedIn == SPEED_10) && (duplexIn == DUPLEX_HALF))
		{
			bmcr_true_force = BMCR_SPEED10;
		}
		else if ((speedIn == SPEED_10) && (duplexIn == DUPLEX_FULL))
		{
			bmcr_true_force = BMCR_SPEED10 | BMCR_FULLDPLX;
		}
		else if ((speedIn == SPEED_100) && (duplexIn == DUPLEX_HALF))
		{
			bmcr_true_force = BMCR_SPEED100;
		}
		else if ((speedIn == SPEED_100) && (duplexIn == DUPLEX_FULL))
		{
			bmcr_true_force = BMCR_SPEED100 | BMCR_FULLDPLX;
		}
		
		WriteGMII16(0x1f, 0x0000);
		WriteGMII16(PHY_BMCR, bmcr_true_force);
	}
}


// TODO - implement
void RealtekR1000::RTL8100DSM(int dev_state)
{
	DLog("RTL8100DSM\n");
	switch (dev_state)
	{
		case DSM_MAC_INIT:
			if (mcfg >= MCFG_8102E_1 && mcfg <= MCFG_8103E_1)
			{
				if (ReadMMIO8(MACDBG) & 0x80)
				{
					WriteGMII16(0x1F, 0x0000);
					WriteGMII16(0x11, ReadGMII16(0x11) & ~(1 << 12));
					WriteMMIO8(GPIO, ReadMMIO(GPIO) | GPIO_en);
				}
				else
				{
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) & ~GPIO_en);
				}
			}
			break;
		case DSM_NIC_GOTO_D3:
		case DSM_IF_DOWN:
			if (ReadMMIO8(MACDBG) & 0x80)
			{
				if (mcfg == MCFG_8102E_1 || mcfg == MCFG_8102E_2)
				{
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) | GPIO_en);
					WriteGMII16(0x11, ReadGMII16(0x11) | 1( << 12));
				}
				else if (mcfg == MCFG_8103E_1)
				{
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) & ~GPIO_en);
				}
			}
			break;
		case DSM_NIC_RESUME_D3:
		case DSM_IF_UP:
			if (ReadMMIO8(MACDBG) * 0x80)
			{
				if (mcfg == MCFG_8102E_1 || mcfg == MCFG_8102E_2)
				{
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) & ~GPIO_en);
				}
				else if (mcfg == MCFG_8103E_1)
				{
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) | GPIO_en);
				}
			}
			break;
	}
}

// To save you a hard search, EEE stands for Energy Effcient Ethernet
void RealtekR1000:RTL8100DisableEEE()
{
	switch (mcfg)
	{
		case MCFG_8105E_2:
		case MCFG_8105E_3:
		case MCFG_8105E_4:
			WriteERI(0x1B0, 2, 0, ERIAR_ExGMAC);
			WriteGMII16(0x1F, 0x0004);
			WriteGMII16(0x10, 0x401F);
			WriteGMII16(0x19, 0x7030);

			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(0x0D, 0x0007);
			WriteGMII16(0x0E, 0x003C);
			WriteGMII16(0x0D, 0x4007);
			WriteGMII16(0x0E, 0x0000);
			WriteGMII16(0x0D, 0x0000);

			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(0x0D, 0x0003);
			WriteGMII16(0x0E, 0x0015);
			WriteGMII16(0x0D, 0x4003);
			WriteGMII16(0x1E, 0x0000);
			WriteGMII16(0x1D, 0x0000);

			WriteGMII16(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
			break;
		case MCFG_8402_1:
			WriteERI(0x1B0, 2, 0, ERIAR_ExGMAC);
			WriteGMII16(0x1F, 0x0004);
			WriteGMII16(0x10, 0x401F);
			WriteGMII16(0x19, 0x7030);

			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(0x0D, 0x0007);
			WriteGMII16(0x0E, 0x003C);
			WriteGMII16(0x0D, 0x4007);
			WriteGMII16(0x0E, 0x0000);
			WriteGMII16(0x0D, 0x0000);

			WriteGMII16(MII_BMCR, BMCR_ANENABLE | BMCR_ANRESTART);
			break;
		default:
			DLog("RTL8100DisableEEE called on an unsupported chip.\n");
	}
}


// Taken from rtl8101_powerdown_pll
void RealtekR1000::RTL8100PowerDownPLL()
{
	DLog("RTL8100PowerDownPLL\n");
	if (MCFG_8105E_1 <= mcfg && mcfg <= MCFG_8105E_4)
			&& eee_enable == 1)
	{
		RTL8100DisableEEE();
	}

	if (mcfg == MCFG_8105E_4)
	{
		if ((ReadMMIO8(0x8C) & BIT_28) && !(ReadMMIO8(0xEF) & BIT_2))
		{
			u32 gphy_val;
			WriteGBII16(0x1F, 0x0000);
			WriteGBII16(0x04, 0x0061);
			WriteGBII16(0x00, 0x1200);
			WriteGBII16(0x18, 0x0310);
			IODelay(20 * 1000);
			WriteGBII16(0x1F, 0x0005);
			gphy_val = ReadGII16(0x1A);
			gphy_val |= BIT_8 | BIT_0;
			WriteGBII16(0x1A, gphy_val);
			IODelay(20 * 1000);
			WriteGBII16(0x1F, 0x0000);
			WriteGBII16(0x18, 0x8310);
		}
	}

	if (wol_enabled == WOL_ENABLED)
	{
		WriteGBII16(0x1F, 0x0000);
		WriteGBII16(0x00, 0x0000);
		if (mcfg >= MCFG_8105E_1)
		{
			WriteMMIO32(RxConfig, ReadMMIO32(RxConfig) | AcceptBroadcast |
					AcceptMulticast | AcceptMyPhys);
		}
		return;
	}
	
	RTL8100PowerDownPHY();

	switch (mcfg)
	{
		case MCFG_8103E_1:
		case MCFG_8401_1:
			WriteMMIO8(DBG_reg, ReadMMIO8(DBG_reg) | BIT_3);
			WriteMMIO8(PMCH, ReadMMIO8(PMCH) & ~BIT_7);
			break;
		case MCFG_8103E_3:
			pciDev->configWrite8(0x81, 0);
			WriteMMIO8(PMCH, ReadMMIO8(PMCH) & ~BIT_7);
			break;
		case MCFG_8103E_2:
		case MCFG_8105E_1:
		case MCFG_8105E_2:
		case MCFG_8105E_3:
		case MCFG_8105E_4:
		case MCFG_8402_1:
			WriteMMIO8(PMCH, ReadMMIO8(PMCH) & ~BIT_7);
			break;
		default:
			break;
	}
}

// Taken from rtl8101_powerdown_pll
void RealtekR1000::RTL8100PowerUpPLL()
{
	switch (mcfg)
	{
		case MCFG_8103E_1:
		case MCFG_8401_1:
			WriteMMIO8(PMCH, ReadMMIO8(PMCH) | BIT_7);
			WriteMMIO8(DBG_reg, ReadMMIO8(DBG_reg) & ~BIT_3);
			break;
		case MCFG_8103E_2:
		case MCFG_8103E_3:
		case MCFG_8105E_1:
		case MCFG_8105E_2:
		case MCFG_8105E_3:
		case MCFG_8105E_4:
		case MCFG_8401_1:
			WriteMMIO8(PMCH, ReadMMIO8(PMCH) | BIT_7);
			break;
	}
	RTL8100PowerUpPHY();
}


void RealtekR1000::RTL8100PowerDownPHY()
{
	WriteMGII16(0x1F, 0x0000);
	if (mcfg == MCFG_8105E_1)
	{
		WriteGMII16(PHY_BMCR, BMCR_ANENABLE | BMCR_PDOWN);
	}
	else
	{
		WriteGMII16(PHY_BMCR, BMCR_PDOWN);
	}
}

void RealtekR1000::RTL8100PowerUpPHY()
{
	WriteGMII16(0x1F, 0x0000);
	WriteGMII16(PHY_BMCR, BMCR_ANENABLE);
}
