// -*- mode: c++; eval: (c-set-style "linux"); -*-
//////////////////////////////////////////////////////////////////
//
// ProxyChannel.h
//
// Copyright (c) Citron Network Inc. 2001-2002
//
// This work is published under the GNU Public License (GPL)
// see file COPYING for details.
// We also explicitely grant the right to link this code
// with the OpenH323 library.
//
// initial author: Chin-Wei Huang <cwhuang@linux.org.tw>
// initial version: 12/7/2001
//
//////////////////////////////////////////////////////////////////

#ifndef PROXYCHANNEL_H
#define PROXYCHANNEL_H "@(#) $Id$"

#ifdef P_SOLARIS
#define map stl_map
#endif

#include <map>
#include "RasTbl.h"
#include "ProxyThread.h"

class H245Handler;
class H245ProxyHandler;
class NATHandler;
class CallSignalSocket;
class H245Socket;
class UDPProxySocket;
class T120ProxySocket;
class LogicalChannel;
class RTPLogicalChannel;
class T120LogicalChannel;

class H245_RequestMessage;
class H245_ResponseMessage;
class H245_CommandMessage;
class H245_IndicationMessage;
class H245_H2250LogicalChannelParameters;
class H245_OpenLogicalChannel;
class H245_OpenLogicalChannelAck;
class H245_OpenLogicalChannelReject;
class H245_CloseLogicalChannel;

class H245Handler {
// This class handles H.245 messages which can either be transmitted on their
// own TCP connection or can be tunneled in the Q.931 connection
public:
	H245Handler(PIPSocket::Address local, PIPSocket::Address remote);
	virtual ~H245Handler();

	virtual void OnH245Address(H225_TransportAddress &);
	virtual bool HandleMesg(PPER_Stream &);
	virtual bool HandleFastStartSetup(H245_OpenLogicalChannel &);
	virtual bool HandleFastStartResponse(H245_OpenLogicalChannel &);
	typedef bool (H245Handler::*pMem)(H245_OpenLogicalChannel &);

	PIPSocket::Address GetLocalAddr() const { return localAddr; }
	void SetLocalAddr(PIPSocket::Address local) { localAddr = local; }

protected:
	virtual bool HandleRequest(H245_RequestMessage &);
	virtual bool HandleResponse(H245_ResponseMessage &);
	virtual bool HandleCommand(H245_CommandMessage &);
	virtual bool HandleIndication(H245_IndicationMessage &);

	NATHandler *hnat;

private:
	PIPSocket::Address localAddr, remoteAddr;
};

class H245ProxyHandler : public H245Handler {
public:
	typedef std::map<WORD, LogicalChannel *>::iterator iterator;
	typedef std::map<WORD, LogicalChannel *>::const_iterator const_iterator;
	typedef std::map<WORD, RTPLogicalChannel *>::iterator siterator;
	typedef std::map<WORD, RTPLogicalChannel *>::const_iterator const_siterator;

	H245ProxyHandler(CallSignalSocket *, PIPSocket::Address, PIPSocket::Address, H245ProxyHandler * = 0);
	virtual ~H245ProxyHandler();

	// override from class H245Handler
	virtual bool HandleFastStartSetup(H245_OpenLogicalChannel &);
	virtual bool HandleFastStartResponse(H245_OpenLogicalChannel &);

	LogicalChannel *FindLogicalChannel(WORD);
	RTPLogicalChannel *FindRTPLogicalChannelBySessionID(WORD);

private:
	// override from class H245Handler
	virtual bool HandleRequest(H245_RequestMessage &);
	virtual bool HandleResponse(H245_ResponseMessage &);

	bool OnLogicalChannelParameters(H245_H2250LogicalChannelParameters *, WORD);
	bool HandleOpenLogicalChannel(H245_OpenLogicalChannel &);
	bool HandleOpenLogicalChannelAck(H245_OpenLogicalChannelAck &);
	bool HandleOpenLogicalChannelReject(H245_OpenLogicalChannelReject &);
	bool HandleCloseLogicalChannel(H245_CloseLogicalChannel &);

	RTPLogicalChannel *CreateRTPLogicalChannel(WORD, WORD);
	RTPLogicalChannel *CreateFastStartLogicalChannel(WORD);
	T120LogicalChannel *CreateT120LogicalChannel(WORD);
	void RemoveLogicalChannel(WORD flcn);

	ProxyHandleThread *handler;
	std::map<WORD, LogicalChannel *> logicalChannels;
	std::map<WORD, RTPLogicalChannel *> sessionIDs;
	std::map<WORD, RTPLogicalChannel *> fastStartLCs;
	H245ProxyHandler *peer;
};

#ifdef DOC_PLUS_PLUS
class  gk_Q931InformationMessageList : public PList {
#endif
PDECLARE_LIST(gk_Q931InformationMessageList, Q931 *);
};

class CallSignalSocket : public TCPProxySocket {
public:
	PCLASSINFO ( CallSignalSocket, TCPProxySocket )

	CallSignalSocket();
	CallSignalSocket(CallSignalSocket *, WORD);
	~CallSignalSocket();

	// override from class ProxySocket
        virtual Result ReceiveData();
	virtual bool EndSession();
	void SendReleaseComplete(const enum Q931::CauseValues cause=Q931::NormalCallClearing);

	// override from class TCPProxySocket
	virtual TCPProxySocket *ConnectTo();
	virtual void SetConnected(bool c);
	virtual void LockUse(const PString &name);
	virtual void UnlockUse(const PString &name);
	virtual const BOOL IsInUse() {
		return m_usedCondition.Condition();
	}
	friend class ProxyDeleter;

	bool HandleH245Mesg(PPER_Stream &);
	void OnH245ChannelClosed() { PWaitAndSignal lock(m_lock); m_h245socket = 0; }

protected:
	void OnSetup(H225_Setup_UUIE &);
	void OnCallProceeding(H225_CallProceeding_UUIE &);
	void OnConnect(H225_Connect_UUIE &);
	void OnAlerting(H225_Alerting_UUIE &);
	void OnInformation(H225_Information_UUIE &);
	void OnReleaseComplete(H225_ReleaseComplete_UUIE &);
	void OnFacility(H225_Facility_UUIE &);
	void OnProgress(H225_Progress_UUIE &);
	void OnEmpty(H225_H323_UU_PDU_h323_message_body &);
	void OnStatus(H225_Status_UUIE &);
	void OnStatusInquiry(H225_StatusInquiry_UUIE &);
	void OnSetupAcknowledge(H225_SetupAcknowledge_UUIE &);
	void OnNotify(H225_Notify_UUIE &);
	void OnNonStandardData(PASN_OctetString &);
	void OnTunneledH245(H225_ArrayOf_PASN_OctetString &);
	void OnFastStart(H225_ArrayOf_PASN_OctetString &, bool);
	void OnInformationMsg(Q931 &pdu);

	const endptr GetCgEP(Q931 &q931pdu);

	template<class UUIE> void HandleH245Address(UUIE & uu)
	{
		if (uu.HasOptionalField(UUIE::e_h245Address))
			if (!SetH245Address(uu.m_h245Address))
				uu.RemoveOptionalField(UUIE::e_h245Address);
	}
	template<class UUIE> void HandleFastStart(UUIE & uu, bool fromCaller)
		{
			if (m_h245handler && uu.HasOptionalField(UUIE::e_fastStart))
				OnFastStart(uu.m_fastStart, fromCaller);
		}

	// localAddr is NOT the local address the socket bind to,
	// but the local address that remote socket bind to
	// they may be different in multi-homed environment
	Address localAddr, peerAddr;
	WORD peerPort;

private:
	void InternalSendReleaseComplete(const enum Q931::CauseValues cause);
	bool InternalEndSession();
	void SetTimer(PTimeInterval time);
	void StartTimer();
	void StopTimer();
	PDECLARE_NOTIFIER(PTimer, CallSignalSocket, OnTimeout);
	PDECLARE_NOTIFIER(PTimer, CallSignalSocket, OnT302Timeout);
	void OnTimeout();
	void SendStatusEnquiryMessage();
	void BuildReleasePDU(Q931 &) const;
	bool SetH245Address(H225_TransportAddress &);
	void SetNumbersInUUIE(PString &CalledPartyNumber, PString & CallingPartyNumber);
	// the method is only valid within ReceiveData()
	Q931 *GetReceivedQ931() const;

	callptr m_call;
	WORD m_crv;
	H245Handler *m_h245handler;
	H245Socket *m_h245socket;
	bool m_h245Tunneling;
	Q931 *m_receivedQ931;

	void DoRoutingDecision();
	void SendInformationMessages();
	void BuildConnection();
	bool FakeSetupACK(Q931 &setup);
	Q931 * GetSetupPDU() const;

        BOOL CgPNConversion(BOOL connecting=FALSE);

	PString DialedDigits;
	PString CalledNumber;
	BOOL isRoutable;
	gk_Q931InformationMessageList Q931InformationMessages;
	Q931 * m_SetupPDU;
	BOOL m_numbercomplete;
	unsigned int m_calledPLAN, m_calledTON;

	PTimeInterval m_timeout;
	PTimer * m_StatusEnquiryTimer; // This timer will be set to the time between 2 ping (i.e. 1 minute)
	PTimer * m_StatusTimer;        // This timer will be set to the maximum wait time for the status message
	                               // (aka pong message)
	PTimer m_t302;
	BOOL m_replytoStatusMessage;

	mutable PMutex m_lock;
	mutable ProxyCondMutex m_usedCondition;
};

inline Q931 * CallSignalSocket::GetSetupPDU() const {
	return m_SetupPDU;
}

inline bool CallSignalSocket::HandleH245Mesg(PPER_Stream & strm)
{
	return m_h245handler->HandleMesg(strm);
}

inline Q931 *CallSignalSocket::GetReceivedQ931() const
{
	return m_receivedQ931;
}

#endif // PROXYCHANNEL_H
