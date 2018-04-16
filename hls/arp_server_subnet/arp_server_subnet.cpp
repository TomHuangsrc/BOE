/************************************************
Copyright (c) 2016, Xilinx, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, 
this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, 
this list of conditions and the following disclaimer in the documentation 
and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors 
may be used to endorse or promote products derived from this software 
without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, 
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, 
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, 
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, 
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.// Copyright (c) 2015 Xilinx, Inc.
************************************************/

#include "arp_server_subnet.hpp"

/** @ingroup arp_server
 *
 */
void arp_pkg_receiver(stream<axiWord>&        arpDataIn,
                      stream<arpReplyMeta>&   arpReplyMetaFifo,
                      stream<arpTableEntry>&  arpTableInsertFifo,
                      ap_uint<32>             myIpAddress)
{
#pragma HLS PIPELINE II=1
  static uint16_t 		wordCount = 0;
  static ap_uint<16>	opCode;
  static ap_uint<32>	protoAddrDst;
  static ap_uint<32>	inputIP;
  static arpReplyMeta meta;

  axiWord currWord;

  currWord.last = 0; //probably not necessary
  if (!arpDataIn.empty() && !arpReplyMetaFifo.full() && !arpTableInsertFifo.full())
	{
		arpDataIn.read(currWord);

		switch(wordCount)
		{
		//TODO DO MAC ADDRESS Filtering somewhere, should be done in/after mac
			case 0:
				//MAC_DST = currWord.data(47, 0);
				meta.srcMac(15, 0) = currWord.data(63, 48);
				break;
			case 1:
				meta.srcMac(47 ,16) = currWord.data(31, 0);
				meta.ethType = currWord.data(47, 32);
				meta.hwType = currWord.data(63, 48);
				break;
			case 2:
				meta.protoType = currWord.data(15, 0);
				meta.hwLen = currWord.data(23, 16);
				meta.protoLen = currWord.data(31, 24);
				opCode = currWord.data(47, 32);
				meta.hwAddrSrc(15,0) = currWord.data(63, 48);
				break;
			case 3:
				meta.hwAddrSrc(47, 16) = currWord.data(31, 0);
				meta.protoAddrSrc = currWord.data(63, 32);
				break;
			case 4:
				//hwAddrDst = currWord.data(47, 0);
				protoAddrDst(15, 0) = currWord.data(63, 48);
				break;
			case 5:
				protoAddrDst(31, 16) = currWord.data(15, 0);
				break;
			default:
				break;
		} //switch
		if (currWord.last == 1)
		{
			if ((opCode == REQUEST) && (protoAddrDst == myIpAddress))
			{
			  // Trigger ARP reply
			  arpReplyMetaFifo.write(meta);
				//arpState = ARP_REPLY;
			}
			else
			{
				if ((opCode == REPLY) && (protoAddrDst == myIpAddress))
				{
					arpTableInsertFifo.write(arpTableEntry(meta.protoAddrSrc, meta.hwAddrSrc, true));
				}
				//arpState = ARP_IDLE;
			}
			wordCount = 0;
		}
		else
		{
			wordCount++;
		}
	}
}

/** @ingroup arp_server
 *
 */
void arp_pkg_sender(stream<arpReplyMeta>&     arpReplyMetaFifo,
                    stream<ap_uint<32> >&     arpRequestMetaFifo,
                    stream<axiWord>&          arpDataOut,
                    ap_uint<48>				  myMacAddress,
                    ap_uint<32>               myIpAddress)
{
#pragma HLS PIPELINE II=1
  enum arpSendStateType {ARP_IDLE, ARP_REPLY, ARP_SENTRQ};
  static arpSendStateType aps_fsmState = ARP_IDLE;

  static uint16_t			sendCount = 0;
  static arpReplyMeta		replyMeta;
  static ap_uint<32>		inputIP;

  axiWord sendWord;

  switch (aps_fsmState)
  {
   case ARP_IDLE:
   sendCount = 0;
    if (!arpReplyMetaFifo.empty())
    {
      arpReplyMetaFifo.read(replyMeta);
      aps_fsmState = ARP_REPLY;
    }
    else if (!arpRequestMetaFifo.empty())
    {
      arpRequestMetaFifo.read(inputIP);
      aps_fsmState = ARP_SENTRQ;
    }
    break;
  case ARP_SENTRQ:
	  if(!arpDataOut.full())
	  {
		switch(sendCount)
		{
			case 0:
				sendWord.data(47, 0)  = BROADCAST_MAC;
				sendWord.data(63, 48) = myMacAddress(15, 0);
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 1:
				sendWord.data(31, 0)  = myMacAddress(47, 16);
				sendWord.data(47, 32) = 0x0608;
				sendWord.data(63, 48) = 0x0100;
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 2:
				sendWord.data(15, 0)  = 0x0008;								// IP Address
				sendWord.data(23, 16) = 6;									// HW Address Length
				sendWord.data(31, 24) = 4;									// Protocol Address Length
				sendWord.data(47, 32) = REQUEST;
				sendWord.data(63, 48) = myMacAddress(15, 0);
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 3:
				sendWord.data(31, 0)  = myMacAddress(47, 16);
				sendWord.data(63, 32) = myIpAddress;//MY_IP_ADDR;
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 4:
				sendWord.data(47, 0)  = 0;										// Sought-after MAC pt.1
				sendWord.data(63, 48) = inputIP(15, 0);
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 5:
				sendWord.data(63, 16) = 0;									// Sought-after MAC pt.1
				sendWord.data(15, 0)  = inputIP(31, 16);
				sendWord.keep = 0x03;
				sendWord.last = 1;
				aps_fsmState = ARP_IDLE;
				break;
		} //switch sendcount
		arpDataOut.write(sendWord);
		sendCount++;
	  }
		break;
  case ARP_REPLY:
	  if(!arpDataOut.full())
	  {
		  switch(sendCount)
			{
			case 0:
				sendWord.data(47, 0)  = replyMeta.srcMac;
				sendWord.data(63, 48) = myMacAddress(15, 0);
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 1:
				sendWord.data(31, 0) = myMacAddress(47, 16);
				sendWord.data(47, 32) = replyMeta.ethType;
				sendWord.data(63, 48) = replyMeta.hwType;
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 2:
				sendWord.data(15, 0)  = replyMeta.protoType;
				sendWord.data(23, 16) = replyMeta.hwLen;
				sendWord.data(31, 24) = replyMeta.protoLen;
				sendWord.data(47, 32) = REPLY;
				sendWord.data(63, 48) = myMacAddress(15, 0);
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 3:
				sendWord.data(31, 0)  = myMacAddress(47, 16);
				sendWord.data(63, 32) = myIpAddress;//MY_IP_ADDR, maybe use proto instead
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 4:
				sendWord.data(47, 0)  = replyMeta.hwAddrSrc;
				sendWord.data(63, 48) = replyMeta.protoAddrSrc(15, 0);
				sendWord.keep = 0xff;
				sendWord.last = 0;
				break;
			case 5:
				sendWord.data(63, 16) = 0;
				sendWord.data(15, 0) = replyMeta.protoAddrSrc(31, 16);
				sendWord.keep = 0x03;
				sendWord.last = 1;
				aps_fsmState = ARP_IDLE;
				break;
			} //switch
			arpDataOut.write(sendWord);
			sendCount++;
	  }
			break;
  } //switch
}

/** @ingroup arp_server
 *
 */
void arp_table( stream<arpTableEntry>&    	arpTableInsertFifo,
                stream<ap_uint<32> >&     	macIpEncode_req,
                stream<arpTableReply>&		macIpEncode_rsp,
                stream<ap_uint<32> >&		arpRequestMetaFifo)
{
#pragma HLS PIPELINE II=1

	static arpTableEntry		arpTable[256];
	#pragma HLS RESOURCE variable=arpTable core=RAM_1P_BRAM
	#pragma HLS DEPENDENCE variable=arpTable inter false

	ap_uint<32>			query_ip;
	arpTableEntry		currEntry;


	if (!arpTableInsertFifo.empty())
	{
		arpTableInsertFifo.read(currEntry);
		arpTable[currEntry.ipAddress(31,24)] = currEntry;
	}
	else if (!macIpEncode_req.empty() && !arpRequestMetaFifo.full() && !macIpEncode_rsp.full())
	{
		macIpEncode_req.read(query_ip);
		currEntry = arpTable[query_ip(31,24)];
		if (!currEntry.valid)
		{
			// send ARP request
			arpRequestMetaFifo.write(query_ip);
		}
		macIpEncode_rsp.write(arpTableReply(currEntry.macAddress, currEntry.valid));
	}

}

/** @ingroup arp_server
 *
 */
void arp_server_subnet(	stream<axiWord>&          arpDataIn,
                  	  	stream<ap_uint<32> >&     macIpEncode_req,
				        stream<axiWord>&          arpDataOut,
				        stream<arpTableReply>&    macIpEncode_rsp,
				        ap_uint<48> myMacAddress,
				        ap_uint<32> myIpAddress)
{
	#pragma HLS INTERFACE ap_ctrl_none port=return
	#pragma HLS DATAFLOW

	#pragma  HLS resource core=AXI4Stream variable=arpDataIn metadata="-bus_bundle s_axis"
	#pragma  HLS resource core=AXI4Stream variable=arpDataOut metadata="-bus_bundle m_axis"
	#pragma  HLS resource core=AXI4Stream variable=macIpEncode_req metadata="-bus_bundle s_axis_arp_lookup_request"
	#pragma  HLS resource core=AXI4Stream variable=macIpEncode_rsp metadata="-bus_bundle m_axis_arp_lookup_reply"
  #pragma HLS DATA_PACK variable=macIpEncode_rsp

	#pragma HLS INTERFACE ap_none register port=myMacAddress
	#pragma HLS INTERFACE ap_none register port=myIpAddress

  static stream<arpReplyMeta>     arpReplyMetaFifo("arpReplyMetaFifo");
  #pragma HLS STREAM variable=arpReplyMetaFifo depth=4
  #pragma HLS DATA_PACK variable=arpReplyMetaFifo

  static stream<ap_uint<32> >   arpRequestMetaFifo("arpRequestMetaFifo");
  #pragma HLS STREAM variable=arpRequestMetaFifo depth=4
  //#pragma HLS DATA_PACK variable=arpRequestMetaFifo

  static stream<arpTableEntry>    arpTableInsertFifo("arpTableInsertFifo");
  #pragma HLS STREAM variable=arpTableInsertFifo depth=4
  #pragma HLS DATA_PACK variable=arpTableInsertFifo

  arp_pkg_receiver(arpDataIn, arpReplyMetaFifo, arpTableInsertFifo, myIpAddress);

  arp_pkg_sender(arpReplyMetaFifo, arpRequestMetaFifo, arpDataOut, myMacAddress, myIpAddress);

  arp_table(arpTableInsertFifo, macIpEncode_req, macIpEncode_rsp, arpRequestMetaFifo);
}
