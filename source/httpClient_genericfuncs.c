/*
*	Copyright (C) 2013
*	Chair of Process Control Engineering,
*	Aachen University of Technology.
*	All rights reserved.
*
*	Redistribution and use in source and binary forms, with or without
*	modification, are permitted provided that the following conditions
*	are met:
*	1. Redistributions of source code must retain the above copyright
*	   notice, this list of conditions and the following disclaimer.
*	2. Redistributions in binary form must print or display the above
*	   copyright notice either during startup or must have a means for
*	   the user to view the copyright notice.
*	3. Redistributions in binary form must reproduce the above copyright
*	   notice, this list of conditions and the following disclaimer in
*		the documentation and/or other materials provided with the
*		distribution.
*	4. Neither the name of the Chair of Process Control Engineering nor
*		the name of the Aachen University of Technology may be used to
*		endorse or promote products derived from this software without
*		specific prior written permission.
*
*	THIS SOFTWARE IS PROVIDED BY THE CHAIR OF PROCESS CONTROL ENGINEERING
*	``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*	LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
*	A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE CHAIR OF
*	PROCESS CONTROL ENGINEERING BE LIABLE FOR ANY DIRECT, INDIRECT,
*	INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*	BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
*	OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
*	AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*	LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
*	WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*	POSSIBILITY OF SUCH DAMAGE.
*
***********************************************************************/
#ifndef OV_COMPILE_LIBRARY_kshttp
#define OV_COMPILE_LIBRARY_kshttp
#endif

#include "kshttp.h"
#include "libov/ov_macros.h"
#include "libov/ov_malloc.h"
#include "libov/ov_result.h"
#include "ov_ksserver_backend.h"
#include "config.h"
#include "ks_logfile.h"
#include "ksbase_helper.h"




/*******************************************************************************************************************************************************************************
 * 				Channel-handling
 *******************************************************************************************************************************************************************************/

OV_RESULT getChannelPointer(OV_INSTPTR_kshttp_httpClient this, OV_INSTPTR_ksbase_Channel* ppChannel, OV_VTBLPTR_ksbase_Channel* ppVtblChannel)
{
	OV_STRING OptValTemp = NULL;
	OV_INSTPTR_ov_class pClassChannel = NULL;
	OV_RESULT result;

	/*	check if there is a channel and get pointer to it	*/
	*ppChannel = Ov_GetParent(ksbase_AssocChannelDataHandler, this);
	if(!(*ppChannel))
	{/*	no Channel found. Create one	*/
		/*
		 * Checking KSHTTP_USECHANNEL and KS_USECHANNEL options to determine which channel type to use. use TCPChannel per default.
		 */
		ov_memstack_lock();
		OptValTemp = ov_vendortree_getcmdlineoption_value("KSHTTP_USECHANNEL");
		if(!OptValTemp)
			OptValTemp = ov_vendortree_getcmdlineoption_value("KS_USECHANNEL");

		if(!OptValTemp)
		{/*	Neither option specified, use TCPChannel as default	*/
			OptValTemp = ov_memstack_alloc(sizeof("TCPChannel"));
			strcpy(OptValTemp, "TCPChannel");
		}

		if(*OptValTemp)
		{	/*	empty option means do not create channel	*/
			pClassChannel = Ov_StaticPtrCast(ov_class, Ov_GetFirstChild(ov_instantiation, pclass_ov_class));
			while(pClassChannel)
			{
				if(ov_string_compare(pClassChannel->v_identifier, OptValTemp) == OV_STRCMP_EQUAL)
					break;
				pClassChannel = Ov_StaticPtrCast(ov_class, Ov_GetNextChild(ov_instantiation, pClassChannel));
			}

			if(pClassChannel)
			{/*	channel found create it	*/
				result = ov_class_createobject(pClassChannel, Ov_StaticPtrCast(ov_domain, this), "httpchannel", OV_PMH_DEFAULT, NULL, NULL, NULL, (OV_INSTPTR_ov_object*) ppChannel);
				if(Ov_Fail(result))
				{
					KS_logfile_error(("%s getChannelPointer: could not create channel for manager communication. reason: %s", this->v_identifier, ov_result_getresulttext(result)));
					return result;
				}
				else
				{
					result = Ov_Link(ksbase_AssocChannelDataHandler, *ppChannel, this);
					if(Ov_Fail(result))
					{
						KS_logfile_error(("%s getChannelPointer: could not link with channel. reason: %s", this->v_identifier, ov_result_getresulttext(result)));
						return result;
					}
					(*ppChannel)->v_ClientHandlerAssociated = KSBASE_CH_NOTNEEDED;

				}
			}
		}
		else
		{
			ov_memstack_unlock();
			*ppChannel = NULL;
			*ppVtblChannel = NULL;
			return OV_ERR_OK;
		}
		ov_memstack_unlock();
	}


	Ov_GetVTablePtr(ksbase_Channel, *ppVtblChannel, *ppChannel);
	if(!(*ppVtblChannel))
	{
		KS_logfile_error(("%s: getChannelPointer: could not get VTable of channel object. Aborting.", this->v_identifier));
		return OV_ERR_BADFACTORY;
	}
	return OV_ERR_OK;
}

/*******************************************************************************************************************************************************************************
 * 				Connection handling and sending
 ******************************************************************************************************************************************************************************/

/*	if there is no connection (open or opening) open one and reset lasteventtime	*/
OV_RESULT initiateConnection(OV_INSTPTR_kshttp_httpClient this, OV_INSTPTR_ksbase_Channel pChannel, OV_VTBLPTR_ksbase_Channel pVtblChannel, OV_BOOL isLocal, OV_STRING host, OV_STRING port)
{
	OV_RESULT result;

	if((pChannel->v_ConnectionState != KSBASE_CONNSTATE_OPEN) && (pChannel->v_ConnectionState != KSBASE_CONNSTATE_OPENING))
	{
		if(!port)
			return OV_ERR_BADPARAM;

		if(isLocal)
			result = pVtblChannel->m_OpenLocalConn(pChannel, port);
		else
		{
			if(!host)
				return OV_ERR_BADPARAM;
			result = pVtblChannel->m_OpenConnection(pChannel, host, port);
		}

		if(Ov_Fail(result))
		{
			KS_logfile_error(("%s: initiateConnection: could not open connection", this->v_identifier));
			return result;
		}
		ov_time_gettime(&(this->v_timeLastEvent));
	}
	return OV_ERR_OK;
}

/*	check if connection is open. if so, send and set Client state to busy and reset lasteventtime. if not set client state to awaiting connection. activate typemethod	*/
OV_RESULT trySend(OV_INSTPTR_kshttp_httpClient thisCl, OV_INSTPTR_ksbase_Channel pChannel, OV_VTBLPTR_ksbase_Channel pVtblChannel)
{
	OV_RESULT result = OV_ERR_OK;

	if(pChannel->v_ConnectionState == KSBASE_CONNSTATE_OPEN)
	{
		result = pVtblChannel->m_SendData(pChannel);
		if(Ov_Fail(result))
		{
			ksbase_free_KSDATAPACKET(&(pChannel->v_outData));
			return result;
		}
		ov_time_gettime(&(thisCl->v_timeLastEvent));
		thisCl->v_state = KSBASE_CLST_AWAITINGANSWER;	/*	set state to busy and activate typemethod	*/
		thisCl->v_actimode = 1;
	}
	else
	{
		thisCl->v_state = KSBASE_CLST_AWAITINGCONNECTION;
		thisCl->v_actimode = 1;
	}
	return OV_ERR_OK;
}
/*******************************************************************************************************************************************************************************
 * 				header
 ******************************************************************************************************************************************************************************/
/**
 * creation of http request header (call)
 * @param datapacket: datapacket to write into
 * @return	errorcode
 */
OV_RESULT kshttp_generateClientMessageHeader(
		OV_STRING method,
		OV_STRING requestUri,
		const OV_INSTPTR_kshttp_httpClient this,
		const OV_INSTPTR_ksbase_ClientTicketGenerator TicketGenerator,
		const OV_INSTPTR_ov_domain	callbackThat,
		void (*callback)(const OV_INSTPTR_ov_domain this, const OV_INSTPTR_ov_domain that)
		)
{
	OV_RESULT result;
	OV_STRING RequestLine = NULL;
	OV_STRING RequestAdditionalHeaders = NULL;
	OV_STRING RequestHeaders = NULL;
	OV_INSTPTR_kshttp_httpClient thisCl = this;
	OV_INSTPTR_ksbase_Channel	pChannel = NULL;
	OV_VTBLPTR_ksbase_Channel	pVtblChannel = NULL;

	if(!requestUri){
		return OV_ERR_BADPARAM;
	}
	if(!method){
		return OV_ERR_BADPARAM;
	}

	if((thisCl->v_state != KSBASE_CLST_COMPLETED) && (thisCl->v_state != KSBASE_CLST_INITIAL))
		return OV_ERR_NOACCESS;

	/*	set callback function	*/
	thisCl->v_callback.instanceCalled = callbackThat;
	thisCl->v_callback.callbackFunction = callback;

	/*	get pointer to channel and to its Vtable	*/
	result = getChannelPointer(thisCl, &pChannel, &pVtblChannel);
	if(Ov_Fail(result))
	{
		KS_logfile_error(("%s: Could not get Channel pointers.", this->v_identifier));
		ksbase_free_KSDATAPACKET(&(pChannel->v_outData));
		return result;
	}
	if(!pChannel || !pVtblChannel)
	{
		KS_logfile_warning(("%s: No Channel found and none should be created. Will do nothing.", this->v_identifier));
		ksbase_free_KSDATAPACKET(&(pChannel->v_outData));
		return OV_ERR_OK;
	}

	ov_string_print(&RequestAdditionalHeaders, "Host:\r\nConnection: close\r\nUser-Agent: ACPLT/OV HTTP Client %s (compiled %s %s)\r\n", OV_LIBRARY_DEF_kshttp.version, __TIME__, __DATE__);
	ov_string_print(&RequestLine, "%s %s HTTP/1.1", method, requestUri);
	ov_string_print(&RequestHeaders, "%s\r\n%s\r\n", RequestLine, RequestAdditionalHeaders);

	result = ksbase_KSDATAPACKET_append(&(pChannel->v_outData), (OV_BYTE*) RequestHeaders, ov_string_getlength(RequestHeaders));
	ov_string_setvalue(&RequestAdditionalHeaders, NULL);
	ov_string_setvalue(&RequestLine, NULL);
	ov_string_setvalue(&RequestHeaders, NULL);
	if(Ov_Fail(result)){
		ksbase_free_KSDATAPACKET(&(pChannel->v_outData));
		return result;
	}

	/*	send created Message	*/
	result = initiateConnection(thisCl, pChannel, pVtblChannel, TRUE, NULL, thisCl->v_ManagerPort);

	if(Ov_Fail(result))
	{
		ksbase_free_KSDATAPACKET(&(pChannel->v_outData));
		return result;
	}

	result = trySend(thisCl, pChannel, pVtblChannel);
	if(Ov_Fail(result))
		return result;

	thisCl->v_actimode = 1;

	//thisCl->v_sentProcID = KS_REGISTER;

	return OV_ERR_OK;
}

/*************************************************************************************************************************************************************
 * 	decode header of replies (params are not checked for NULL-pointers)
 *************************************************************************************************************************************************************/
OV_RESULT kshttp_processServerReplyHeader(KS_DATAPACKET* dataReceived, KSHTTP_RESPONSE *responseStruct)
{
	OV_RESULT result;

	result = parse_http_header_from_server(dataReceived, responseStruct);


	/***************************************************************************************************************************************************************************
	 * checking if request is in the buffer completely, if not, return ERR_OK and wait until it is complete. While waiting set the receive timeout value to a few seconds
	 * 	(we received a fragmented package so there will come more data. if no data arrives within a second, we can assume the package is broken)
	 ***************************************************************************************************************************************************************************/
	if(result == OV_ERR_TARGETGENERIC)
	{
		return result;		/*	get called again to process the request next time (if it is complete then).
												Yes, this could block the ClientHandler for a longer time.	*/
	}

	return OV_ERR_OK;
}

void GetServerCallback(const OV_INSTPTR_ov_domain this, const OV_INSTPTR_ov_domain that)
{
	//OV_INSTPTR_kshttp_httpClient callingCl = Ov_StaticPtrCast(kshttp_httpClient, this);
	//OV_INSTPTR_kshttp_httpClient callbackOwner = Ov_StaticPtrCast(kshttp_httpClient, that);

	//OV_RESULT result;
}


/*
 * This function checks if the input buffer holds a complete request
 */
OV_RESULT parse_http_header_from_server(KS_DATAPACKET* dataReceived, KSHTTP_RESPONSE *responseStruct)
{
	OV_STRING *pallheaderslist=NULL;
	OV_STRING *plist=NULL;
	OV_STRING ResponseLine = NULL;
	OV_STRING *http_request_array = NULL;
	OV_UINT allheaderscount = 0;
	OV_UINT beginOfContent = 0;
	OV_UINT len = 0, i = 0;

	//split http header from content
	http_request_array = ov_string_split((OV_STRING)dataReceived->data, "\r\n\r\n", &len);
	beginOfContent = ov_string_getlength(http_request_array[0])+4;

	//seperate all headers
	pallheaderslist = ov_string_split(http_request_array[0], "\r\n", &allheaderscount);
	ov_string_freelist(http_request_array);
	if(allheaderscount<=0){
		ov_string_freelist(pallheaderslist);
		//not everything is here till now
		return OV_ERR_TARGETGENERIC;
	}
	//split out the first line containing the result
	ov_string_setvalue(&ResponseLine, pallheaderslist[0]);

	//split out the actual result "HTTP/1.0 200 OK" or "HTTP/1.1 404 Not Found"
	plist = ov_string_split(ResponseLine, " ", &len);
	if(len<3){
		ov_string_freelist(pallheaderslist);
		ov_string_freelist(plist);
		return OV_ERR_BADPARAM; //400
	}
	ov_string_setvalue(&responseStruct->version, plist[0]);
	responseStruct->statusCode = atoi(plist[1]);

	//check all other headers
	for (i=1; i<allheaderscount; i++){
		if(ov_string_match(pallheaderslist[i], "Host:*") == TRUE){
			ov_string_freelist(plist);
			plist = ov_string_split(pallheaderslist[i], "Host: ", &len);
			if(len > 0){
				ov_string_setvalue(&(responseStruct->host), plist[1]);
			}
		}else if(ov_string_match(pallheaderslist[i], "Content-Length:*") == TRUE){
			ov_string_freelist(plist);
			plist = ov_string_split(pallheaderslist[i], "Content-Length: ", &len);
			if(len > 0){
				responseStruct->contentLength = atoi(plist[1]);
			}
		}
	}
	if (dataReceived->length >= beginOfContent + responseStruct->contentLength){
		//save pointer to the content
		responseStruct->content = dataReceived->data + beginOfContent;
	}else{
		//we do not have all data till now
		ov_string_freelist(pallheaderslist);
		ov_string_freelist(plist);
		return OV_ERR_TARGETGENERIC;
	}

	ov_string_freelist(pallheaderslist);
	ov_string_freelist(plist);
	return OV_ERR_OK;

}
