#include "TCPRoutines.h"

#ifndef _TCPLOW_
#define _TCPLOW_

/* network initialization ------------------------------------------------------*/

OSErr MT_InitNetwork(void);		/* opens the network driver */


/* connection stream creation/removal -------------------------------------------*/

OSErr MT_CreateStream(				/* creates a stream needed to establish a connection*/
	StreamPtr *stream,			/* stream identifier (returned)					*/
	unsigned long recvLen,			/* stream buffer length to be allocated			*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);
	
OSErr MT_ReleaseStream(			/* disposes of an unused stream and its buffers		*/
	StreamPtr stream,			/* stream identifier to dispose					*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);


/* connection opening/closing calls ---------------------------------------------*/

OSErr MT_OpenConnection(			/* attempts to establish a connection w/remote host */
	StreamPtr stream,			/* stream id to be used for connection			*/
	long remoteHost,				/* network number of remote host				*/
	short remotePort,				/* network port of remote port					*/
	Byte timeout,					/* timeout value for connection					*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);
	
OSErr MT_WaitForConnection(		/* listens for a remote connection from a rem. port */
	StreamPtr stream,			/* stream id to be used for connection			*/
	Byte timeout,					/* timeout value for open						*/
	short localPort,				/* local port to listen on						*/
	long *remoteHost,				/* remote host connected to (returned)			*/
	short *remotePort,				/* remote port connected to (returned)			*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);

void MT_AsyncWaitForConnection(	/* same as above, except executed asynchronously	*/
	StreamPtr stream,			/* stream id to be used for connection			*/
	Byte timeout,					/* timeout value for open						*/
	short localPort,				/* local port to listen on						*/
	long remoteHost,				/* remote host to listen for					*/
	short remotePort,				/* remote port to listen for					*/
	TCPiopb **returnBlock,			/* parameter block for call (returned)			*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);

OSErr MT_AsyncGetConnectionData(	/* retrieves connection data for above call			*/
	TCPiopb *returnBlock,			/* parameter block for asyncwait call			*/
	long *remoteHost,				/* remote host connected to (returned)			*/
	short *remotePort);				/* remote port connected to (returned)			*/

OSErr MT_CloseConnection(			/* closes an established connection					*/
	StreamPtr stream,			/* stream id of stream used for connection		*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);
	
OSErr MT_AbortConnection(			/* aborts a connection non-gracefully				*/
	StreamPtr stream,			/* stream id of stream used for connection		*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);


/* data sending calls ----------------------------------------------------------*/

OSErr MT_SendData(					/* sends data along an open connection				*/
	StreamPtr stream,			/* stream used for connection					*/
	Ptr data,						/* pointer to data to send						*/
	unsigned short length,			/* length of data to send						*/
	Boolean retry,					/* if true, call continues until send successful*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);
	
OSErr MT_SendMultiData(			/* sends multiple strings of data on a connection	*/
	StreamPtr stream,			/* stream used for connection					*/
	Str255 data[],					/* array of send strings						*/	
	short numData,					/* number of strings to send					*/
	Boolean retry,					/* if true, call continues until send successful*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);

void MT_SendDataAsync(				/* sends data asynchronously						*/
	StreamPtr stream,			/* stream used for connection					*/
	Ptr data,						/* pointer to data to send						*/
	unsigned short length,			/* length of data to send						*/
	TCPiopb **returnBlock,			/* pointer to parameter block (returned)		*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);
	
OSErr MT_SendAsyncDone(			/* called when MT_SendDataAsync call completes			*/
	TCPiopb *returnBlock);			/* parameter block to complete connection		*/


/* data receiving calls --------------------------------------------------------*/

OSErr MT_RecvData(					/* waits for data to be received on a connection	*/
	StreamPtr stream,			/* stream used for connection					*/ 
	Ptr data,						/* pointer to memory used to hold incoming data	*/
	unsigned short *length,			/* length to data received (returned)			*/
	Boolean retry,					/* if true, call continues until successful		*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);
	
void MT_RecvDataAsync(				/* receives data asynchronously						*/
	StreamPtr stream,			/* stream used for connection					*/
	Ptr data,						/* pointer to memory used to hold incoming data	*/
	unsigned short length,			/* length of data requested						*/
	TCPiopb **returnBlock,			/* parameter block to complete connection		*/
	GiveTimePtr giveTimePtr,
	Boolean* cancel);
	
OSErr MT_GetDataLength(			/* called when MT_RecvDataAsync completes				*/
	TCPiopb *returnBlock,			/* parameter block used for receive				*/
	unsigned short *length);		/* length of data received (returned)			*/

#endif //_TCPLOW_