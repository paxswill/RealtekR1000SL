/*
 *  RTL8168.cpp - Hardware methods for RealTek RTL8168 family chips
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

// **********************************
//
// Static Data Member Initialization
//
// **********************************

const u16 RealtekR1000::rtl8168_intr_mask = 
	SYSErr | LinkChg | RxDescUnavail | TxErr | TxOK | RxErr | RxOK;
const u16 RealtekR1000::rtl8168_napi_event =
	RxOK | RxDescUnavail | RxFIFOOver | TxOK | TxErr;

// Copied from rtl8168_hw_start()
void RealtekR1000::RTL8168HwStart()
{
	RTL8168NicReset();
	
	WriteMMIO8(Cfg9346, Cfg9346_Unlock);
	WriteMMIO8(ETThReg, ETTh);
	
	cp_cmd |= PktCntrDisable | INTT_1;
	WriteMMIO16(CPlusCmd, cp_cmd);
	
	WriteMMIO16(IntrMitigate, 0x5151);
	
	intr_mask = rtl8168_intr_mask;
	//Work around for RxFIFO overflow
	if (mcfg == MCFG_8168B_1)
	{
		intr_mask |= RxFIFOOver | PCSTimeout;
		intr_mask &= ~RxDescUnavail;
	}
	
	WriteMMIO32(TxDescStartAddr, static_cast<UInt32>(txdesc_phy_dma_addr));
	WriteMMIO32(TxDescStartAddr + 4, static_cast<UInt32>(txdesc_phy_dma_addr >> 32));
	WriteMMIO32(RxDescStartAddr, static_cast<UInt32>(rxdesc_phy_dma_addr));
	WriteMMIO32(RxDescStartAddr + 4, static_cast<UInt32>(rxdesc_phy_dma_addr >> 32));
	
	// Set Rx Config register to ignore all for now
	WriteMMIO32(RxConfig,
				~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast | AcceptMyPhys | AcceptAllPhys) &
				(rx_config_base | (ReadMMIO32(RxConfig) & rx_config_mask)));
	
	/* Set DMA burst size and Interframe Gap Time */
	if (mcfg == MCFG_8168B_1) 
	{
		WriteMMIO32(TxConfig,
					(TX_DMA_BURST_512 << TxDMAShift) | 
					(InterFrameGap << TxInterFrameGapShift));
	} 
	else 
	{
		WriteMMIO32(TxConfig,
					(TX_DMA_BURST_unlimited << TxDMAShift) | 
					(InterFrameGap << TxInterFrameGapShift));
	}
	
	/* Clear the interrupt status register. */
	WriteMMIO16(IntrStatus, 0xFFFF);
	
	if (!rx_fifo_overflow)
	{
		/* Enable all known interrupts by setting the interrupt mask. */
		WriteMMIO16(IntrMask, intr_mask);
	}
	
	// Do model-dependent initialization
	switch (mcfg)
	{
		case MCFG_8168B_1:
		case MCFG_8168B_2:
		case MCFG_8168B_3:
			RTL8168BHwStart2();
			break;
			
		case MCFG_8168C_1:
		case MCFG_8168C_2:
		case MCFG_8168C_3:
			RTL8168CHwStart2();
			break;
			
		case MCFG_8168CP_1:
		case MCFG_8168CP_2:
			RTL8168CPHwStart2();
			break;
			
		case MCFG_8168D_1:
		case MCFG_8168D_2:
			RTL8168DHwStart2();
			break;
			
		case MCFG_8168DP_1:
			RTL8168DPHwStart2();
			break;
			
#ifdef DEBUG			
		default:
			panic("RTL8168HwStart: bad mcfg %d", mcfg);
			break;
#endif			
	}
	
	if ((mcfg == MCFG_8168B_1) ||
		(mcfg == MCFG_8168B_2) ||
		(mcfg == MCFG_8168B_3)) 
	{
		/* csum offload command for RTL8168B/8111B */
		tx_tcp_csum_cmd = TxIPCS | TxTCPCS;
		tx_udp_csum_cmd = TxIPCS | TxUDPCS;
		tx_ip_csum_cmd = TxIPCS;
	}
	else 
	{
		/* csum offload command for RTL8168C/8111C, RTL8168CP/8111CP, RTL8168D/8111D, RTL8168DP/8111DP */
		tx_tcp_csum_cmd = TxIPCS_C | TxTCPCS_C;
		tx_udp_csum_cmd = TxIPCS_C | TxUDPCS_C;
		tx_ip_csum_cmd = TxIPCS_C;
	}
	
	WriteMMIO8(ChipCmd, CmdTxEnb | CmdRxEnb);
	
	// Set Rx Config register
	// Default to accept broadcast & my address, no multicast, no promiscuous
	u32 rx_mode = (rx_config_base |
				   AcceptBroadcast | AcceptMyPhys |
				   (ReadMMIO32(RxConfig) & rtl_chip_info[mcfg].RxConfigMask));
	WriteMMIO32(RxConfig, rx_mode);
	
	WriteMMIO8(Cfg9346, Cfg9346_Lock);
	
	RTL8168DSM(DSM_MAC_INIT);
	
	u8 options1 = ReadMMIO8(Config3);
	u8 options2 = ReadMMIO8(Config5);
	if ((options1 & LinkUp) ||
		(options1 & MagicPacket) || 
		(options2 & UWF) || 
		(options2 & BWF) || 
		(options2 & MWF))
		wol_enabled = WOL_ENABLED;
	else
		wol_enabled = WOL_DISABLED;
	
	IODelay(10);
	return;
}

// taken from rtl8168_hw_start()
void RealtekR1000::RTL8168BHwStart2()
{
	u8 device_control;
	if (mcfg == MCFG_8168B_1)
	{
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		if (curr_mtu_size > ETHERMTU)
		{
			device_control = pciDev->configRead8(0x69);
			device_control &= ~0x70;
			device_control |= 0x28;
			pciDev->configWrite8(0x69, device_control);
		}
		else
		{
			device_control = pciDev->configRead8(0x69);
			device_control &= ~0x70;
			device_control |= 0x58;
			pciDev->configWrite8(0x69, device_control);
		}
	}
	else if (mcfg == MCFG_8168B_2)
	{
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		if (curr_mtu_size > ETHERMTU)
		{
			device_control = pciDev->configRead8(0x69);
			device_control &= ~0x70;
			device_control |= 0x28;
			pciDev->configWrite8(0x69, device_control);
			
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | (1 << 0));
		}
		else
		{
			device_control = pciDev->configRead8(0x69);
			device_control &= ~0x70;
			device_control |= 0x58;
			pciDev->configWrite8(0x69, device_control);
			
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~(1 << 0));
		}
	}
	else if (mcfg == MCFG_8168B_3)
	{
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		if (curr_mtu_size > ETHERMTU)
		{
			device_control = pciDev->configRead8(0x69);
			device_control &= ~0x70;
			device_control |= 0x28;
			pciDev->configWrite8(0x69, device_control);
			
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | (1 << 0));
		}
		else
		{
			device_control = pciDev->configRead8(0x69);
			device_control &= ~0x70;
			device_control |= 0x58;
			pciDev->configWrite8(0x69, device_control);
			
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~(1 << 0));
		}
	}
	return;
}

// taken from rtl8168_hw_start()
void RealtekR1000::RTL8168CHwStart2()
{
	u8 device_control;
	u16 ephy_data;
	u32 csi_tmp;
	if (mcfg == MCFG_8168C_1)
	{
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
		WriteCSI32(0x70c, csi_tmp | 0x27000000);
		
		WriteMMIO8(DBG_reg, (0x0E << 4) | Fix_Nak_1 | Fix_Nak_2);
		
		/*Set EPHY registers	begin*/
		/*Set EPHY register offset 0x02 bit 11 to 0 and bit 12 to 1*/
		ephy_data = ReadEPHY16(0x02);
		ephy_data &= ~(1 << 11);
		ephy_data |= (1 << 12);
		WriteEPHY16(0x02, ephy_data);
		
		/*Set EPHY register offset 0x03 bit 1 to 1*/
		ephy_data = ReadEPHY16(0x03);
		ephy_data |= (1 << 1);
		WriteEPHY16(0x03, ephy_data);
		
		/*Set EPHY register offset 0x06 bit 7 to 0*/
		ephy_data = ReadEPHY16(0x06);
		ephy_data &= ~(1 << 7);
		WriteEPHY16(0x06, ephy_data);
		/*Set EPHY registers	end*/
		
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		//disable clock request.
		pciDev->configWrite8(0x81, 0x00);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		if (curr_mtu_size > ETHERMTU)
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x20;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload disable
			canOffload &= ~kChecksumIP;
			
			//rx checksum offload disable
			cp_cmd &= ~RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		else
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x50;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload enable
			canOffload |= kChecksumIP;
			
			//rx checksum offload enable
			cp_cmd |= RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
	}
	else if (mcfg == MCFG_8168C_2)
	{
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
		WriteCSI32(0x70c, csi_tmp | 0x27000000);
		
		/******set EPHY registers for RTL8168CP	begin******/
		//Set EPHY register offset 0x01 bit 0 to 1.
		ephy_data = ReadEPHY16(0x01);
		ephy_data |= (1 << 0);
		WriteEPHY16(0x01, ephy_data);
		
		//Set EPHY register offset 0x03 bit 10 to 0, bit 9 to 1 and bit 5 to 1.
		ephy_data = ReadEPHY16(0x03);
		ephy_data &= ~(1 << 10);
		ephy_data |= (1 << 9);
		ephy_data |= (1 << 5);
		WriteEPHY16(0x03, ephy_data);
		/******set EPHY registers for RTL8168CP	end******/
		
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		//disable clock request.
		pciDev->configWrite8(0x81, 0x00);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		if (curr_mtu_size > ETHERMTU)
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x20;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload disable
			canOffload &= ~kChecksumIP;
			
			//rx checksum offload disable
			cp_cmd &= ~RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		else
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x50;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload enable
			canOffload |= kChecksumIP;
			
			//rx checksum offload enable
			cp_cmd |= RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
	}
	else if (mcfg == MCFG_8168C_3)
	{
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
		WriteCSI32(0x70c, csi_tmp | 0x27000000);
		
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		//disable clock request.
		pciDev->configWrite8(0x81, 0x00);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		if (curr_mtu_size > ETHERMTU)
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x20;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload disable
			canOffload &= ~kChecksumIP;
			
			//rx checksum offload disable
			cp_cmd &= ~RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		else
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x50;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload enable
			canOffload |= kChecksumIP;
			
			//rx checksum offload enable
			cp_cmd |= RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
	}
	return;
}

// taken from rtl8168_hw_start()
void RealtekR1000::RTL8168CPHwStart2()
{
	u8 device_control;
	u32 csi_tmp;
	if (mcfg == MCFG_8168CP_1)
	{
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
		WriteCSI32(0x70c, csi_tmp | 0x27000000);
		RTL8168WriteERI(0x1EC, 1, 0x07, ERIAR_ASF);
		
		//disable clock request.
		pciDev->configWrite8(0x81, 0x00);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		if (curr_mtu_size > ETHERMTU)
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x20;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload disable
			canOffload &= ~kChecksumIP;
			
			//rx checksum offload disable
			cp_cmd &= ~RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		else
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x50;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload enable
			canOffload |= kChecksumIP;
			
			//rx checksum offload enable
			cp_cmd |= RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
	}
	else if (mcfg == MCFG_8168CP_2)
	{
		/*set PCI configuration space offset 0x70F to 0x27*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
		WriteCSI32(0x70c, csi_tmp | 0x27000000);
		RTL8168WriteERI(0x1EC, 1, 0x07, ERIAR_ASF);
		
		//disable clock request.
		pciDev->configWrite8(0x81, 0x00);
		
		WriteMMIO16(CPlusCmd, ReadMMIO16(CPlusCmd) & 
					~(EnableBist | Macdbgo_oe | Force_halfdup | Force_rxflow_en | Force_txflow_en | 
					  Cxpl_dbg_sel | ASF | PktCntrDisable | Macdbgo_sel));
		
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Beacon_en);
		
		WriteMMIO8(0xD1, 0x20);
		
		if (curr_mtu_size > ETHERMTU)
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x20
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x20;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload disable
			canOffload &= ~kChecksumIP;
			
			//rx checksum offload disable
			cp_cmd &= ~RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		else
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~Jumbo_En1);
			
			//Set PCI configuration space offset 0x79 to 0x50
			/*Increase the Tx performance*/
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x50;
			pciDev->configWrite8(0x79, device_control);
			
			//tx checksum offload enable
			canOffload |= kChecksumIP;
			
			//rx checksum offload enable
			cp_cmd |= RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		
	}
	return;
}

// taken from rtl8168_hw_start()
void RealtekR1000::RTL8168DHwStart2()
{
	u8 device_control;
	u32 csi_tmp;
	if (mcfg == MCFG_8168D_1)
	{
		/*set PCI configuration space offset 0x70F to 0x13*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
		WriteCSI32(0x70c, csi_tmp | 0x13000000);
		
		/* disable clock request. */
		pciDev->configWrite8(0x81, 0x00);
		
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~BIT_4);
		WriteMMIO8(DBG_reg, ReadMMIO8(DBG_reg) | BIT_7 | BIT_1);
		
		if (curr_mtu_size > ETHERMTU)
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | Jumbo_En1);
			
			/* Set PCI configuration space offset 0x79 to 0x20 */
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x20;
			pciDev->configWrite8(0x79, device_control);
			
			/* tx checksum offload disable */
			canOffload &= ~kChecksumIP;
			
			/* rx checksum offload disable */
			cp_cmd &= ~RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		else
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~Jumbo_En1);
			
			/* Set PCI configuration space offset 0x79 to 0x50 */
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x50;
			pciDev->configWrite8(0x79, device_control);
			
			/* tx checksum offload enable */
			canOffload |= kChecksumIP;
			
			/* rx checksum offload enable */
			cp_cmd |= RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		
		/* set EPHY registers */
		WriteEPHY16(0x01, 0x7C7D);
		WriteEPHY16(0x02, 0x091F);
		WriteEPHY16(0x06, 0xB271);
		WriteEPHY16(0x07, 0xCE00);
	}
	else if (mcfg == MCFG_8168D_2)
	{
		/*set PCI configuration space offset 0x70F to 0x13*/
		/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
		csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
		WriteCSI32(0x70c, csi_tmp | 0x13000000);
		
		WriteMMIO8(DBG_reg, ReadMMIO8(DBG_reg) | BIT_7 | BIT_1);
		
		if (curr_mtu_size > ETHERMTU)
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) | Jumbo_En1);
			
			/* Set PCI configuration space offset 0x79 to 0x20 */
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x20;
			pciDev->configWrite8(0x79, device_control);
			
			/* tx checksum offload disable */
			canOffload &= ~kChecksumIP;
			
			/* rx checksum offload disable */
			cp_cmd &= ~RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		else
		{
			WriteMMIO8(ETThReg, ETTh);
			WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
			WriteMMIO8(Config4, ReadMMIO8(Config4) & ~Jumbo_En1);
			
			/* Set PCI configuration space offset 0x79 to 0x50 */
			device_control = pciDev->configRead8(0x79);
			device_control &= ~0x70;
			device_control |= 0x50;
			pciDev->configWrite8(0x79, device_control);
			
			/* tx checksum offload enable */
			canOffload |= kChecksumIP;
			
			/* rx checksum offload enable */
			cp_cmd |= RxChkSum;
			WriteMMIO16(CPlusCmd, cp_cmd);
		}
		
		WriteMMIO8(Config1, 0xDF);
		
		/* set EPHY registers */
		WriteEPHY16(0x01, 0x6C7F);
		WriteEPHY16(0x02, 0x011F);
		WriteEPHY16(0x03, 0xC1B2);
		WriteEPHY16(0x1A, 0x0546);
		WriteEPHY16(0x1C, 0x80C4);
		WriteEPHY16(0x1D, 0x78E4);
		WriteEPHY16(0x0A, 0x8100);
		
		/* disable clock request. */
		pciDev->configWrite8(0x81, 0x00);
		
		WriteMMIO8(0xF3, ReadMMIO8(0xF3) | (1 << 2));
		
	}
	return;
}

// taken from rtl8168_hw_start()
void RealtekR1000::RTL8168DPHwStart2()
{
	u8 device_control;
	u32 csi_tmp;
	/*set PCI configuration space offset 0x70F to 0x37*/
	/*When the register offset of PCI configuration space larger than 0xff, use CSI to access it.*/
	csi_tmp = ReadCSI32(0x70c) & 0x00ffffff;
	WriteCSI32(0x70c, csi_tmp | 0x37000000);
	
	/* Set PCI configuration space offset 0x79 to 0x50 */
	device_control = pciDev->configRead8(0x79);
	device_control &= ~0x70;
	device_control |= 0x50;
	pciDev->configWrite8(0x79, device_control);
	
	if (curr_mtu_size > ETHERMTU)
	{
		WriteMMIO8(ETThReg, ETTh);
		WriteMMIO8(Config3, ReadMMIO8(Config3) | Jumbo_En0);
		
		/* tx checksum offload disable */
		canOffload &= ~kChecksumIP;
		
		/* rx checksum offload disable */
		cp_cmd &= ~RxChkSum;
		WriteMMIO16(CPlusCmd, cp_cmd);
	}
	else
	{
		WriteMMIO8(ETThReg, ETTh);
		WriteMMIO8(Config3, ReadMMIO8(Config3) & ~Jumbo_En0);
		
		/* tx checksum offload enable */
		canOffload |= kChecksumIP;
		
		/* rx checksum offload enable */
		cp_cmd |= RxChkSum;
		WriteMMIO16(CPlusCmd, cp_cmd);
	}
	
	WriteMMIO8(Config1, 0xDF);
	return;
}


// taken from rtl8168_nic_reset()
void RealtekR1000::RTL8168NicReset()
{
	DLog("RTL8168NicReset\n");
	
	if ((mcfg >= MCFG_8168C_1) && 
	    (mcfg <= MCFG_8168D_2))
	{
		WriteMMIO8(ChipCmd, StopReq | CmdRxEnb | CmdTxEnb);
		IODelay(100);
	}
	
	if (mcfg == MCFG_8168DP_1)
		WriteMMIO32(RxConfig, 
					ReadMMIO32(RxConfig) &
					~(AcceptErr | AcceptRunt | AcceptBroadcast | AcceptMulticast | AcceptMyPhys |  AcceptAllPhys));
	
	/* Soft reset the chip. */
	WriteMMIO8(ChipCmd, CmdReset);
	
	/* Check that the chip has finished the reset. */
	for (int i = 1000; i > 0; i--)
	{
		if ((ReadMMIO8(ChipCmd) & CmdReset) == 0)
			break;
		IODelay(100);
	}
	
	// Clear Rx counter
	cur_rx = 0;
	
	if (mcfg == MCFG_8168DP_1)
	{
		WriteMMIO32(OCPDR, 0x01);
		WriteMMIO32(OCPAR, 0x80001038);
		WriteMMIO32(OCPDR, 0x01);
		WriteMMIO32(OCPAR, 0x00001030);
		
		WriteMMIO32(OCPAR, 0x00001034);
		for (int i = 1000; i > 0; i--)
		{
			if ((ReadMMIO32(OCPDR) & 0xFFFF) == 0)
				break;
			IODelay(100);
		}
	}
}

// Configure the PHY interface
void RealtekR1000::RTL8168HwPhyConfig()
{
	switch (mcfg)
	{
		case MCFG_8168B_1:
		case MCFG_8168B_2:
		case MCFG_8168B_3:
			RTL8168BHwPhyConfig();
			break;
			
		case MCFG_8168C_1:
		case MCFG_8168C_2:
		case MCFG_8168C_3:
			RTL8168CHwPhyConfig();
			break;
			
		case MCFG_8168CP_1:
		case MCFG_8168CP_2:
			RTL8168CPHwPhyConfig();
			break;
			
		case MCFG_8168D_1:
		case MCFG_8168D_2:
			RTL8168DHwPhyConfig();
			break;
			
		case MCFG_8168DP_1:
			RTL8168DPHwPhyConfig();
			break;
	}
}

// subroutines of above
void RealtekR1000::RTL8168BHwPhyConfig()
{
	if (mcfg == MCFG_8168B_1)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x0B, 0x94B0);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x12, 0x6096);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x0D, 0xF8A0);
	}
	else if (mcfg == MCFG_8168B_2)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x0B, 0x94B0);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x12, 0x6096);
		
		WriteGMII16(0x1F, 0x0000);
	}
	else if (mcfg == MCFG_8168B_3)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x0B, 0x94B0);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x12, 0x6096);
		
		WriteGMII16(0x1F, 0x0000);
	}
}

void RealtekR1000::RTL8168CHwPhyConfig()
{
	DLog("RTL8168CHwPhyConfig\n");
	
	if (mcfg == MCFG_8168C_1)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x12, 0x2300);
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x16, 0x000A);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x12, 0xC096);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x00, 0x88DE);
		WriteGMII16(0x01, 0x82B1);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x08, 0x9E30);
		WriteGMII16(0x09, 0x01F0);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x0A, 0x5500);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x03, 0x7002);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x0C, 0x00C8);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x14, ReadGMII16(0x14) | (1 << 5));
		WriteGMII16(0x0D, ReadGMII16(0x0D) & ~(1 << 5));
	}
	else if (mcfg == MCFG_8168C_2)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x12, 0x2300);
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x16, 0x0F0A);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x00, 0x88DE);
		WriteGMII16(0x01, 0x82B1);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x0C, 0x7EB8);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x06, 0x0761);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x03, 0x802F);
		WriteGMII16(0x02, 0x4F02);
		WriteGMII16(0x01, 0x0409);
		WriteGMII16(0x00, 0xF099);
		WriteGMII16(0x04, 0x9800);
		WriteGMII16(0x04, 0x9000);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x16, ReadGMII16(0x16) | (1 << 0));
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x14, ReadGMII16(0x14) | (1 << 5));
		WriteGMII16(0x0D, ReadGMII16(0x0D) & ~(1 << 5));
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x1D, 0x3D98);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x17, 0x0CC0);
		WriteGMII16(0x1F, 0x0000);
	}
	else if (mcfg == MCFG_8168C_3)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x12, 0x2300);
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x16, 0x0F0A);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x00, 0x88DE);
		WriteGMII16(0x01, 0x82B1);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x0C, 0x7EB8);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x06, 0x0761);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x06, 0x5461);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x16, ReadGMII16(0x16) | (1 << 0));
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x14, ReadGMII16(0x14) | (1 << 5));
		WriteGMII16(0x0D, ReadGMII16(0x0D) & ~(1 << 5));
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x1D, 0x3D98);
		WriteGMII16(0x1F, 0x0000);
		
		WriteGMII16(0x1f, 0x0001);
		WriteGMII16(0x17, 0x0CC0);
		WriteGMII16(0x1F, 0x0000);
	}
}

void RealtekR1000::RTL8168CPHwPhyConfig()
{
	if (mcfg == MCFG_8168CP_1)
	{
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x14, ReadGMII16(0x14) | (1 << 5));
		WriteGMII16(0x0D, ReadGMII16(0x0D) & ~(1 << 5));
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x1D, 0x3D98);
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x14, 0xCAA3);
		WriteGMII16(0x1C, 0x000A);
		WriteGMII16(0x18, 0x65D0);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x17, 0xB580);
		WriteGMII16(0x18, 0xFF54);
		WriteGMII16(0x19, 0x3954);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x0D, 0x310C);
		WriteGMII16(0x0E, 0x310C);
		WriteGMII16(0x0F, 0x311C);
		WriteGMII16(0x06, 0x0761);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x18, 0xFF55);
		WriteGMII16(0x19, 0x3955);
		WriteGMII16(0x18, 0xFF54);
		WriteGMII16(0x19, 0x3954);
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x17, 0x0CC0);
		
		WriteGMII16(0x1F, 0x0000);
	}
	else if (mcfg == MCFG_8168CP_2)
	{
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x14, ReadGMII16(0x14) | (1 << 5));
		WriteGMII16(0x0D, ReadGMII16(0x0D) & ~(1 << 5));
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x14, 0xCAA3);
		WriteGMII16(0x1C, 0x000A);
		WriteGMII16(0x18, 0x65D0);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x17, 0xB580);
		WriteGMII16(0x18, 0xFF54);
		WriteGMII16(0x19, 0x3954);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x0D, 0x310C);
		WriteGMII16(0x0E, 0x310C);
		WriteGMII16(0x0F, 0x311C);
		WriteGMII16(0x06, 0x0761);
		
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x18, 0xFF55);
		WriteGMII16(0x19, 0x3955);
		WriteGMII16(0x18, 0xFF54);
		WriteGMII16(0x19, 0x3954);
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x17, 0x0CC0);
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x16, ReadGMII16(0x16) | (1 << 0));
		
		WriteGMII16(0x1F, 0x0000);
	}
}

void RealtekR1000::RTL8168DHwPhyConfig()
{
	u32 gphy_val;
	if (mcfg == MCFG_8168D_1)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x06, 0x4064);
		WriteGMII16(0x07, 0x2863);
		WriteGMII16(0x08, 0x059C);
		WriteGMII16(0x09, 0x26B4);
		WriteGMII16(0x0A, 0x6A19);
		WriteGMII16(0x0B, 0xDCC8);
		WriteGMII16(0x10, 0xF06D);
		WriteGMII16(0x14, 0x7F68);
		WriteGMII16(0x18, 0x7FD9);
		WriteGMII16(0x1C, 0xF0FF);
		WriteGMII16(0x1D, 0x3D9C);
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x12, 0xF49F);
		WriteGMII16(0x13, 0x070B);
		WriteGMII16(0x1A, 0x05AD);
		WriteGMII16(0x14, 0x94C0);
		
		WriteGMII16(0x1F, 0x0002);
		gphy_val = ReadGMII16(0x0B) & 0xFF00;
		gphy_val |= 0x10;
		WriteGMII16(0x0B, gphy_val);
		gphy_val = ReadGMII16(0x0C) & 0x00FF;
		gphy_val |= 0xA200;
		WriteGMII16(0x0C, gphy_val);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x06, 0x5561);
		WriteGMII16(0x1F, 0x0005);
		WriteGMII16(0x05, 0x8332);
		WriteGMII16(0x06, 0x5561);
		
		if (RTL8168ReadEfuse(0x01) == 0xb1)
		{
			WriteGMII16(0x1F, 0x0002);
			WriteGMII16(0x05, 0x669A);
			WriteGMII16(0x1F, 0x0005);
			WriteGMII16(0x05, 0x8330);
			WriteGMII16(0x06, 0x669A);
			
			WriteGMII16(0x1F, 0x0002);
			gphy_val = ReadGMII16(0x0D);
			if ((gphy_val & 0x00FF) != 0x006C)
			{
				gphy_val &= 0xFF00;
				WriteGMII16(0x1F, 0x0002);
				WriteGMII16(0x0D, gphy_val | 0x0065);
				WriteGMII16(0x0D, gphy_val | 0x0066);
				WriteGMII16(0x0D, gphy_val | 0x0067);
				WriteGMII16(0x0D, gphy_val | 0x0068);
				WriteGMII16(0x0D, gphy_val | 0x0069);
				WriteGMII16(0x0D, gphy_val | 0x006A);
				WriteGMII16(0x0D, gphy_val | 0x006B);
				WriteGMII16(0x0D, gphy_val | 0x006C);
			}
		}
		else
		{
			WriteGMII16(0x1F, 0x0002);
			WriteGMII16(0x05, 0x6662);
			WriteGMII16(0x1F, 0x0005);
			WriteGMII16(0x05, 0x8330);
			WriteGMII16(0x06, 0x6662);
		}
		
		WriteGMII16(0x1F, 0x0002);
		gphy_val = ReadGMII16(0x0D);
		gphy_val |= BIT_9;
		gphy_val |= BIT_8;
		WriteGMII16(0x0D, gphy_val);
		gphy_val = ReadGMII16(0x0F);
		gphy_val |= BIT_4;
		WriteGMII16(0x0F, gphy_val);
		
		WriteGMII16(0x1F, 0x0002);
		gphy_val = ReadGMII16(0x02);
		gphy_val &= ~BIT_10;
		gphy_val &= ~BIT_9;
		gphy_val |= BIT_8;
		WriteGMII16(0x02, gphy_val);
		gphy_val = ReadGMII16(0x03);
		gphy_val &= ~BIT_15;
		gphy_val &= ~BIT_14;
		gphy_val &= ~BIT_13;
		WriteGMII16(0x03, gphy_val);
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x17, 0x0CC0);
		
		WriteGMII16(0x1F, 0x0005);
		WriteGMII16(0x05, 0x001B);
		if (ReadGMII16(0x06) == 0xBF00)
		{
			WriteGMII16(0x1f, 0x0005);
			WriteGMII16(0x05, 0xfff6);
			WriteGMII16(0x06, 0x0080);
			WriteGMII16(0x05, 0x8000);
			WriteGMII16(0x06, 0xf8f9);
			WriteGMII16(0x06, 0xfaef);
			WriteGMII16(0x06, 0x59ee);
			WriteGMII16(0x06, 0xf8ea);
			WriteGMII16(0x06, 0x00ee);
			WriteGMII16(0x06, 0xf8eb);
			WriteGMII16(0x06, 0x00e0);
			WriteGMII16(0x06, 0xf87c);
			WriteGMII16(0x06, 0xe1f8);
			WriteGMII16(0x06, 0x7d59);
			WriteGMII16(0x06, 0x0fef);
			WriteGMII16(0x06, 0x0139);
			WriteGMII16(0x06, 0x029e);
			WriteGMII16(0x06, 0x06ef);
			WriteGMII16(0x06, 0x1039);
			WriteGMII16(0x06, 0x089f);
			WriteGMII16(0x06, 0x2aee);
			WriteGMII16(0x06, 0xf8ea);
			WriteGMII16(0x06, 0x00ee);
			WriteGMII16(0x06, 0xf8eb);
			WriteGMII16(0x06, 0x01e0);
			WriteGMII16(0x06, 0xf87c);
			WriteGMII16(0x06, 0xe1f8);
			WriteGMII16(0x06, 0x7d58);
			WriteGMII16(0x06, 0x409e);
			WriteGMII16(0x06, 0x0f39);
			WriteGMII16(0x06, 0x46aa);
			WriteGMII16(0x06, 0x0bbf);
			WriteGMII16(0x06, 0x8290);
			WriteGMII16(0x06, 0xd682);
			WriteGMII16(0x06, 0x9802);
			WriteGMII16(0x06, 0x014f);
			WriteGMII16(0x06, 0xae09);
			WriteGMII16(0x06, 0xbf82);
			WriteGMII16(0x06, 0x98d6);
			WriteGMII16(0x06, 0x82a0);
			WriteGMII16(0x06, 0x0201);
			WriteGMII16(0x06, 0x4fef);
			WriteGMII16(0x06, 0x95fe);
			WriteGMII16(0x06, 0xfdfc);
			WriteGMII16(0x06, 0x05f8);
			WriteGMII16(0x06, 0xf9fa);
			WriteGMII16(0x06, 0xeef8);
			WriteGMII16(0x06, 0xea00);
			WriteGMII16(0x06, 0xeef8);
			WriteGMII16(0x06, 0xeb00);
			WriteGMII16(0x06, 0xe2f8);
			WriteGMII16(0x06, 0x7ce3);
			WriteGMII16(0x06, 0xf87d);
			WriteGMII16(0x06, 0xa511);
			WriteGMII16(0x06, 0x1112);
			WriteGMII16(0x06, 0xd240);
			WriteGMII16(0x06, 0xd644);
			WriteGMII16(0x06, 0x4402);
			WriteGMII16(0x06, 0x8217);
			WriteGMII16(0x06, 0xd2a0);
			WriteGMII16(0x06, 0xd6aa);
			WriteGMII16(0x06, 0xaa02);
			WriteGMII16(0x06, 0x8217);
			WriteGMII16(0x06, 0xae0f);
			WriteGMII16(0x06, 0xa544);
			WriteGMII16(0x06, 0x4402);
			WriteGMII16(0x06, 0xae4d);
			WriteGMII16(0x06, 0xa5aa);
			WriteGMII16(0x06, 0xaa02);
			WriteGMII16(0x06, 0xae47);
			WriteGMII16(0x06, 0xaf82);
			WriteGMII16(0x06, 0x13ee);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x00ee);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0x0fee);
			WriteGMII16(0x06, 0x834c);
			WriteGMII16(0x06, 0x0fee);
			WriteGMII16(0x06, 0x834f);
			WriteGMII16(0x06, 0x00ee);
			WriteGMII16(0x06, 0x8351);
			WriteGMII16(0x06, 0x00ee);
			WriteGMII16(0x06, 0x834a);
			WriteGMII16(0x06, 0xffee);
			WriteGMII16(0x06, 0x834b);
			WriteGMII16(0x06, 0xffe0);
			WriteGMII16(0x06, 0x8330);
			WriteGMII16(0x06, 0xe183);
			WriteGMII16(0x06, 0x3158);
			WriteGMII16(0x06, 0xfee4);
			WriteGMII16(0x06, 0xf88a);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x8be0);
			WriteGMII16(0x06, 0x8332);
			WriteGMII16(0x06, 0xe183);
			WriteGMII16(0x06, 0x3359);
			WriteGMII16(0x06, 0x0fe2);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0x0c24);
			WriteGMII16(0x06, 0x5af0);
			WriteGMII16(0x06, 0x1e12);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x8ce5);
			WriteGMII16(0x06, 0xf88d);
			WriteGMII16(0x06, 0xaf82);
			WriteGMII16(0x06, 0x13e0);
			WriteGMII16(0x06, 0x834f);
			WriteGMII16(0x06, 0x10e4);
			WriteGMII16(0x06, 0x834f);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4e78);
			WriteGMII16(0x06, 0x009f);
			WriteGMII16(0x06, 0x0ae0);
			WriteGMII16(0x06, 0x834f);
			WriteGMII16(0x06, 0xa010);
			WriteGMII16(0x06, 0xa5ee);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x01e0);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x7805);
			WriteGMII16(0x06, 0x9e9a);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4e78);
			WriteGMII16(0x06, 0x049e);
			WriteGMII16(0x06, 0x10e0);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x7803);
			WriteGMII16(0x06, 0x9e0f);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4e78);
			WriteGMII16(0x06, 0x019e);
			WriteGMII16(0x06, 0x05ae);
			WriteGMII16(0x06, 0x0caf);
			WriteGMII16(0x06, 0x81f8);
			WriteGMII16(0x06, 0xaf81);
			WriteGMII16(0x06, 0xa3af);
			WriteGMII16(0x06, 0x81dc);
			WriteGMII16(0x06, 0xaf82);
			WriteGMII16(0x06, 0x13ee);
			WriteGMII16(0x06, 0x8348);
			WriteGMII16(0x06, 0x00ee);
			WriteGMII16(0x06, 0x8349);
			WriteGMII16(0x06, 0x00e0);
			WriteGMII16(0x06, 0x8351);
			WriteGMII16(0x06, 0x10e4);
			WriteGMII16(0x06, 0x8351);
			WriteGMII16(0x06, 0x5801);
			WriteGMII16(0x06, 0x9fea);
			WriteGMII16(0x06, 0xd000);
			WriteGMII16(0x06, 0xd180);
			WriteGMII16(0x06, 0x1f66);
			WriteGMII16(0x06, 0xe2f8);
			WriteGMII16(0x06, 0xeae3);
			WriteGMII16(0x06, 0xf8eb);
			WriteGMII16(0x06, 0x5af8);
			WriteGMII16(0x06, 0x1e20);
			WriteGMII16(0x06, 0xe6f8);
			WriteGMII16(0x06, 0xeae5);
			WriteGMII16(0x06, 0xf8eb);
			WriteGMII16(0x06, 0xd302);
			WriteGMII16(0x06, 0xb3fe);
			WriteGMII16(0x06, 0xe2f8);
			WriteGMII16(0x06, 0x7cef);
			WriteGMII16(0x06, 0x325b);
			WriteGMII16(0x06, 0x80e3);
			WriteGMII16(0x06, 0xf87d);
			WriteGMII16(0x06, 0x9e03);
			WriteGMII16(0x06, 0x7dff);
			WriteGMII16(0x06, 0xff0d);
			WriteGMII16(0x06, 0x581c);
			WriteGMII16(0x06, 0x551a);
			WriteGMII16(0x06, 0x6511);
			WriteGMII16(0x06, 0xa190);
			WriteGMII16(0x06, 0xd3e2);
			WriteGMII16(0x06, 0x8348);
			WriteGMII16(0x06, 0xe383);
			WriteGMII16(0x06, 0x491b);
			WriteGMII16(0x06, 0x56ab);
			WriteGMII16(0x06, 0x08ef);
			WriteGMII16(0x06, 0x56e6);
			WriteGMII16(0x06, 0x8348);
			WriteGMII16(0x06, 0xe783);
			WriteGMII16(0x06, 0x4910);
			WriteGMII16(0x06, 0xd180);
			WriteGMII16(0x06, 0x1f66);
			WriteGMII16(0x06, 0xa004);
			WriteGMII16(0x06, 0xb9e2);
			WriteGMII16(0x06, 0x8348);
			WriteGMII16(0x06, 0xe383);
			WriteGMII16(0x06, 0x49ef);
			WriteGMII16(0x06, 0x65e2);
			WriteGMII16(0x06, 0x834a);
			WriteGMII16(0x06, 0xe383);
			WriteGMII16(0x06, 0x4b1b);
			WriteGMII16(0x06, 0x56aa);
			WriteGMII16(0x06, 0x0eef);
			WriteGMII16(0x06, 0x56e6);
			WriteGMII16(0x06, 0x834a);
			WriteGMII16(0x06, 0xe783);
			WriteGMII16(0x06, 0x4be2);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0xe683);
			WriteGMII16(0x06, 0x4ce0);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0xa000);
			WriteGMII16(0x06, 0x0caf);
			WriteGMII16(0x06, 0x81dc);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4d10);
			WriteGMII16(0x06, 0xe483);
			WriteGMII16(0x06, 0x4dae);
			WriteGMII16(0x06, 0x0480);
			WriteGMII16(0x06, 0xe483);
			WriteGMII16(0x06, 0x4de0);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x7803);
			WriteGMII16(0x06, 0x9e0b);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4e78);
			WriteGMII16(0x06, 0x049e);
			WriteGMII16(0x06, 0x04ee);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x02e0);
			WriteGMII16(0x06, 0x8332);
			WriteGMII16(0x06, 0xe183);
			WriteGMII16(0x06, 0x3359);
			WriteGMII16(0x06, 0x0fe2);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0x0c24);
			WriteGMII16(0x06, 0x5af0);
			WriteGMII16(0x06, 0x1e12);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x8ce5);
			WriteGMII16(0x06, 0xf88d);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x30e1);
			WriteGMII16(0x06, 0x8331);
			WriteGMII16(0x06, 0x6801);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x8ae5);
			WriteGMII16(0x06, 0xf88b);
			WriteGMII16(0x06, 0xae37);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4e03);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4ce1);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0x1b01);
			WriteGMII16(0x06, 0x9e04);
			WriteGMII16(0x06, 0xaaa1);
			WriteGMII16(0x06, 0xaea8);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4e04);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4f00);
			WriteGMII16(0x06, 0xaeab);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4f78);
			WriteGMII16(0x06, 0x039f);
			WriteGMII16(0x06, 0x14ee);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x05d2);
			WriteGMII16(0x06, 0x40d6);
			WriteGMII16(0x06, 0x5554);
			WriteGMII16(0x06, 0x0282);
			WriteGMII16(0x06, 0x17d2);
			WriteGMII16(0x06, 0xa0d6);
			WriteGMII16(0x06, 0xba00);
			WriteGMII16(0x06, 0x0282);
			WriteGMII16(0x06, 0x17fe);
			WriteGMII16(0x06, 0xfdfc);
			WriteGMII16(0x06, 0x05f8);
			WriteGMII16(0x06, 0xe0f8);
			WriteGMII16(0x06, 0x60e1);
			WriteGMII16(0x06, 0xf861);
			WriteGMII16(0x06, 0x6802);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x60e5);
			WriteGMII16(0x06, 0xf861);
			WriteGMII16(0x06, 0xe0f8);
			WriteGMII16(0x06, 0x48e1);
			WriteGMII16(0x06, 0xf849);
			WriteGMII16(0x06, 0x580f);
			WriteGMII16(0x06, 0x1e02);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x48e5);
			WriteGMII16(0x06, 0xf849);
			WriteGMII16(0x06, 0xd000);
			WriteGMII16(0x06, 0x0282);
			WriteGMII16(0x06, 0x5bbf);
			WriteGMII16(0x06, 0x8350);
			WriteGMII16(0x06, 0xef46);
			WriteGMII16(0x06, 0xdc19);
			WriteGMII16(0x06, 0xddd0);
			WriteGMII16(0x06, 0x0102);
			WriteGMII16(0x06, 0x825b);
			WriteGMII16(0x06, 0x0282);
			WriteGMII16(0x06, 0x77e0);
			WriteGMII16(0x06, 0xf860);
			WriteGMII16(0x06, 0xe1f8);
			WriteGMII16(0x06, 0x6158);
			WriteGMII16(0x06, 0xfde4);
			WriteGMII16(0x06, 0xf860);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x61fc);
			WriteGMII16(0x06, 0x04f9);
			WriteGMII16(0x06, 0xfafb);
			WriteGMII16(0x06, 0xc6bf);
			WriteGMII16(0x06, 0xf840);
			WriteGMII16(0x06, 0xbe83);
			WriteGMII16(0x06, 0x50a0);
			WriteGMII16(0x06, 0x0101);
			WriteGMII16(0x06, 0x071b);
			WriteGMII16(0x06, 0x89cf);
			WriteGMII16(0x06, 0xd208);
			WriteGMII16(0x06, 0xebdb);
			WriteGMII16(0x06, 0x19b2);
			WriteGMII16(0x06, 0xfbff);
			WriteGMII16(0x06, 0xfefd);
			WriteGMII16(0x06, 0x04f8);
			WriteGMII16(0x06, 0xe0f8);
			WriteGMII16(0x06, 0x48e1);
			WriteGMII16(0x06, 0xf849);
			WriteGMII16(0x06, 0x6808);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x48e5);
			WriteGMII16(0x06, 0xf849);
			WriteGMII16(0x06, 0x58f7);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x48e5);
			WriteGMII16(0x06, 0xf849);
			WriteGMII16(0x06, 0xfc04);
			WriteGMII16(0x06, 0x4d20);
			WriteGMII16(0x06, 0x0002);
			WriteGMII16(0x06, 0x4e22);
			WriteGMII16(0x06, 0x0002);
			WriteGMII16(0x06, 0x4ddf);
			WriteGMII16(0x06, 0xff01);
			WriteGMII16(0x06, 0x4edd);
			WriteGMII16(0x06, 0xff01);
			WriteGMII16(0x06, 0xf8fa);
			WriteGMII16(0x06, 0xfbef);
			WriteGMII16(0x06, 0x79bf);
			WriteGMII16(0x06, 0xf822);
			WriteGMII16(0x06, 0xd819);
			WriteGMII16(0x06, 0xd958);
			WriteGMII16(0x06, 0x849f);
			WriteGMII16(0x06, 0x09bf);
			WriteGMII16(0x06, 0x82be);
			WriteGMII16(0x06, 0xd682);
			WriteGMII16(0x06, 0xc602);
			WriteGMII16(0x06, 0x014f);
			WriteGMII16(0x06, 0xef97);
			WriteGMII16(0x06, 0xfffe);
			WriteGMII16(0x06, 0xfc05);
			WriteGMII16(0x06, 0x17ff);
			WriteGMII16(0x06, 0xfe01);
			WriteGMII16(0x06, 0x1700);
			WriteGMII16(0x06, 0x0102);
			WriteGMII16(0x05, 0x83d8);
			WriteGMII16(0x06, 0x8051);
			WriteGMII16(0x05, 0x83d6);
			WriteGMII16(0x06, 0x82a0);
			WriteGMII16(0x05, 0x83d4);
			WriteGMII16(0x06, 0x8000);
			WriteGMII16(0x02, 0x2010);
			WriteGMII16(0x03, 0xdc00);
			WriteGMII16(0x1f, 0x0000);
			WriteGMII16(0x0b, 0x0600);
			WriteGMII16(0x1f, 0x0005);
			WriteGMII16(0x05, 0xfff6);
			WriteGMII16(0x06, 0x00fc);
			WriteGMII16(0x1f, 0x0000);
		}
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x0D, 0xF880);
		WriteGMII16(0x1F, 0x0000);
	}
	else if (mcfg == MCFG_8168D_2)
	{
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x06, 0x4064);
		WriteGMII16(0x07, 0x2863);
		WriteGMII16(0x08, 0x059C);
		WriteGMII16(0x09, 0x26B4);
		WriteGMII16(0x0A, 0x6A19);
		WriteGMII16(0x0B, 0xDCC8);
		WriteGMII16(0x10, 0xF06D);
		WriteGMII16(0x14, 0x7F68);
		WriteGMII16(0x18, 0x7FD9);
		WriteGMII16(0x1C, 0xF0FF);
		WriteGMII16(0x1D, 0x3D9C);
		WriteGMII16(0x1F, 0x0003);
		WriteGMII16(0x12, 0xF49F);
		WriteGMII16(0x13, 0x070B);
		WriteGMII16(0x1A, 0x05AD);
		WriteGMII16(0x14, 0x94C0);
		
		WriteGMII16(0x1F, 0x0002);
		WriteGMII16(0x06, 0x5561);
		WriteGMII16(0x1F, 0x0005);
		WriteGMII16(0x05, 0x8332);
		WriteGMII16(0x06, 0x5561);
		
		if (RTL8168ReadEfuse(0x01) == 0xb1)
		{
			WriteGMII16(0x1F, 0x0002);
			WriteGMII16(0x05, 0x669A);
			WriteGMII16(0x1F, 0x0005);
			WriteGMII16(0x05, 0x8330);
			WriteGMII16(0x06, 0x669A);
			
			WriteGMII16(0x1F, 0x0002);
			gphy_val = ReadGMII16(0x0D);
			if ((gphy_val & 0x00FF) != 0x006C)
			{
				gphy_val &= 0xFF00;
				WriteGMII16(0x1F, 0x0002);
				WriteGMII16(0x0D, gphy_val | 0x0065);
				WriteGMII16(0x0D, gphy_val | 0x0066);
				WriteGMII16(0x0D, gphy_val | 0x0067);
				WriteGMII16(0x0D, gphy_val | 0x0068);
				WriteGMII16(0x0D, gphy_val | 0x0069);
				WriteGMII16(0x0D, gphy_val | 0x006A);
				WriteGMII16(0x0D, gphy_val | 0x006B);
				WriteGMII16(0x0D, gphy_val | 0x006C);
			}
		}
		else
		{
			WriteGMII16(0x1F, 0x0002);
			WriteGMII16(0x05, 0x2642);
			WriteGMII16(0x1F, 0x0005);
			WriteGMII16(0x05, 0x8330);
			WriteGMII16(0x06, 0x2642);
		}
		
		if (RTL8168ReadEfuse(0x30) == 0x98)
		{
			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(0x11, ReadGMII16(0x11) & ~BIT_1);
			WriteGMII16(0x1F, 0x0005);
			WriteGMII16(0x01, ReadGMII16(0x01) | BIT_9);
		}
		else if (RTL8168ReadEfuse(0x30) == 0x90)
		{
			WriteGMII16(0x1F, 0x0005);
			WriteGMII16(0x01, ReadGMII16(0x01) & ~BIT_9);
			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(0x16, 0x5101);
		}
		
		WriteGMII16(0x1F, 0x0002);
		gphy_val = ReadGMII16(0x02);
		gphy_val &= ~BIT_10;
		gphy_val &= ~BIT_9;
		gphy_val |= BIT_8;
		WriteGMII16(0x02, gphy_val);
		gphy_val = ReadGMII16(0x03);
		gphy_val &= ~BIT_15;
		gphy_val &= ~BIT_14;
		gphy_val &= ~BIT_13;
		WriteGMII16(0x03, gphy_val);
		
		WriteGMII16(0x1F, 0x0001);
		WriteGMII16(0x17, 0x0CC0);
		
		WriteGMII16(0x1F, 0x0002);
		gphy_val = ReadGMII16(0x0F);
		gphy_val |= BIT_4;
		gphy_val |= BIT_2;
		gphy_val |= BIT_1;
		gphy_val |= BIT_0;
		WriteGMII16(0x0F, gphy_val);
		
		WriteGMII16(0x1F, 0x0005);
		WriteGMII16(0x05, 0x001B);
		if (ReadGMII16(0x06) == 0xB300)
		{
			WriteGMII16(0x1f, 0x0005);
			WriteGMII16(0x05, 0xfff6);
			WriteGMII16(0x06, 0x0080);
			WriteGMII16(0x05, 0x8000);
			WriteGMII16(0x06, 0xf8f9);
			WriteGMII16(0x06, 0xfaee);
			WriteGMII16(0x06, 0xf8ea);
			WriteGMII16(0x06, 0x00ee);
			WriteGMII16(0x06, 0xf8eb);
			WriteGMII16(0x06, 0x00e2);
			WriteGMII16(0x06, 0xf87c);
			WriteGMII16(0x06, 0xe3f8);
			WriteGMII16(0x06, 0x7da5);
			WriteGMII16(0x06, 0x1111);
			WriteGMII16(0x06, 0x12d2);
			WriteGMII16(0x06, 0x40d6);
			WriteGMII16(0x06, 0x4444);
			WriteGMII16(0x06, 0x0281);
			WriteGMII16(0x06, 0xc6d2);
			WriteGMII16(0x06, 0xa0d6);
			WriteGMII16(0x06, 0xaaaa);
			WriteGMII16(0x06, 0x0281);
			WriteGMII16(0x06, 0xc6ae);
			WriteGMII16(0x06, 0x0fa5);
			WriteGMII16(0x06, 0x4444);
			WriteGMII16(0x06, 0x02ae);
			WriteGMII16(0x06, 0x4da5);
			WriteGMII16(0x06, 0xaaaa);
			WriteGMII16(0x06, 0x02ae);
			WriteGMII16(0x06, 0x47af);
			WriteGMII16(0x06, 0x81c2);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4e00);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4d0f);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4c0f);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4f00);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x5100);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4aff);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4bff);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x30e1);
			WriteGMII16(0x06, 0x8331);
			WriteGMII16(0x06, 0x58fe);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x8ae5);
			WriteGMII16(0x06, 0xf88b);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x32e1);
			WriteGMII16(0x06, 0x8333);
			WriteGMII16(0x06, 0x590f);
			WriteGMII16(0x06, 0xe283);
			WriteGMII16(0x06, 0x4d0c);
			WriteGMII16(0x06, 0x245a);
			WriteGMII16(0x06, 0xf01e);
			WriteGMII16(0x06, 0x12e4);
			WriteGMII16(0x06, 0xf88c);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x8daf);
			WriteGMII16(0x06, 0x81c2);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4f10);
			WriteGMII16(0x06, 0xe483);
			WriteGMII16(0x06, 0x4fe0);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x7800);
			WriteGMII16(0x06, 0x9f0a);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4fa0);
			WriteGMII16(0x06, 0x10a5);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4e01);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4e78);
			WriteGMII16(0x06, 0x059e);
			WriteGMII16(0x06, 0x9ae0);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x7804);
			WriteGMII16(0x06, 0x9e10);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4e78);
			WriteGMII16(0x06, 0x039e);
			WriteGMII16(0x06, 0x0fe0);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x7801);
			WriteGMII16(0x06, 0x9e05);
			WriteGMII16(0x06, 0xae0c);
			WriteGMII16(0x06, 0xaf81);
			WriteGMII16(0x06, 0xa7af);
			WriteGMII16(0x06, 0x8152);
			WriteGMII16(0x06, 0xaf81);
			WriteGMII16(0x06, 0x8baf);
			WriteGMII16(0x06, 0x81c2);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4800);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4900);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x5110);
			WriteGMII16(0x06, 0xe483);
			WriteGMII16(0x06, 0x5158);
			WriteGMII16(0x06, 0x019f);
			WriteGMII16(0x06, 0xead0);
			WriteGMII16(0x06, 0x00d1);
			WriteGMII16(0x06, 0x801f);
			WriteGMII16(0x06, 0x66e2);
			WriteGMII16(0x06, 0xf8ea);
			WriteGMII16(0x06, 0xe3f8);
			WriteGMII16(0x06, 0xeb5a);
			WriteGMII16(0x06, 0xf81e);
			WriteGMII16(0x06, 0x20e6);
			WriteGMII16(0x06, 0xf8ea);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0xebd3);
			WriteGMII16(0x06, 0x02b3);
			WriteGMII16(0x06, 0xfee2);
			WriteGMII16(0x06, 0xf87c);
			WriteGMII16(0x06, 0xef32);
			WriteGMII16(0x06, 0x5b80);
			WriteGMII16(0x06, 0xe3f8);
			WriteGMII16(0x06, 0x7d9e);
			WriteGMII16(0x06, 0x037d);
			WriteGMII16(0x06, 0xffff);
			WriteGMII16(0x06, 0x0d58);
			WriteGMII16(0x06, 0x1c55);
			WriteGMII16(0x06, 0x1a65);
			WriteGMII16(0x06, 0x11a1);
			WriteGMII16(0x06, 0x90d3);
			WriteGMII16(0x06, 0xe283);
			WriteGMII16(0x06, 0x48e3);
			WriteGMII16(0x06, 0x8349);
			WriteGMII16(0x06, 0x1b56);
			WriteGMII16(0x06, 0xab08);
			WriteGMII16(0x06, 0xef56);
			WriteGMII16(0x06, 0xe683);
			WriteGMII16(0x06, 0x48e7);
			WriteGMII16(0x06, 0x8349);
			WriteGMII16(0x06, 0x10d1);
			WriteGMII16(0x06, 0x801f);
			WriteGMII16(0x06, 0x66a0);
			WriteGMII16(0x06, 0x04b9);
			WriteGMII16(0x06, 0xe283);
			WriteGMII16(0x06, 0x48e3);
			WriteGMII16(0x06, 0x8349);
			WriteGMII16(0x06, 0xef65);
			WriteGMII16(0x06, 0xe283);
			WriteGMII16(0x06, 0x4ae3);
			WriteGMII16(0x06, 0x834b);
			WriteGMII16(0x06, 0x1b56);
			WriteGMII16(0x06, 0xaa0e);
			WriteGMII16(0x06, 0xef56);
			WriteGMII16(0x06, 0xe683);
			WriteGMII16(0x06, 0x4ae7);
			WriteGMII16(0x06, 0x834b);
			WriteGMII16(0x06, 0xe283);
			WriteGMII16(0x06, 0x4de6);
			WriteGMII16(0x06, 0x834c);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4da0);
			WriteGMII16(0x06, 0x000c);
			WriteGMII16(0x06, 0xaf81);
			WriteGMII16(0x06, 0x8be0);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0x10e4);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0xae04);
			WriteGMII16(0x06, 0x80e4);
			WriteGMII16(0x06, 0x834d);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x4e78);
			WriteGMII16(0x06, 0x039e);
			WriteGMII16(0x06, 0x0be0);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x7804);
			WriteGMII16(0x06, 0x9e04);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4e02);
			WriteGMII16(0x06, 0xe083);
			WriteGMII16(0x06, 0x32e1);
			WriteGMII16(0x06, 0x8333);
			WriteGMII16(0x06, 0x590f);
			WriteGMII16(0x06, 0xe283);
			WriteGMII16(0x06, 0x4d0c);
			WriteGMII16(0x06, 0x245a);
			WriteGMII16(0x06, 0xf01e);
			WriteGMII16(0x06, 0x12e4);
			WriteGMII16(0x06, 0xf88c);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x8de0);
			WriteGMII16(0x06, 0x8330);
			WriteGMII16(0x06, 0xe183);
			WriteGMII16(0x06, 0x3168);
			WriteGMII16(0x06, 0x01e4);
			WriteGMII16(0x06, 0xf88a);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x8bae);
			WriteGMII16(0x06, 0x37ee);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x03e0);
			WriteGMII16(0x06, 0x834c);
			WriteGMII16(0x06, 0xe183);
			WriteGMII16(0x06, 0x4d1b);
			WriteGMII16(0x06, 0x019e);
			WriteGMII16(0x06, 0x04aa);
			WriteGMII16(0x06, 0xa1ae);
			WriteGMII16(0x06, 0xa8ee);
			WriteGMII16(0x06, 0x834e);
			WriteGMII16(0x06, 0x04ee);
			WriteGMII16(0x06, 0x834f);
			WriteGMII16(0x06, 0x00ae);
			WriteGMII16(0x06, 0xabe0);
			WriteGMII16(0x06, 0x834f);
			WriteGMII16(0x06, 0x7803);
			WriteGMII16(0x06, 0x9f14);
			WriteGMII16(0x06, 0xee83);
			WriteGMII16(0x06, 0x4e05);
			WriteGMII16(0x06, 0xd240);
			WriteGMII16(0x06, 0xd655);
			WriteGMII16(0x06, 0x5402);
			WriteGMII16(0x06, 0x81c6);
			WriteGMII16(0x06, 0xd2a0);
			WriteGMII16(0x06, 0xd6ba);
			WriteGMII16(0x06, 0x0002);
			WriteGMII16(0x06, 0x81c6);
			WriteGMII16(0x06, 0xfefd);
			WriteGMII16(0x06, 0xfc05);
			WriteGMII16(0x06, 0xf8e0);
			WriteGMII16(0x06, 0xf860);
			WriteGMII16(0x06, 0xe1f8);
			WriteGMII16(0x06, 0x6168);
			WriteGMII16(0x06, 0x02e4);
			WriteGMII16(0x06, 0xf860);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x61e0);
			WriteGMII16(0x06, 0xf848);
			WriteGMII16(0x06, 0xe1f8);
			WriteGMII16(0x06, 0x4958);
			WriteGMII16(0x06, 0x0f1e);
			WriteGMII16(0x06, 0x02e4);
			WriteGMII16(0x06, 0xf848);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x49d0);
			WriteGMII16(0x06, 0x0002);
			WriteGMII16(0x06, 0x820a);
			WriteGMII16(0x06, 0xbf83);
			WriteGMII16(0x06, 0x50ef);
			WriteGMII16(0x06, 0x46dc);
			WriteGMII16(0x06, 0x19dd);
			WriteGMII16(0x06, 0xd001);
			WriteGMII16(0x06, 0x0282);
			WriteGMII16(0x06, 0x0a02);
			WriteGMII16(0x06, 0x8226);
			WriteGMII16(0x06, 0xe0f8);
			WriteGMII16(0x06, 0x60e1);
			WriteGMII16(0x06, 0xf861);
			WriteGMII16(0x06, 0x58fd);
			WriteGMII16(0x06, 0xe4f8);
			WriteGMII16(0x06, 0x60e5);
			WriteGMII16(0x06, 0xf861);
			WriteGMII16(0x06, 0xfc04);
			WriteGMII16(0x06, 0xf9fa);
			WriteGMII16(0x06, 0xfbc6);
			WriteGMII16(0x06, 0xbff8);
			WriteGMII16(0x06, 0x40be);
			WriteGMII16(0x06, 0x8350);
			WriteGMII16(0x06, 0xa001);
			WriteGMII16(0x06, 0x0107);
			WriteGMII16(0x06, 0x1b89);
			WriteGMII16(0x06, 0xcfd2);
			WriteGMII16(0x06, 0x08eb);
			WriteGMII16(0x06, 0xdb19);
			WriteGMII16(0x06, 0xb2fb);
			WriteGMII16(0x06, 0xfffe);
			WriteGMII16(0x06, 0xfd04);
			WriteGMII16(0x06, 0xf8e0);
			WriteGMII16(0x06, 0xf848);
			WriteGMII16(0x06, 0xe1f8);
			WriteGMII16(0x06, 0x4968);
			WriteGMII16(0x06, 0x08e4);
			WriteGMII16(0x06, 0xf848);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x4958);
			WriteGMII16(0x06, 0xf7e4);
			WriteGMII16(0x06, 0xf848);
			WriteGMII16(0x06, 0xe5f8);
			WriteGMII16(0x06, 0x49fc);
			WriteGMII16(0x06, 0x044d);
			WriteGMII16(0x06, 0x2000);
			WriteGMII16(0x06, 0x024e);
			WriteGMII16(0x06, 0x2200);
			WriteGMII16(0x06, 0x024d);
			WriteGMII16(0x06, 0xdfff);
			WriteGMII16(0x06, 0x014e);
			WriteGMII16(0x06, 0xddff);
			WriteGMII16(0x06, 0x01f8);
			WriteGMII16(0x06, 0xfafb);
			WriteGMII16(0x06, 0xef79);
			WriteGMII16(0x06, 0xbff8);
			WriteGMII16(0x06, 0x22d8);
			WriteGMII16(0x06, 0x19d9);
			WriteGMII16(0x06, 0x5884);
			WriteGMII16(0x06, 0x9f09);
			WriteGMII16(0x06, 0xbf82);
			WriteGMII16(0x06, 0x6dd6);
			WriteGMII16(0x06, 0x8275);
			WriteGMII16(0x06, 0x0201);
			WriteGMII16(0x06, 0x4fef);
			WriteGMII16(0x06, 0x97ff);
			WriteGMII16(0x06, 0xfefc);
			WriteGMII16(0x06, 0x0517);
			WriteGMII16(0x06, 0xfffe);
			WriteGMII16(0x06, 0x0117);
			WriteGMII16(0x06, 0x0001);
			WriteGMII16(0x06, 0x0200);
			WriteGMII16(0x05, 0x83d8);
			WriteGMII16(0x06, 0x8000);
			WriteGMII16(0x05, 0x83d6);
			WriteGMII16(0x06, 0x824f);
			WriteGMII16(0x02, 0x2010);
			WriteGMII16(0x03, 0xdc00);
			WriteGMII16(0x1f, 0x0000);
			WriteGMII16(0x0b, 0x0600);
			WriteGMII16(0x1f, 0x0005);
			WriteGMII16(0x05, 0xfff6);
			WriteGMII16(0x06, 0x00fc);
			WriteGMII16(0x1f, 0x0000);
		}
		
		WriteGMII16(0x1F, 0x0000);
		WriteGMII16(0x0D, 0xF880);
		WriteGMII16(0x1F, 0x0000);
	}
}

void RealtekR1000::RTL8168DPHwPhyConfig()
{
	if (mcfg == MCFG_8168DP_1)
	{
		RTL8168WriteOCP_GPHY(0x1F, 0x0002);
		RTL8168WriteOCP_GPHY(0x10, 0x0008);
		RTL8168WriteOCP_GPHY(0x0D, 0x006C);
		
		RTL8168WriteOCP_GPHY(0x1F, 0x0000);
		RTL8168WriteOCP_GPHY(0x0D, 0xF880);
		
		RTL8168WriteOCP_GPHY(0x1F, 0x0001);
		RTL8168WriteOCP_GPHY(0x17, 0x0CC0);
		
		RTL8168WriteOCP_GPHY(0x1F, 0x0001);
		RTL8168WriteOCP_GPHY(0x0B, 0xA4D8);
		RTL8168WriteOCP_GPHY(0x09, 0x281C);
		RTL8168WriteOCP_GPHY(0x07, 0x2883);
		RTL8168WriteOCP_GPHY(0x0A, 0x6B35);
		RTL8168WriteOCP_GPHY(0x1D, 0x3DA4);
		RTL8168WriteOCP_GPHY(0x1C, 0xEFFD);
		RTL8168WriteOCP_GPHY(0x14, 0x7F52);
		RTL8168WriteOCP_GPHY(0x18, 0x7FC6);
		RTL8168WriteOCP_GPHY(0x08, 0x0601);
		RTL8168WriteOCP_GPHY(0x06, 0x4063);
		RTL8168WriteOCP_GPHY(0x10, 0xF074);
		RTL8168WriteOCP_GPHY(0x1F, 0x0003);
		RTL8168WriteOCP_GPHY(0x13, 0x0789);
		RTL8168WriteOCP_GPHY(0x12, 0xF4BD);
		RTL8168WriteOCP_GPHY(0x1A, 0x04FD);
		RTL8168WriteOCP_GPHY(0x14, 0x84B0);
		RTL8168WriteOCP_GPHY(0x1F, 0x0000);
		RTL8168WriteOCP_GPHY(0x00, 0x9200);
		
		RTL8168WriteOCP_GPHY(0x1F, 0x0005);
		RTL8168WriteOCP_GPHY(0x01, 0x0340);
		RTL8168WriteOCP_GPHY(0x1F, 0x0001);
		RTL8168WriteOCP_GPHY(0x04, 0x4000);
		RTL8168WriteOCP_GPHY(0x03, 0x1D21);
		RTL8168WriteOCP_GPHY(0x02, 0x0C32);
		RTL8168WriteOCP_GPHY(0x01, 0x0200);
		RTL8168WriteOCP_GPHY(0x00, 0x5554);
		RTL8168WriteOCP_GPHY(0x04, 0x4800);
		RTL8168WriteOCP_GPHY(0x04, 0x4000);
		RTL8168WriteOCP_GPHY(0x04, 0xF000);
		RTL8168WriteOCP_GPHY(0x03, 0xDF01);
		RTL8168WriteOCP_GPHY(0x02, 0xDF20);
		RTL8168WriteOCP_GPHY(0x01, 0x101A);
		RTL8168WriteOCP_GPHY(0x00, 0xA0FF);
		RTL8168WriteOCP_GPHY(0x04, 0xF800);
		RTL8168WriteOCP_GPHY(0x04, 0xF000);
		RTL8168WriteOCP_GPHY(0x1F, 0x0000);
		
		RTL8168WriteOCP_GPHY(0x1F, 0x0007);
		RTL8168WriteOCP_GPHY(0x1E, 0x0023);
		RTL8168WriteOCP_GPHY(0x16, 0x0000);
		RTL8168WriteOCP_GPHY(0x1F, 0x0000);
	}
}

void RealtekR1000::RTL8168SleepRxEnable()
{
	if ((mcfg == MCFG_8168B_1) ||
		(mcfg == MCFG_8168B_2))
	{
		WriteMMIO8(ChipCmd, CmdReset);
		// Turn off OWN bit in Rx descriptors
		R1000InitRxDescCmds(false);
		WriteMMIO8(ChipCmd, CmdRxEnb);
	}
}

void RealtekR1000::RTL8168PowerDownPLL()
{
	DLog("RTL8168PowerDownPLL\n");
	if (mcfg == MCFG_8168DP_1)
		return;
	if (((mcfg == MCFG_8168CP_1) || (mcfg == MCFG_8168CP_2))
		&& (ReadMMIO16(CPlusCmd) & ASF))
		return;
	
	if (wol_enabled == WOL_ENABLED)
		return;
	
	RTL8168PowerDownPHY();
	
	switch (mcfg) 
	{
		case MCFG_8168D_1:
		case MCFG_8168D_2:
			WriteMMIO8(PMCH, ReadMMIO8(PMCH) & ~BIT_7);
			break;
	}	
}

void RealtekR1000::RTL8168PowerUpPLL()
{
	DLog("RTL8168PowerUpPLL\n");
	if (mcfg == MCFG_8168DP_1)
		return;
	
	switch (mcfg) {
		case MCFG_8168D_1:
		case MCFG_8168D_2:
			WriteMMIO8(PMCH, ReadMMIO8(PMCH) | BIT_7);
			break;
	}
	
	RTL8168PowerUpPHY();
	RTL8168SetMedium(autoneg, speed, duplex);
}


void RealtekR1000::RTL8168PowerDownPHY()
{
	DLog("RTL8168PowerDownPHY\n");
	WriteGMII16(0x1F, 0x0000);
	WriteGMII16(0x0E, 0x0200);
	WriteGMII16(PHY_BMCR, PHY_Power_Down);
}

void RealtekR1000::RTL8168PowerUpPHY()
{
	DLog("RTL8168PowerUpPHY\n");
	WriteGMII16(0x1F, 0x0000);
	WriteGMII16(0x0E, 0x0000);
	WriteGMII16(PHY_BMCR, PHY_Enable_Auto_Nego);
}


void RealtekR1000::RTL8168SetMedium(ushort speedIn, uchar duplexIn, uchar autonegIn)
{
	u16 auto_nego = 0;
	u16 giga_ctrl = 0;
	
	if ((speedIn != SPEED_1000) && 
	    (speedIn != SPEED_100) && 
	    (speedIn != SPEED_10)) 
	{
		speedIn = SPEED_1000;
		duplexIn = DUPLEX_FULL;
	}
	
	if ((autonegIn == AUTONEG_ENABLE) ||
		(speedIn == SPEED_1000))
	{
		/*n-way force*/
		if ((speedIn == SPEED_10) && (duplexIn == DUPLEX_HALF))
		{
			auto_nego |= PHY_Cap_10_Half;
		}
		else if ((speedIn == SPEED_10) && (duplexIn == DUPLEX_FULL)) 
		{
			auto_nego |= PHY_Cap_10_Half |
			PHY_Cap_10_Full;
		}
		else if ((speedIn == SPEED_100) && (duplexIn == DUPLEX_HALF))
		{
			auto_nego |= PHY_Cap_100_Half | 
			PHY_Cap_10_Half | 
			PHY_Cap_10_Full;
		}
		else if ((speedIn == SPEED_100) && (duplexIn == DUPLEX_FULL))
		{
			auto_nego |= PHY_Cap_100_Half | 
			PHY_Cap_100_Full |
			PHY_Cap_10_Half | 
			PHY_Cap_10_Full;
		}
		else if (speedIn == SPEED_1000) 
		{
			giga_ctrl |= PHY_Cap_1000_Half | 
			PHY_Cap_1000_Full;
			
			auto_nego |= PHY_Cap_100_Half | 
			PHY_Cap_100_Full |
			PHY_Cap_10_Half | 
			PHY_Cap_10_Full;
		}
		
		//disable flow control
		auto_nego &= ~PHY_Cap_PAUSE;
		auto_nego &= ~PHY_Cap_ASYM_PAUSE;
		
		// save for later
		autoneg = autonegIn;
		speed = speedIn; 
		duplex = duplexIn; 
				
		RTL8168PowerUpPHY();
		
		WriteGMII16(0x1f, 0x0000);
		WriteGMII16(PHY_AUTO_NEGO_REG, auto_nego);
		WriteGMII16(PHY_1000_CTRL_REG, giga_ctrl);
		WriteGMII16(PHY_BMCR, BMCR_RESET | BMCR_ANENABLE | BMCR_ANRESTART);
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
	
	if (mcfg == MCFG_8168DP_1)
	{
		if (speedIn == SPEED_10)
		{
			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(0x10, 0x04EE);
		} 
		else
		{
			WriteGMII16(0x1F, 0x0000);
			WriteGMII16(0x10, 0x01EE);
		}
	}
}


void RealtekR1000::RTL8168DSM(int dev_state)
{
	DLog("RTL8168DSM\n");
	switch (dev_state) 
	{
		case DSM_MAC_INIT:
			if ((mcfg == MCFG_8168C_2) ||
				(mcfg == MCFG_8168C_3)) 
			{
				if (ReadMMIO8(MACDBG) & 0x80) 
				{
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) | GPIO_en);
				}
				else
				{
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) & ~GPIO_en);
				}
			}
			
			break;
			
		case DSM_NIC_GOTO_D3:
		case DSM_IF_DOWN:
			if ((mcfg == MCFG_8168C_2) || 
				(mcfg == MCFG_8168C_3))
				if (ReadMMIO8(MACDBG) & 0x80)
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) & ~GPIO_en);
			
			break;
			
		case DSM_NIC_RESUME_D3:
		case DSM_IF_UP:
			if ((mcfg == MCFG_8168C_2) ||
				(mcfg == MCFG_8168C_3))
				if (ReadMMIO8(MACDBG) & 0x80)
					WriteMMIO8(GPIO, ReadMMIO8(GPIO) | GPIO_en);
			
			break;
	}
	
}

int RealtekR1000::RTL8168ReadERI(int addr,
								 int len,
								 int type)
{
	int i, val_shift, shift = 0;
	u32 value1 = 0, value2 = 0, mask;
	
	if (len > 4 || len <= 0)
		return -1;
	
	while (len > 0)
	{
		val_shift = addr % ERIAR_Addr_Align;
		addr = addr & ~0x3;
		
		WriteMMIO32(ERIAR, 
					ERIAR_Read |
					type << ERIAR_Type_shift |
					ERIAR_ByteEn << ERIAR_ByteEn_shift |
					addr);
		
		for (i = 0; i < 10; i++)
		{
			IODelay(100);
			
			/* Check if the RTL8168 has completed ERI read */
			if (ReadMMIO32(ERIAR) & ERIAR_Flag) 
				break;
		}
		
		if (len == 1)		mask = (0xFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 2)	mask = (0xFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 3)	mask = (0xFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else			mask = (0xFFFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		
		value1 = ReadMMIO32(ERIDR) & mask;
		value2 |= (value1 >> val_shift * 8) << shift * 8;
		
		if (len <= 4 - val_shift)
			len = 0;
		else
		{
			len -= (4 - val_shift);
			shift = 4 - val_shift;
			addr += 4;
		}
	}
	
	return value2;
}

int RealtekR1000::RTL8168WriteERI(int addr,
								  int len,
								  int value,
								  int type)
{
	
	int i, val_shift, shift = 0;
	u32 value1 = 0, mask;
	
	if (len > 4 || len <= 0)
		return -1;
	
	while(len > 0)
	{
		val_shift = addr % ERIAR_Addr_Align;
		addr = addr & ~0x3;
		
		if (len == 1)		mask = (0xFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 2)	mask = (0xFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else if (len == 3)	mask = (0xFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		else			mask = (0xFFFFFFFF << (val_shift * 8)) & 0xFFFFFFFF;
		
		value1 = RTL8168ReadERI(addr, 4, type) & ~mask;
		value1 |= (((value << val_shift * 8) >> shift * 8));
		
		WriteMMIO32(ERIDR, value1);
		WriteMMIO32(ERIAR, 
					ERIAR_Write |
					type << ERIAR_Type_shift |
					ERIAR_ByteEn << ERIAR_ByteEn_shift |
					addr);
		
		for (i = 0; i < 10; i++)
		{
			IODelay(100);
			
			/* Check if the RTL8168 has completed ERI write */
			if (!(ReadMMIO32(ERIAR) & ERIAR_Flag)) 
				break;
		}
		
		if (len <= 4 - val_shift)
			len = 0;
		else
		{
			len -= (4 - val_shift);
			shift = 4 - val_shift;
			addr += 4;
		}
	}
	
	return 0;
}

void RealtekR1000::RTL8168WriteOCP_GPHY(int RegAddr, u16 value)
{
	WriteMMIO32(OCPDR, OCPDR_Write |
				(RegAddr & OCPDR_Reg_Mask) << OCPDR_GPHY_Reg_shift |
				(value & OCPDR_Data_Mask));
	WriteMMIO32(OCPAR, OCPAR_GPHY_Write);
	WriteMMIO32(EPHY_RXER_NUM, 0);
	
	for (int ocp_wcnt = 0; ocp_wcnt < 100; ocp_wcnt++)
	{
		IOSleep(2);
		
		if (!(ReadMMIO32(OCPAR) & OCPAR_Flag))
			break;
	}
}

u16 RealtekR1000::RTL8168ReadOCP_GPHY(int RegAddr)
{
	u16 value = 0xFFFF;
	
	WriteMMIO32(OCPDR, OCPDR_Read |
				(RegAddr & OCPDR_Reg_Mask) << OCPDR_GPHY_Reg_shift);
	WriteMMIO32(OCPAR, OCPAR_GPHY_Write);
	WriteMMIO32(EPHY_RXER_NUM, 0);
	
	for (int ocp_rcnt = 0; ocp_rcnt < 100; ocp_rcnt++)
	{
		IOSleep(2);
		
		if (!(ReadMMIO32(OCPAR) & OCPAR_Flag))
			break;
	}
	
	WriteMMIO32(OCPAR, OCPAR_GPHY_Read);
	value = static_cast<u16>(ReadMMIO32(OCPDR) & OCPDR_Data_Mask);
	
	return value;
}

u8 RealtekR1000::RTL8168ReadEfuse(u16 reg)
{
	if (efuse == EFUSE_NOT_SUPPORT)
		return EFUSE_READ_FAIL;
	
	u32 temp = EFUSE_READ | ((reg & EFUSE_Reg_Mask) << EFUSE_Reg_Shift);
	WriteMMIO32(EFUSEAR, temp);
	
	u8 efuse_data;
	int cnt = 0;
	do {
		IODelay(100);
		temp = ReadMMIO32(EFUSEAR);
		cnt++;
	} while (!(temp & EFUSE_READ_OK) && (temp < EFUSE_Check_Cnt));
	
	if (temp == EFUSE_Check_Cnt)
		efuse_data = EFUSE_READ_FAIL;
	else
		efuse_data = static_cast<u8>(ReadMMIO32(EFUSEAR) & EFUSE_Data_Mask);
	
	return efuse_data;
}
