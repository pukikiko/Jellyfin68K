/*----------------------------------------------------------
#
#	finger -- MPW Tool
#
#	Written by Steven Falkenburg
#
#-----------------------------------------------------------
#
#	TCPHi.c
#
#	This module contains high-level routines for setting
#	up connections using MacTCP and sending/receiving data
#	on these connections.
#
#-----------------------------------------------------------*/

#include <string.h>
#include <MacTCP.h>
#include "TCPHi.h"

/* MT_InitNetwork opens the network driver
*/

OSErr MT_InitNetwork(void)
{
	return OpenTCPDriver();
}


/* MT_CreateStream() creates an unconnected network stream to be
   used later by MT_OpenConnection.  The length of the receive
   buffer must be specified in the call */
   
OSErr MT_CreateStream(StreamPtr *stream,unsigned long recvLen, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	Ptr recvPtr;
	OSErr err;
	
	recvPtr = NewPtr(recvLen);
	err = MemError();
	if (err==noErr)
		err = LowTCPCreateStream(stream,recvPtr,recvLen,(TCPNotifyProcPtr)nil, giveTimePtr, cancel);
	return err;
}


/* MT_OpenConnection() initiates a connection to a remote machine,
   given that machine's network number and connection port.  A timeout
   value for the call must be given, and the stream identifier is returned. */

OSErr MT_OpenConnection(StreamPtr stream,long remoteHost,short remotePort,Byte timeout, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	ip_addr localHost;
	tcp_port localPort = 0;
	
	return LowTCPOpenConnection(stream,timeout,remoteHost,remotePort,&localHost,
								&localPort, giveTimePtr, cancel);
}


/* MT_WaitForConnection() listens for a connection on a particular port from a
	particular host.  It returns when a connection has been established */

OSErr MT_WaitForConnection(StreamPtr stream,Byte timeout,short localPort,
						long *remoteHost,short *remotePort, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	ip_addr localHost;
	
	return LowTCPWaitForConnection(stream,timeout,(ip_addr *)remoteHost,
				(tcp_port *)remotePort,&localHost,(tcp_port *)&localPort,false,nil, giveTimePtr, cancel);
}


/* MT_AsyncWaitForConnection() listens for a connection on a particular port from a
	particular host.  It is executed asynchronously and returns immediately */

void MT_AsyncWaitForConnection(StreamPtr stream,Byte timeout,short localPort,
				long remoteHost,short remotePort,TCPiopb **returnBlock, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	ip_addr localHost;
	
	LowTCPWaitForConnection(stream,timeout,(ip_addr *)&remoteHost,
				(tcp_port *)&remotePort,&localHost,(tcp_port *)&localPort,true,returnBlock, giveTimePtr, cancel);
}


/* MT_AsyncGetConnectionData() should be called when a call to MT_AsyncWaitForConnection
	completes (when returnBlock->ioResult <= 0).  This call retrieves the information
	about this new connection and disposes the parameter block. */
	
OSErr MT_AsyncGetConnectionData(TCPiopb *returnBlock,long *remoteHost,short *remotePort)
{
	ip_addr localHost;
	tcp_port localPort;
	
	return LowFinishTCPWaitForConn(returnBlock,(ip_addr *)remoteHost,
						(tcp_port *)remotePort,&localHost,&localPort);
}


/* MT_CloseConnection() terminates a connection to a remote host, given the
   stream identifier of the connection */
   
OSErr MT_CloseConnection(StreamPtr stream, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	return LowTCPClose(stream,10, giveTimePtr, cancel);
}


/* MT_AbortConnection() aborts a connection to a remote host, given the
   stream identifier of the connection */
   
OSErr MT_AbortConnection(StreamPtr stream, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	return LowTCPAbort(stream, giveTimePtr, cancel);
}
	

/* MT_ReleaseStream() frees the allocated buffer space for a given connection
   stream.  This call should be made after MT_CloseConnection. */
   
OSErr MT_ReleaseStream(StreamPtr stream, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	OSErr err;
	Ptr recvPtr;
	unsigned long recvLen;
	
	if ((err = LowTCPRelease(stream,&recvPtr,&recvLen, giveTimePtr, cancel)) == noErr)
			DisposePtr(recvPtr);
	
	return err;
}


/* MT_SendData() sends data along a connection stream to a remote host. */

OSErr MT_SendData(StreamPtr stream,Ptr data,unsigned short length,Boolean retry, GiveTimePtr giveTimePtr, Boolean* cancel)
{	
	OSErr err;
	struct wdsEntry myWDS[2];	/* global write data structure */

	myWDS[0].length = length;
	myWDS[0].ptr = data;
	myWDS[1].length = 0;
	myWDS[1].ptr = nil;
	do
		err = LowTCPSendData(stream,20,false,false,(Ptr) myWDS,false,nil, giveTimePtr, cancel);
	while (retry && err==commandTimeout);
	return err;
}

/* MT_SendMultiData() is similar to MT_SendData, but takes an array of strings to send
   to the remote host. */

OSErr MT_SendMultiData(StreamPtr stream,Str255 data[],short numData,Boolean retry, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	struct wdsEntry *theWDS;
	short i;
	OSErr err;
	
	theWDS = (wdsEntry *)NewPtr((numData+1) * sizeof(wdsEntry));
	if (MemError())
		return MemError();	
	theWDS[numData].length = 0;
	theWDS[numData].ptr = nil;
	for (i=0; i<numData; i++) {
		theWDS[i].ptr = data[i];
		theWDS[i].length = strlen((char *)data[i]);
	}
	do
		err = LowTCPSendData(stream,20,false,false,(Ptr) theWDS,false,nil, giveTimePtr, cancel);
	while (retry && err==commandTimeout);
	DisposePtr((Ptr)theWDS);
	return err;
}


/* MT_SendDataAsync() sends data to a remote host asynchronously.  The ioResult
   parameter block variable should be checked, and SendDataDone() called when
   this flag is zero or negative */

void MT_SendDataAsync(StreamPtr stream, Ptr data,unsigned short length,TCPiopb **returnBlock, GiveTimePtr giveTimePtr, Boolean* cancel)
{	
	struct wdsEntry *theWDS;

	theWDS = (wdsEntry *)NewPtr( (2*sizeof(wdsEntry)) );
	theWDS[0].length = length;
	theWDS[0].ptr = data;
	theWDS[1].length = 0;
	theWDS[1].ptr = 0;
	LowTCPSendData(stream,20,false,false,(Ptr) theWDS,true,returnBlock, giveTimePtr, cancel);
}


/* SendDataDone() should be called in response to the completion of a MT_SendDataAsync
   call.  It returns any error which occurred in the send. */

OSErr MT_SendAsyncDone(TCPiopb *returnBlock)
{
	DisposePtr((Ptr)returnBlock->csParam.send.wdsPtr);
	return LowFinishTCPSend(returnBlock);
}


/* MT_RecvData() waits for data to be received on a connection stream.  When data
   arrives, it is copied into the data buffer and the call terminates. */

OSErr MT_RecvData(StreamPtr stream,Ptr data,unsigned short *length,Boolean retry, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	Boolean	urgent,mark;
	OSErr	err;
	unsigned short recvLength;

	do {
		recvLength = *length;
		err = LowTCPRecvData(stream,20,&urgent,&mark,data,&recvLength,false,nil, giveTimePtr, cancel);
	}
	while (retry && err==commandTimeout);
	*length = recvLength;
	if (err == noErr) {
		*(data+*length) = '\0';
	}
	return err;
}


/* MT_RecvDataAsync() is identical to MT_RecvData above, but in this case, the call is
   made asynchronously. */

void MT_RecvDataAsync(StreamPtr stream,Ptr data,unsigned short length,TCPiopb **returnBlock, GiveTimePtr giveTimePtr, Boolean* cancel)
{
	Boolean urgent,mark;
	
	LowTCPRecvData(stream,20,&urgent,&mark,data,&length,true,returnBlock, giveTimePtr, cancel);
}


/* MT_GetDataLength() should be called in response to the completion of the
   MT_RecvDataAsync call. */

OSErr MT_GetDataLength(TCPiopb *returnBlock,unsigned short *length)
{
	Boolean urgent,mark;
	
	return LowFinishTCPRecv(returnBlock,&urgent,&mark,length);
}
