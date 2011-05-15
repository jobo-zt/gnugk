//////////////////////////////////////////////////////////////////
//
// gk.cxx for GNU Gatekeeper
//
// Copyright (c) 2000-2011, Jan Willamowius
//
// This work is published under the GNU Public License version 2 (GPLv2)
// see file COPYING for details.
// We also explicitly grant the right to link this code
// with the OpenH323/H323Plus and OpenSSL library.
//
//////////////////////////////////////////////////////////////////


#include <ptlib.h>
#include <ptlib/sockets.h>
#ifndef _WIN32
#define HAS_SETUSERNAME
#include <signal.h>
#endif
#ifdef P_LINUX
#include <sys/resource.h>
#endif
#include <h225.h>
#include "h323util.h"
#include "Toolkit.h"
#include "stl_supp.h"
#include "RasSrv.h"
#include "RasTbl.h"
#include "SoftPBX.h"
#include "MakeCall.h"
#include "gktimer.h"
#include "gk.h"
#include "config.h"
#include "capctrl.h"

using std::vector;

PCREATE_PROCESS(Gatekeeper)

/*
 * many things here should be members of Gatekeeper
 */

PReadWriteMutex ConfigReloadMutex;

#if PTRACING
PTextFile* Gatekeeper::m_logFile = NULL;
PFilePath Gatekeeper::m_logFilename;
PMutex Gatekeeper::m_logFileMutex;
int Gatekeeper::m_rotateInterval = -1;
int Gatekeeper::m_rotateMinute = 0;
int Gatekeeper::m_rotateHour = 0;
int Gatekeeper::m_rotateDay = 0;
GkTimer* Gatekeeper::m_rotateTimer = GkTimerManager::INVALID_HANDLE;
#endif // PTRACING

PSemaphore ShutdownMutex(1,1);

namespace { // keep the global objects private

PTimedMutex ReloadMutex;

#ifndef _WIN32
PString pidfile("/var/run/gnugk.pid");
#endif

void ShutdownHandler()
{
#if PTRACING
	Gatekeeper::EnableLogFileRotation(false);
#endif
	// delete singleton objects
	PTRACE(3, "GK\tDeleting global reference tables");

	Job::StopAll();
	delete CapacityControl::Instance();
	delete CallTable::Instance();
	delete RegistrationTable::Instance();
	delete RasServer::Instance();
	if (MakeCallEndPoint::InstanceExists())
		delete MakeCallEndPoint::Instance();
	delete Toolkit::Instance();
	PTRACE(3, "GK\tdelete ok");

#if PTRACING
	Gatekeeper::CloseLogFile();
#endif
}

static const char * KnowConfigEntries[][2] = {
	// valid config entries
	{ "AssignedAliases::SQL", "Password" },
	{ "AssignedAliases::SQL", "Username" },
	{ "AssignedAliases::SQL", "Driver" },
	{ "AssignedAliases::SQL", "Database" },
	{ "AssignedAliases::SQL", "Query" },
	{ "AssignedAliases::SQL", "Library" },
	{ "CallTable", "DisabledCodecs" },
	{ "CallTable", "IRRCheck" },
	{ "CallTable", "GenerateNBCDR" },
	{ "CallTable", "GenerateUCCDR" },
	{ "CallTable", "SingleFailoverCDR" },
	{ "CallTable", "AcctUpdateInterval" },
	{ "CallTable", "TimestampFormat" },
	{ "CallTable", "IRRFrequency" },
	{ "CallTable", "DefaultCallDurationLimit" },
	{ "CallTable", "DefaultCallTimeout" },
	{ "CTI::Agents", "RequestTimeout" },
	{ "CTI::Agents", "CTI_Timeout" },	// obsolete, but still accepted
	{ "CTI::Agents", "VirtualQueue" },	// obsolete, but still accepted
	{ "CTI::Agents", "VirtualQueueAliases" },
	{ "CTI::Agents", "VirtualQueuePrefixes" },
	{ "CTI::Agents", "VirtualQueueRegex" },
	{ "CTI::MakeCall", "DisableFastStart" },
	{ "CTI::MakeCall", "DisableH245Tunneling" },
	{ "CTI::MakeCall", "Interface" },
	{ "CTI::MakeCall", "EndpointAlias" },
	{ "CTI::MakeCall", "UseH450" },
	{ "CTI::MakeCall", "Gatekeeper" },
	{ "Endpoint", "Prefix" },
	{ "Endpoint", "EndpointIdentifier" },
	{ "Endpoint", "UseAlternateGK" },
	{ "Endpoint", "Password" },
	{ "Endpoint", "Discovery" },
	{ "Endpoint", "E164" },
	{ "Endpoint", "TimeToLive" },
	{ "Endpoint", "RRQRetryInterval" },
	{ "Endpoint", "Vendor" },
	{ "Endpoint", "UnregisterOnReload" },
	{ "Endpoint", "NATRetryInterval" },
	{ "Endpoint", "NATKeepaliveInterval" },
	{ "Endpoint", "Type" },
	{ "Endpoint", "H323ID" },
	{ "Endpoint", "Gatekeeper" },
	{ "Endpoint", "GatekeeperIdentifier" },
	{ "Endpoint", "DisableH46018" },
	{ "FileAcct", "StandardCDRFormat" },
	{ "FileAcct", "Rotate" },
	{ "FileAcct", "RotateTime" },
	{ "FileAcct", "CDRString" },
	{ "FileAcct", "RotateDay" },
	{ "FileAcct", "TimestampFormat" },
	{ "FileAcct", "DetailFile" },
	{ "Gatekeeper::Acct", "CapacityControl" },
	{ "Gatekeeper::Acct", "default" },
	{ "Gatekeeper::Acct", "FileAcct" },
	{ "Gatekeeper::Acct", "RadAcct" },
	{ "Gatekeeper::Acct", "SQLAcct" },
	{ "Gatekeeper::Acct", "StatusAcct" },
	{ "Gatekeeper::Acct", "SyslogAcct" },
	{ "Gatekeeper::Auth", "AliasAuth" },
	{ "Gatekeeper::Auth", "CapacityControl" },
	{ "Gatekeeper::Auth", "default" },
	{ "Gatekeeper::Auth", "FileIPAuth" },
	{ "Gatekeeper::Auth", "H350PasswordAuth" },
	{ "Gatekeeper::Auth", "PrefixAuth" },
	{ "Gatekeeper::Auth", "RadAliasAuth" },
	{ "Gatekeeper::Auth", "RadAuth" },
	{ "Gatekeeper::Auth", "SimplePasswordAuth" },
	{ "Gatekeeper::Auth", "SQLAuth" },
	{ "Gatekeeper::Auth", "SQLAliasAuth" },
	{ "Gatekeeper::Auth", "SQLPasswordAuth" },
	{ "Gatekeeper::Main", "ExternalIP" },
	{ "Gatekeeper::Main", "ExternalIsDynamic" },
	{ "Gatekeeper::Main", "DefaultDomain" },
	{ "Gatekeeper::Main", "Home" },
	{ "Gatekeeper::Main", "TimeToLive" },
	{ "Gatekeeper::Main", "RedirectGK" },
	{ "Gatekeeper::Main", "EndpointIDSuffix" },
	{ "Gatekeeper::Main", "EndpointSignalPort" },
	{ "Gatekeeper::Main", "Name" },
	{ "Gatekeeper::Main", "TimestampFormat" },
	{ "Gatekeeper::Main", "MinimumBandwidthPerCall" },
	{ "Gatekeeper::Main", "NetworkInterfaces" },
	{ "Gatekeeper::Main", "Fortytwo" },
	{ "Gatekeeper::Main", "StatusTraceLevel" },
	{ "Gatekeeper::Main", "StatusPort" },
	{ "Gatekeeper::Main", "UseMulticastListener" },
	{ "Gatekeeper::Main", "UnicastRasPort" },
	{ "Gatekeeper::Main", "UseBroadcastListener" },
	{ "Gatekeeper::Main", "MulticastPort" },
	{ "Gatekeeper::Main", "MulticastGroup" },
	{ "Gatekeeper::Main", "SkipForwards" },
	{ "Gatekeeper::Main", "AlternateGKs" },
	{ "Gatekeeper::Main", "ListenQueueLength" },
	{ "Gatekeeper::Main", "CompareAliasType" },
	{ "Gatekeeper::Main", "CompareAliasCase" },
	{ "Gatekeeper::Main", "TotalBandwidth" },
	{ "Gatekeeper::Main", "MaximumBandwidthPerCall" },
	{ "Gatekeeper::Main", "SendTo" },
	{ "Gatekeeper::Main", "EndpointSuffix" },
	{ "Gatekeeper::Main", "DisconnectCallsOnShutdown" },
	{ "Gatekeeper::Main", "TraceLevel" },
	{ "Gatekeeper::Main", "SshStatusPort" },
	{ "Gatekeeper::Main", "FortyTwo" },	// legacy
	{ "Gatekeeper::Main", "FourtyTwo" },	// legacy
	{ "GkH350::Settings", "ServiceControl" },
	{ "GkH350::Settings", "ServerPort" },
	{ "GkH350::Settings", "SearchBaseDN" },
	{ "GkH350::Settings", "BindAuthMode" },
	{ "GkH350::Settings", "BindUserDN" },
	{ "GkH350::Settings", "BindUserPW" },
	{ "GkH350::Settings", "ServerName" },
	{ "GkH350::Settings", "AssignedAliases" },
	{ "GkH350::Settings", "GatekeeperDiscovery" },
	{ "GkH350::Settings", "StartTLS" },
	{ "GkQoSMonitor", "Enable" },
	{ "GkQoSMonitor", "CallEndOnly" },
	{ "GkQoSMonitor", "DetailFile" },
	{ "GkQoSMonitor::SQL", "Password" },
	{ "GkQoSMonitor::SQL", "Username" },
	{ "GkQoSMonitor::SQL", "Driver" },
	{ "GkQoSMonitor::SQL", "Database" },
	{ "GkQoSMonitor::SQL", "Query" },
	{ "GkQoSMonitor::SQL", "Host" },
	{ "GkQoSMonitor::SQL", "CacheTimeout" },
	{ "GkQoSMonitor::SQL", "Library" },
	{ "LogFile", "Rotate" },
	{ "LogFile", "RotateTime" },
	{ "LogFile", "RotateDay" },
	{ "LogFile", "Filename" },
	{ "Proxy", "DisableRTPQueueing" },
	{ "Proxy", "RTPPortRange" },
	{ "Proxy", "ProxyForNAT" },
	{ "Proxy", "ProxyForSameNAT" },
	{ "Proxy", "Enable" },
	{ "Proxy", "InternalNetwork" },
	{ "Proxy", "EnableRTCPStats" },
	{ "Proxy", "EnableRTPMute" },
	{ "Proxy", "T120PortRange" },
	{ "Proxy", "ProxyAlways" },
	{ "RadAcct", "RequestTimeout" },
	{ "RadAcct", "RequestRetransmissions" },
	{ "RadAcct", "AppendCiscoAttributes" },
	{ "RadAcct", "RoundRobinServers" },
	{ "RadAcct", "Servers" },
	{ "RadAcct", "SharedSecret" },
	{ "RadAcct", "TimestampFormat" },
	{ "RadAcct", "IdCacheTimeout" },
	{ "RadAcct", "SocketDeleteTimeout" },
	{ "RadAcct", "LocalInterface" },
	{ "RadAcct", "FixedUsername" },
	{ "RadAcct", "UseDialedNumber" },
	{ "RadAcct", "RadiusPortRange" },
	{ "RadAcct", "DefaultAcctPort" },
	{ "RadAliasAuth", "RequestTimeout" },
	{ "RadAliasAuth", "RequestRetransmissions" },
	{ "RadAliasAuth", "AppendCiscoAttributes" },
	{ "RadAliasAuth", "RoundRobinServers" },
	{ "RadAliasAuth", "IncludeTerminalAliases" },
	{ "RadAliasAuth", "Servers" },
	{ "RadAliasAuth", "FixedPassword" },
	{ "RadAliasAuth", "SharedSecret" },
	{ "RadAliasAuth", "IdCacheTimeout" },
	{ "RadAliasAuth", "SocketDeleteTimeout" },
	{ "RadAliasAuth", "LocalInterface" },
	{ "RadAliasAuth", "FixedUsername" },
	{ "RadAliasAuth", "UseDialedNumber" },
	{ "RadAliasAuth", "RadiusPortRange" },
	{ "RadAliasAuth", "DefaultAuthPort" },
	{ "RadAuth", "RequestTimeout" },
	{ "RadAuth", "RequestRetransmissions" },
	{ "RadAuth", "AppendCiscoAttributes" },
	{ "RadAuth", "RoundRobinServers" },
	{ "RadAuth", "IncludeTerminalAliases" },
	{ "RadAuth", "Servers" },
	{ "RadAuth", "SharedSecret" },
	{ "RadAuth", "IdCacheTimeout" },
	{ "RadAuth", "SocketDeleteTimeout" },
	{ "RadAuth", "LocalInterface" },
	{ "RadAuth", "UseDialedNumber" },
	{ "RadAuth", "RadiusPortRange" },
	{ "RadAuth", "DefaultAuthPort" },
	{ "RasSrv::ARQFeatures", "ArjReasonRouteCallToGatekeeper" },
	{ "RasSrv::ARQFeatures", "CallUnregisteredEndpoints" },
	{ "RasSrv::ARQFeatures", "RoundRobinGateways" },
	{ "RasSrv::ARQFeatures", "RemoveTrailingChar" },
	{ "RasSrv::ARQFeatures", "SendRIP" },
	{ "RasSrv::LRQFeatures", "SendRIP" },
	{ "RasSrv::LRQFeatures", "AcceptForwardedLRQ" },
	{ "RasSrv::LRQFeatures", "AcceptNonNeighborLRQ" },
	{ "RasSrv::LRQFeatures", "AcceptNonNeighborLCF" },
	{ "RasSrv::LRQFeatures", "NeighborTimeout" },
	{ "RasSrv::LRQFeatures", "AlwaysForwardLRQ" },
	{ "RasSrv::LRQFeatures", "ForwardResponse" },
	{ "RasSrv::LRQFeatures", "ForwardHopCount" },
	{ "RasSrv::LRQFeatures", "ForwardLRQ" },
	{ "RasSrv::RRQFeatures", "AcceptMCUPrefixes" },
	{ "RasSrv::RRQFeatures", "AliasTypeFilter" },
	{ "RasSrv::RRQFeatures", "AcceptEndpointIdentifier" },
	{ "RasSrv::RRQFeatures", "IRQPollCount" },
	{ "RasSrv::RRQFeatures", "OverwriteEPOnSameAddress" },
	{ "RasSrv::RRQFeatures", "AcceptGatewayPrefixes" },
	{ "RasSrv::RRQFeatures", "SupportDynamicIP" },
	{ "RoutedMode", "GkRouted" },
	{ "RoutedMode", "Q931PortRange" },
	{ "RoutedMode", "AcceptNeighborsCalls" },
	{ "RoutedMode", "ActivateFailover" },
	{ "RoutedMode", "GenerateCallProceeding" },
	{ "RoutedMode", "CpsLimit" },
	{ "RoutedMode", "AcceptUnregisteredCalls" },
	{ "RoutedMode", "RemoveH245AddressOnTunneling" },
	{ "RoutedMode", "TcpKeepAlive" },
	{ "RoutedMode", "H245PortRange" },
	{ "RoutedMode", "ShowForwarderNumber" },
	{ "RoutedMode", "TranslateFacility" },
	{ "RoutedMode", "H245Routed" },
	{ "RoutedMode", "SendReleaseCompleteOnDRQ" },
	{ "RoutedMode", "SupportNATedEndpoints" },
	{ "RoutedMode", "CallSignalPort" },
	{ "RoutedMode", "CallSignalHandlerNumber" },
	{ "RoutedMode", "ENUMservers" },
	{ "RoutedMode", "UseProvisionalRespToH245Tunneling" },
	{ "RoutedMode", "EnableH450.2" },
	{ "RoutedMode", "H4502EmulatorTransferMethod" },
	{ "RoutedMode", "EnableH46018" },
	{ "RoutedMode", "ScreenCallingPartyNumberIE" },
	{ "RoutedMode", "CpsCheckInterval" },
	{ "RoutedMode", "ScreenSourceAddress" },
	{ "RoutedMode", "AlertingTimeout" },
	{ "RoutedMode", "SetupTimeout" },
	{ "RoutedMode", "DropCallsByReleaseComplete" },
	{ "RoutedMode", "RemoveCallOnDRQ" },
	{ "RoutedMode", "GKRouted" },
	{ "RoutedMode", "ScreenDisplayIE" },
	{ "RoutedMode", "FailoverCauses" },
	{ "RoutedMode", "ForwardOnFacility" },
	{ "RoutedMode", "RtpHandlerNumber" },
	{ "RoutedMode", "SocketCleanupTimeout" },
	{ "RoutedMode", "SignalTimeout" },
	{ "RoutedMode", "EnableH46023" },
	{ "RoutedMode", "H46023STUN" },
	{ "RoutedMode", "H46023PublicIP" },
	{ "RoutedMode", "AcceptNeighborCalls" },
	{ "RoutedMode", "SupportCallingNATedEndpoints" },
	{ "RoutedMode", "TranslateReceivedQ931Cause" },
	{ "RoutedMode", "TranslateSentQ931Cause" },
	{ "RoutedMode", "H46018NoNat" },
	{ "RoutedMode", "NATStdMin" },
	{ "RoutedMode", "DisableRetryChecks" },
	{ "Routing::CatchAll", "CatchAllAlias" },
	{ "Routing::CatchAll", "CatchAllIP" },
	{ "Routing::Sql", "Password" },
	{ "Routing::Sql", "Username" },
	{ "Routing::Sql", "Driver" },
	{ "Routing::Sql", "Database" },
	{ "Routing::Sql", "Query" },
	{ "Routing::Sql", "Host" },
	{ "Routing::Sql", "MinPoolSize" },
	{ "Routing::Sql", "Library" },
	{ "SQLAcct", "StopQuery" },
	{ "SQLAcct", "Password" },
	{ "SQLAcct", "TimestampFormat" },
	{ "SQLAcct", "Username" },
	{ "SQLAcct", "StartQuery" },
	{ "SQLAcct", "UpdateQuery" },
	{ "SQLAcct", "Driver" },
	{ "SQLAcct", "Database" },
	{ "SQLAcct", "StopQueryAlt" },
	{ "SQLAcct", "StartQueryAlt" },
	{ "SQLAcct", "MinPoolSize" },
	{ "SQLAcct", "Host" },
	{ "SQLAcct", "AlertQuery" },
	{ "SQLAcct", "RegisterQuery" },
	{ "SQLAcct", "UnregisterQuery" },
	{ "SQLAcct", "Library" },
	{ "SQLAliasAuth", "TableJWJW" },
	{ "SQLAliasAuth", "Password" },
	{ "SQLAliasAuth", "Username" },
	{ "SQLAliasAuth", "Driver" },
	{ "SQLAliasAuth", "MinPoolSize" },
	{ "SQLAliasAuth", "Database" },
	{ "SQLAliasAuth", "Query" },
	{ "SQLAliasAuth", "Host" },
	{ "SQLAliasAuth", "CacheTimeout" },
	{ "SQLAliasAuth", "Library" },
	{ "SQLAuth", "Password" },
	{ "SQLAuth", "Username" },
	{ "SQLAuth", "CallQuery" },
	{ "SQLAuth", "Driver" },
	{ "SQLAuth", "MinPoolSize" },
	{ "SQLAuth", "Database" },
	{ "SQLAuth", "Host" },
	{ "SQLAuth", "NbQuery" },
	{ "SQLAuth", "RegQuery" },
	{ "SQLAuth", "CacheTimeout" },
	{ "SQLAuth", "Library" },
	{ "SQLConfig", "NeighborsQuery" },
	{ "SQLConfig", "Password" },
	{ "SQLConfig", "Username" },
	{ "SQLConfig", "Driver" },
	{ "SQLConfig", "PermanentEndpointsQuery" },
	{ "SQLConfig", "Database" },
	{ "SQLConfig", "Host" },
	{ "SQLConfig", "ConfigQuery" },
	{ "SQLConfig", "Library" },
	{ "SQLConfig", "GWPrefixesQuery" },
	{ "SQLConfig", "RewriteE164Query" },
	{ "SQLConfig", "RewriteAliasQuery" },
	{ "SQLPasswordAuth", "Password" },
	{ "SQLPasswordAuth", "Username" },
	{ "SQLPasswordAuth", "Driver" },
	{ "SQLPasswordAuth", "MinPoolSize" },
	{ "SQLPasswordAuth", "Database" },
	{ "SQLPasswordAuth", "Query" },
	{ "SQLPasswordAuth", "Host" },
	{ "SQLPasswordAuth", "CacheTimeout" },
	{ "SQLPasswordAuth", "Library" },
	{ "StatusAcct", "UpdateEvent" },
	{ "StatusAcct", "TimestampFormat" },
	{ "StatusAcct", "ConnectEvent" },
	{ "StatusAcct", "StopEvent" },
	{ "StatusAcct", "StartEvent" },
	{ "SyslogAcct", "UpdateEvent" },
	{ "SyslogAcct", "TimestampFormat" },
	{ "SyslogAcct", "ConnectEvent" },
	{ "SyslogAcct", "StopEvent" },
	{ "SyslogAcct", "StartEvent" },
    { "GkPresence::SQL", "Driver" },
	{ "GkPresence::SQL", "MinPoolSize" },
    { "GkPresence::SQL", "Host" },
    { "GkPresence::SQL", "Database" },
    { "GkPresence::SQL", "CacheTimeout" },
    { "GkPresence::SQL", "QueryList" },
    { "GkPresence::SQL", "QueryAdd" },
    { "GkPresence::SQL", "QueryDelete" },
    { "GkPresence::SQL", "QueryUpdate" },
    { "AssignedGatekeepers::SQL", "Driver" },
	{ "AssignedGatekeepers::SQL", "MinPoolSize" },
    { "AssignedGatekeepers::SQL", "Host" },
    { "AssignedGatekeepers::SQL", "Database" },
    { "AssignedGatekeepers::SQL", "CacheTimeout" },
    { "AssignedGatekeepers::SQL", "Query" },
    { "AssignedAliases::SQL", "Driver" },
	{ "AssignedAliases::SQL", "MinPoolSize" },
    { "AssignedAliases::SQL", "Host" },
    { "AssignedAliases::SQL", "Database" },
    { "AssignedAliases::SQL", "CacheTimeout" },
    { "AssignedAliases::SQL", "Query" },

	// ignore name partially to check
	{ "EP::", "DisableH46018" },
	{ "EP::", "CalledTypeOfNumber" },
	{ "EP::", "MaxBandwidth" },
	{ "EP::", "CallingTypeOfNumber" },
	{ "EP::", "PrefixCapacities" },
	{ "EP::", "TranslateReceivedQ931Cause" },
	{ "EP::", "TranslateSentQ931Cause" },
	{ "EP::", "Capacity" },
	{ "EP::", "Proxy" },
	{ "EP::", "GatewayPriority" },
	{ "EP::", "GatewayPrefixes" },
	{ "Neighbor::", "AcceptForwardedLRQ" },
	{ "Neighbor::", "UseH46018" },
	{ "Neighbor::", "Dynamic" },
	{ "Neighbor::", "Password" },
	{ "Neighbor::", "AcceptPrefixes" },
	{ "Neighbor::", "ForwardResponse" },
	{ "Neighbor::", "SendPrefixes" },
	{ "Neighbor::", "ForwardHopCount" },
	{ "Neighbor::", "ForwardLRQ" },
	{ "Neighbor::", "Host" },
	{ "Neighbor::", "GatekeeperIdentifier" },

	// uncheckable sections
	{ "CapacityControl", "*" },
	{ "Endpoint::RewriteE164", "*" },
	{ "GkStatus::Auth", "*" },
	{ "H225toQ931", "*" },
	{ "FileIPAuth", "*" },
	{ "ModeSelection", "*" },
	{ "NATedEndpoints", "*" },
	{ "PrefixAuth", "*" },
	{ "RasSrv::AlternateGatekeeper", "*" },
	{ "RasSrv::AssignedAlias", "*" },
	{ "RasSrv::AssignedGatekeeper", "*" },
	{ "RasSrv::AssignedAliases", "*" },
	{ "RasSrv::GWPrefixes", "*" },
	{ "RasSrv::GWRewriteE164", "*" },
	{ "RasSrv::Neighbors", "*" },
	{ "RasSrv::PermanentEndpoints", "*" },
	{ "RasSrv::RewriteAlias", "*" },
	{ "RasSrv::RewriteE164", "*" },
	{ "RasSrv::RRQAuth", "*" },
	{ "ReplyToRasAddress", "*" },
	{ "RewriteCLI", "*" },
	{ "Routing::Explicit", "*" },
	{ "Routing::NumberAnalysis", "*" },
	{ "RoutingPolicy", "*" },
	{ "RoutingPolicy::OnARQ", "*" },
	{ "RoutingPolicy::OnFacility", "*" },
	{ "RoutingPolicy::OnLRQ", "*" },
	{ "RoutingPolicy::OnSetup", "*" },
	{ "SimplePasswordAuth", "*" },

	{ NULL }	// the end
};

bool CheckConfig(PConfig * cfg, const PString & mainsection)
{
	unsigned warnings = 0;
	bool mainsectionfound = false;
	PStringList sections = cfg->GetSections();
	for (PINDEX i = 0; i < sections.GetSize(); ++i) {
		// check section names
		PCaselessString sect = sections[i];
		if ((sect.Left(1) == ";") || (sect.Left(1) == "#")) {
			continue;
		}
		if (sect.Left(4) == "EP::") {
			sect = "EP::";
		}
		if (sect.Left(10) == "Neighbor::") {
			sect = "Neighbor::";
		}
		if (sect == mainsection) {
			mainsectionfound = true;
		}
		const char * ks = NULL;
		unsigned j = 0;
		bool found = false;
		bool section_checkable = true;
		while ((ks = KnowConfigEntries[j][0])) {
			if (sect == ks) {
				found = true;
				section_checkable = (PString(KnowConfigEntries[j][1]) != "*");
				break;
			}
			j++;
		}
		if (!found) {
			PTRACE(0, "WARNING: Config section [" << sect << "] unknown");
			warnings++;
		} else if (!section_checkable) {
			//PTRACE(0, "Section " << sect << " can't be checked in detail");
		} else {
			// check all entries in this section
			PStringToString entries = cfg->GetAllKeyValues(sect);
			for (PINDEX j = 0; j < entries.GetSize(); j++) {
				PCaselessString key = entries.GetKeyAt(j);
				PString value = entries.GetDataAt(j);
				if (value.IsEmpty()) {
					PTRACE(0, "WARNING: Empty switch: [" << sect << "] " << key << "=");
				}
				unsigned k = 0;
				bool entry_found = false;
				while ((ks = KnowConfigEntries[k][0])) {
					const char * ke = KnowConfigEntries[k][1];
					k++;
					if ((sect == ks) && (key == ke)) {
						entry_found = true;
						break;
					}
				}
				if (!entry_found) {
					PTRACE(0, "WARNING: Config entry [" << sect << "] " << key << "=" << value << " unknown");
					warnings++;
				}
			}
		}
	}
	if (!mainsectionfound) {
		PTRACE(0, "WARNING: This doesn't look like a GNU Gatekeeper configuration file!");
	}

	return (warnings == 0);
}

// due to some unknown reason (PWLib bug?),
// we have to delete Toolkit::Instance first,
// or we get core dump
void ExitGK()
{
#if PTRACING
	Gatekeeper::EnableLogFileRotation(false);
#endif

	delete Toolkit::Instance();

#if PTRACING
	Gatekeeper::CloseLogFile();
#endif
	exit(0);
}

} // end of anonymous namespace

void ReloadHandler()
{
	Gatekeeper::ReopenLogFile();

	// only one thread must do this
	if (ReloadMutex.WillBlock())
		return;
	
	/*
	** Enter critical Section
	*/
	PWaitAndSignal reload(ReloadMutex);

	ConfigReloadMutex.StartWrite();

	/*
	** Force reloading config
	*/
	Toolkit::Instance()->ReloadConfig();

	SoftPBX::TimeToLive = GkConfig()->GetInteger("TimeToLive", SoftPBX::TimeToLive);

	/*
	** Update all gateway prefixes
	*/

	CallTable::Instance()->LoadConfig();
	RegistrationTable::Instance()->LoadConfig();
	CallTable::Instance()->UpdatePrefixCapacityCounters();

	// don't put this in LoadConfig()
	RasServer::Instance()->SetRoutedMode();

	// Load ENUM servers
	RasServer::Instance()->SetENUMServers();

	// Load RDS servers
	RasServer::Instance()->SetRDSServers();

	RasServer::Instance()->LoadConfig();

	Gatekeeper::EnableLogFileRotation();
	
	ConfigReloadMutex.EndWrite();

	/*
	** Don't disengage current calls!
	*/
	PTRACE(3, "GK\tCarry on current calls.");

	/*
	** Leave critical Section
	*/
	// give other threads the chance to pass by this handler
	PThread::Sleep(500);
}

#ifdef _WIN32

BOOL WINAPI WinCtrlHandlerProc(DWORD dwCtrlType)
{
	PString eventName = "CTRL_UNKNOWN_EVENT";
	
	if( dwCtrlType == CTRL_LOGOFF_EVENT ) {
		eventName = "CTRL_LOGOFF_EVENT";
#if PTRACING
		PTRACE(2,"GK\tGatekeeper received " <<eventName);
#endif
		// prevent shut down
		return FALSE;
	}
	
	if( dwCtrlType == CTRL_C_EVENT )
		eventName = "CTRL_C_EVENT";
	else if( dwCtrlType == CTRL_BREAK_EVENT )
		eventName = "CTRL_BREAK_EVENT";
	else if( dwCtrlType == CTRL_CLOSE_EVENT )
		eventName = "CTRL_CLOSE_EVENT";
	else if( dwCtrlType == CTRL_SHUTDOWN_EVENT )
		eventName = "CTRL_SHUTDOWN_EVENT";

#if PTRACING
	PTRACE(1,"GK\tGatekeeper shutdown due to "<<eventName);
#endif

	PWaitAndSignal shutdown(ShutdownMutex);
	RasServer::Instance()->Stop();

	// CTRL_CLOSE_EVENT:
	// this case needs special treatment as Windows would
	// immidiately call ExitProcess() upon returning TRUE,
	// and the GK has no chance to clean-up. The call to
	// WaitForSingleObject() results in around 5 sec's of
	// clean-up time - This may at times not be sufficient
	// for the GK to shut down in an organized fashion. The
	// only safe way to handle this, is to remove the
	// 'Close' menu item from the System menu and we will
	// never have to deal with this event again.
	if( dwCtrlType == CTRL_CLOSE_EVENT )
		WaitForSingleObject(GetCurrentProcess(), 15000);

	// proceed with shut down
	return TRUE;
}

bool Gatekeeper::SetUserAndGroup(const PString& /*username*/)
{
	return false;
}

// method to enable data execution prevention when supported by OS (starting with XP SP3)
#define PROCESS_DEP_ENABLE                          0x00000001
#define PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION     0x00000002

BOOL SetDEP(__in DWORD dwFlags = PROCESS_DEP_ENABLE)
{
	HMODULE hMod = GetModuleHandleW(L"Kernel32.dll");
	if (!hMod)
		return FALSE;
	typedef BOOL (WINAPI *PSETDEP)(DWORD);
	PSETDEP procSet = (PSETDEP)GetProcAddress(hMod,"SetProcessDEPPolicy");
	if (!procSet)
		return FALSE;
	return procSet(dwFlags);
}

#else	// _WIN32

#  include <pwd.h>

bool Gatekeeper::SetUserAndGroup(const PString &username)
{
#if defined(P_PTHREADS) && !defined(P_THREAD_SAFE_CLIB)
	static const size_t MAX_PASSWORD_BUFSIZE = 1024;

	struct passwd userdata;
	struct passwd *userptr;
	char buffer[MAX_PASSWORD_BUFSIZE];

#if defined (P_LINUX) || defined (P_AIX) || defined(P_IRIX) || (__GNUC__>=3 && defined(P_SOLARIS)) || defined(P_RTEMS)
	::getpwnam_r(username, &userdata, buffer, sizeof(buffer), &userptr);
#else
	userptr = ::getpwnam_r(username, &userdata, buffer, sizeof(buffer));
#endif
#else
	struct passwd *userptr = ::getpwnam(username);
#endif

	return userptr && userptr->pw_name
		&& (::setgid(userptr->pw_gid) == 0) && (::setuid(userptr->pw_uid) == 0);
}

void UnixShutdownHandler(int sig)
{
	if (ShutdownMutex.WillBlock() || !RasServer::Instance()->IsRunning())
		return;
	PWaitAndSignal shutdown(ShutdownMutex);
	PTRACE(1, "GK\tReceived signal " << sig);
	PFile::Remove(pidfile);
	RasServer::Instance()->Stop();
}

void UnixReloadHandler(int sig) // For HUP Signal
{
	PTRACE(1, "GK\tGatekeeper Hangup (signal " << sig << ")");
	ReloadHandler();
}
#endif // _WIN32


// default params for overwriting
Gatekeeper::Gatekeeper(const char * _manuf,
					   const char * _name,
					   WORD _majorVersion,
					   WORD _minorVersion,
					   CodeStatus _status,
					   WORD _buildNumber)
#ifdef COMPILE_AS_SERVICE
	: PServiceProcess(_manuf, _name, _majorVersion, _minorVersion, _status, _buildNumber)
#else
	: PProcess(_manuf, _name, _majorVersion, _minorVersion, _status, _buildNumber)
#endif
{
#ifdef _WIN32
	// set data execution prevention (ignore if not available)
	SetDEP(PROCESS_DEP_ENABLE);
#endif

#ifdef COMPILE_AS_SERVICE
	// save original arguments
	for (int i = 0; i < GetArguments().GetCount(); i++) {
		savedArguments += GetArguments().GetParameter(i);
		savedArguments += " ";
	}
#ifndef _WIN32
	// set startup arguments for service process
	GetArguments().Parse(GetArgumentsParseString());
	if (GetArguments().HasOption("pid"))
		pidfile = GetArguments().GetOptionString("pid");
	GetArguments().SetArgs("-d -p " + pidfile);
#endif
#endif
}

#ifdef COMPILE_AS_SERVICE
PBoolean Gatekeeper::OnStart()
{
	// change to the default directory to the one containing the executable
	PDirectory exeDir = GetFile().GetDirectory();
	exeDir.Change();

	return TRUE;
}

void Gatekeeper::OnStop()
{
	Terminate();
}

void Gatekeeper::Terminate()
{
#ifdef _WIN32
	if (!RasServer::Instance()->IsRunning())
#else
	if (ShutdownMutex.WillBlock() || !RasServer::Instance()->IsRunning())
#endif
		return;
	PWaitAndSignal shutdown(ShutdownMutex);
	RasServer::Instance()->Stop();
	// wait for termination
	PThread::Sleep(10 * 1000);	// sleep 10 sec
}

PBoolean Gatekeeper::OnPause()
{
	// ignore pause
	return false;	// return true; would pause, but we can't continue, then
}


void Gatekeeper::OnContinue()
{
	// ignore continue (can't be paused, anyway)
}

void Gatekeeper::OnControl()
{
}
#endif // COMPILE_AS_SERVICE


const PString Gatekeeper::GetArgumentsParseString() const
{
	return PString
		("r-routed."
		 "-h245routed."
		 "d-direct."
		 "i-interface:"
		 "l-timetolive:"
		 "b-bandwidth:"
#ifdef HAS_SETUSERNAME
		 "u-user:"
#endif
#if PTRACING
		 "t-trace."
		 "o-output:"
#endif
		 "c-config:"
		 "s-section:"
		 "-pid:"
#ifdef P_LINUX
		 "-core:"
#endif
		 "h-help:"
		 );
}


bool Gatekeeper::InitHandlers(const PArgList& args)
{
#ifdef _WIN32
	SetConsoleCtrlHandler(WinCtrlHandlerProc, TRUE);
#else
	struct sigaction sigact;
	
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = UnixShutdownHandler;
	sigemptyset(&sigact.sa_mask);
	sigaddset(&sigact.sa_mask, SIGTERM);
	sigaddset(&sigact.sa_mask, SIGINT);
	sigaddset(&sigact.sa_mask, SIGQUIT);
	sigaddset(&sigact.sa_mask, SIGHUP);
	sigaddset(&sigact.sa_mask, SIGUSR1);
	
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = SIG_IGN;
	sigemptyset(&sigact.sa_mask);

	// ignore these signals
	sigaction(SIGPIPE, &sigact, NULL);
	sigaction(SIGABRT, &sigact, NULL);

	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_handler = UnixReloadHandler;
	sigemptyset(&sigact.sa_mask);
	sigaddset(&sigact.sa_mask, SIGHUP);
	sigaddset(&sigact.sa_mask, SIGUSR1);
	
	sigaction(SIGHUP, &sigact, NULL);
	sigaction(SIGUSR1, &sigact, NULL);

	if (args.HasOption("pid"))
		pidfile = args.GetOptionString("pid");
	PTextFile pid(pidfile, PFile::WriteOnly);
	pid.WriteLine(PString(PString::Unsigned, getpid()));
#endif
	return TRUE;
}


bool Gatekeeper::InitLogging(const PArgList &args)
{
#if PTRACING
	// Syslog is the default when compiled as service, but we don't want that
	PTrace::ClearOptions(PTrace::SystemLogStream);
	PTrace::SetOptions(PTrace::DateAndTime | PTrace::TraceLevel | PTrace::FileAndLine);
	PTrace::SetLevel(args.GetOptionCount('t'));
	if (args.HasOption('o')) {
		if (!SetLogFilename(args.GetOptionString('o'))) {
			cerr << "Warning: could not open the log file \""
			     << args.GetOptionString('o') << '"' << endl;
			return FALSE;
		}
	}
#endif
	
	return TRUE;
}


bool Gatekeeper::InitConfig(const PArgList &args)
{
	// get the name of the config file
	PFilePath fp;
	PString section("Gatekeeper::Main");

	if (args.HasOption('c'))
		fp = PFilePath(args.GetOptionString('c'));
	else
		fp = "gatekeeper.ini";
	if (!PFile::Exists(fp)) {
		cerr << "WARNING: Config file " << fp << " doesn't exist!"
			 << "- Use the -c switch to specify the config file.\n" << endl;
	}

	if (args.HasOption('s'))
		section = args.GetOptionString('s');

	Toolkit::Instance()->SetConfig(fp, section);

	// check config for unknown options (only warn about them)
	CheckConfig(GkConfig(), section);

	return true;
}


void Gatekeeper::PrintOpts(void)
{
	cout << "Options:\n"
		"  -r  --routed       : Use gatekeeper routed call signaling\n"
		"  -rr --h245routed   : Use H.245 control channel routed\n"
		"  -d  --direct       : Use direct endpoint call signaling\n"
		"  -i  --interface IP : The IP that the gatekeeper listen to\n"
		"  -l  --timetolive n : Time to live for client registration\n"
		"  -b  --bandwidth n  : Specify the total bandwidth\n"
#ifdef HAS_SETUSERNAME
		"  -u  --user name    : Run as this user\n"
#endif
#if PTRACING
		"  -t  --trace        : Set trace verbosity\n"
		"  -o  --output file  : Write trace to this file\n"
#endif
		"  -c  --config file  : Specify which config file to use\n"
		"  -s  --section sec  : Specify which main section to use in the config file\n"
		"      --pid file     : Specify the pid file\n"
#ifdef P_LINUX
		"      --core n       : Enable core dumps (with max size of n bytes)\n"
#endif
		"  -h  --help         : Show this message\n" << endl;
}


void Gatekeeper::Main()
{
#ifdef COMPILE_AS_SERVICE
	GetArguments().SetArgs(savedArguments);
#endif

	PArgList & args = GetArguments();
	args.Parse(GetArgumentsParseString());

#ifdef P_LINUX
	// set the core file size
	if (args.HasOption("core")) {
		struct rlimit rlim;
		if (getrlimit(RLIMIT_CORE, &rlim) != 0)
			cout << "Could not get current core file size : error = " << errno << endl;
		else {
			cout << "Current core dump size limits - soft: " << rlim.rlim_cur
				<< ", hard: " << rlim.rlim_max << endl;
			int uid = geteuid();
			seteuid(getuid()); // Switch back to starting uid for next call
			const PCaselessString s = args.GetOptionString("core");
			rlim_t v = (s == "unlimited" ? RLIM_INFINITY : (rlim_t)s.AsInteger());
			rlim.rlim_cur = v;
			if (setrlimit(RLIMIT_CORE, &rlim) != 0)
				cout << "Could not set current core file size to " << v << " : error = " << errno << endl;
			else {
				getrlimit(RLIMIT_CORE, &rlim);
				cout << "New core dump size limits - soft: " << rlim.rlim_cur
					<< ", hard: " << rlim.rlim_max << endl;
			}
			seteuid(uid);
		}
	}
#endif

#ifdef HAS_SETUSERNAME
	if (args.HasOption('u')) {
		const PString username = args.GetOptionString('u');

		if ( !SetUserAndGroup(username) ) {
			cout << "GNU Gatekeeper could not run as user "
			     << username
			     << endl;
			return;
		}
	}
#endif

	if(!InitLogging(args))
		return;

	if (args.HasOption('h')) {
		PrintOpts();
		ExitGK();
	}

	if (args.HasOption('i'))
		Toolkit::Instance()->SetGKHome(args.GetOptionString('i').Lines());

	if (!InitConfig(args) || !InitHandlers(args))
		ExitGK();


	// set trace level + output file from config , if not set on the command line (for service)
	PString fake_cmdline;
	if (args.GetOptionCount('t') == 0) {
		int log_trace_level = GkConfig()->GetInteger("TraceLevel", 0);
		for (int t=0; t < log_trace_level; t++)
			fake_cmdline += " -t";
	}
	if (!args.HasOption('o')) {
		PString log_trace_file = GkConfig()->GetString("Logfile", "Filename", "");
		if (!log_trace_file.IsEmpty())
			fake_cmdline += " -o " + log_trace_file;
	}
	if (!fake_cmdline.IsEmpty()) {
		PArgList fake_args(fake_cmdline);
		fake_args.Parse(GetArgumentsParseString());
		InitLogging(fake_args);
	}

	EnableLogFileRotation();
	
	PString welcome("GNU Gatekeeper with ID '" + Toolkit::GKName() + "' started\n" + Toolkit::GKVersion());
	cout << welcome << '\n';
	PTRACE(1, welcome);

	vector<PIPSocket::Address> GKHome;
	PString home(Toolkit::Instance()->GetGKHome(GKHome));
	if (GKHome.empty()) {
		cerr << "Fatal: Cannot find any interface to run GnuGk!\n";
		ExitGK();
	}
	cout << "Listen on " << home << "\n";

	PIPSocket::Address addr;
	if (Toolkit::Instance()->isBehindNAT(addr))
		cout << "Public IP: " << addr.AsString() << "\n\n";
	else
	    cout << "\n";

	// Copyright notice
	cout <<
		"This program is free software; you can redistribute it and/or\n"
		"modify it under the terms of the GNU General Public License version 2.\n"
		"We also explicitly grant the right to link this code\n"
		"with the OpenH323/H323Plus and OpenSSL library.\n"
	     << endl;

#ifdef HAS_H46018
	cout << "This program contains H.460.18 and H.460.19 technology patented by Tandberg\n"
			"and licensed to the GNU Gatekeeper Project.\n"
			<< endl;
#endif

#ifdef HAS_H46023
	cout << "This program contains H.460.23 and H.460.24 technology\n"
            "licensed to the GNU Gatekeeper Project.\n"
		 << endl;
#endif

	// read capacity from commandline
	int GKcapacity;
	if (args.HasOption('b'))
		GKcapacity = args.GetOptionString('b').AsInteger();
	else
		GKcapacity = GkConfig()->GetInteger("TotalBandwidth", -1);
	if (GKcapacity == 0)
		GKcapacity = -1;	// turn bw management off for 0, too
	CallTable::Instance()->SetTotalBandwidth(GKcapacity);
	if (GKcapacity < 0)
		PTRACE(2, "GK\tTotal bandwidth not limited");
	else
		PTRACE(2, "GK\tAvailable bandwidth: " << GKcapacity);

	// read timeToLive from command line
	if (args.HasOption('l'))
		SoftPBX::TimeToLive = args.GetOptionString('l').AsInteger();
	else
		SoftPBX::TimeToLive = GkConfig()->GetInteger("TimeToLive", -1);
	PTRACE(2, "GK\tTimeToLive for Registrations: " << SoftPBX::TimeToLive);

	RasServer *RasSrv = RasServer::Instance();

	// read signaling method from commandline
	if (args.HasOption('r'))
		RasSrv->SetRoutedMode(true, (args.GetOptionCount('r') > 1 || args.HasOption("h245routed")));
	else if (args.HasOption('d'))
		RasSrv->SetRoutedMode(false, false);
	else
		RasSrv->SetRoutedMode();

	// Load ENUM servers
	RasSrv->SetENUMServers();

	// Load RDS servers
	RasSrv->SetRDSServers();	

#if defined(_WIN32)
	// 1) prevent CTRL_CLOSE_EVENT, CTRL_LOGOFF_EVENT and CTRL_SHUTDOWN_EVENT
	//    dialog box from being displayed.
	// 2) set process shutdown priority - we want as much time as possible
	//    for tasks, such as unregistering endpoints during the shut down process.
	//    0x3ff is a maximimum permitted for windows app
	SetProcessShutdownParameters(0x3ff, SHUTDOWN_NORETRY);
#endif

	// let's go
	RasSrv->Run();

	//HouseKeeping();

	// graceful shutdown
	cerr << "\nShutting down gatekeeper . . . ";
	ShutdownHandler();
	cerr << "done\n";

#ifdef _WIN32
	// remove control handler/close console
	SetConsoleCtrlHandler((PHANDLER_ROUTINE)WinCtrlHandlerProc, FALSE);
	FreeConsole();
#endif // _WIN32
}

#if PTRACING
namespace {
const char* const logConfigSectionName = "Logfile";
}

const char* const Gatekeeper::m_intervalNames[] =
{
	"Hourly", "Daily", "Weekly", "Monthly"
};

void Gatekeeper::GetRotateInterval(
	PConfig& cfg,
	const PString& section
	)
{
	PString s;
	
	if (m_rotateInterval == Hourly)
		m_rotateMinute = cfg.GetInteger(section, "RotateTime", 59);
	else {
		s = cfg.GetString(section, "RotateTime", "00:59");
		m_rotateHour = s.AsInteger();
		m_rotateMinute = 0;
		if (s.Find(':') != P_MAX_INDEX)
			m_rotateMinute = s.Mid(s.Find(':') + 1).AsInteger();
			
		if (m_rotateHour < 0 || m_rotateHour > 23 || m_rotateMinute < 0
			|| m_rotateMinute > 59) {
			PTRACEX(1, "GK\tInvalid log file RotateTime specified: " << s);
			m_rotateMinute = 59;
			m_rotateHour = 0;
		}
	}
			
	if (m_rotateInterval == Weekly)	{
		s = cfg.GetString(section, "RotateDay", "Sun");
		if (strspn(s, "0123456") == (size_t)s.GetLength()) {
			m_rotateDay = s.AsInteger();
		} else {
			std::map<PCaselessString, int> dayNames;
			dayNames["sun"] = 0; dayNames["sunday"] = 0;
			dayNames["mon"] = 1; dayNames["monday"] = 1;
			dayNames["tue"] = 2; dayNames["tuesday"] = 2;
			dayNames["wed"] = 3; dayNames["wednesday"] = 3;
			dayNames["thu"] = 4; dayNames["thursday"] = 4;
			dayNames["fri"] = 5; dayNames["friday"] = 5;
			dayNames["sat"] = 6; dayNames["saturday"] = 6;
			std::map<PCaselessString, int>::const_iterator i = dayNames.find(s);
			m_rotateDay = (i != dayNames.end()) ? i->second : -1;
		}
		if (m_rotateDay < 0 || m_rotateDay > 6) {
			PTRACEX(1, "GK\tInvalid log file RotateDay specified: " << s);
			m_rotateDay = 0;
		}
	} else if (m_rotateInterval == Monthly) {
		m_rotateDay = cfg.GetInteger(section, "RotateDay", 1);
		if (m_rotateDay < 1 || m_rotateDay > 31) {
			PTRACEX(1, "GK\tInvalid RotateDay specified: "
				<< cfg.GetString(section, "RotateDay", "")
				);
			m_rotateDay = 1;
		}
	}
}

void Gatekeeper::EnableLogFileRotation(
	bool enable
	)
{
	PWaitAndSignal lock(m_logFileMutex);
	
	if (m_rotateTimer != GkTimerManager::INVALID_HANDLE) {
		Toolkit::Instance()->GetTimerManager()->UnregisterTimer(m_rotateTimer);
		m_rotateTimer = GkTimerManager::INVALID_HANDLE;
	}
	
	if (!enable)
		return;
		
	PConfig* const config = GkConfig();
	// determine rotation type (by lines, by size, by time)	
	const PString rotateCondition = config->GetString(
		logConfigSectionName, "Rotate", ""
		).Trim();
	if (rotateCondition.IsEmpty())
		return;
		
	for (int i = 0; i < RotationIntervalMax; i++)
		if (strcasecmp(rotateCondition, m_intervalNames[i]) == 0)
			m_rotateInterval = i;

	if (m_rotateInterval < 0 || m_rotateInterval >= RotationIntervalMax) {
		PTRACEX(1, "GK\tUnsupported log file rotation method: "
			<< rotateCondition << " - rotation disabled"
			);
		return;
	}

	// time based rotation
	GetRotateInterval(*config, logConfigSectionName);

	// setup rotation timer in case of time based rotation
	PTime now, rotateTime;
			
	switch (m_rotateInterval)
	{
	case Hourly:
		rotateTime = PTime(0, m_rotateMinute, now.GetHour(), now.GetDay(),
			now.GetMonth(), now.GetYear(), now.GetTimeZone()
			);
		if (rotateTime <= now)
			rotateTime += PTimeInterval(0, 0, 0, 1); // 1 hour
		m_rotateTimer = Toolkit::Instance()->GetTimerManager()->RegisterTimer(
			&Gatekeeper::RotateOnTimer, rotateTime, 60*60
			);
		PTRACEX(5, "GK\tHourly log file rotation enabled (first "
			"rotation scheduled at " << rotateTime
			);
		break;
		
	case Daily:
		rotateTime = PTime(0, m_rotateMinute, m_rotateHour, now.GetDay(),
			now.GetMonth(), now.GetYear(), now.GetTimeZone()
			);
		if (rotateTime <= now)
			rotateTime += PTimeInterval(0, 0, 0, 0, 1); // 1 day
		m_rotateTimer = Toolkit::Instance()->GetTimerManager()->RegisterTimer(
			&Gatekeeper::RotateOnTimer, rotateTime, 60*60*24
			);
		PTRACEX(5, "GK\tDaily rotation enabled (first rotation scheduled at "
			<< rotateTime
			);
		break;
		
	case Weekly:
		rotateTime = PTime(0, m_rotateMinute, m_rotateHour, now.GetDay(),
			now.GetMonth(), now.GetYear(), now.GetTimeZone()
			);
		if (rotateTime.GetDayOfWeek() < m_rotateDay)
			rotateTime += PTimeInterval(0, 0, 0, 0,
				m_rotateDay - rotateTime.GetDayOfWeek() /* days */
				);
		else if (rotateTime.GetDayOfWeek() > m_rotateDay)
			rotateTime -= PTimeInterval(0, 0, 0, 0,
				rotateTime.GetDayOfWeek() - m_rotateDay /* days */
				);
		if (rotateTime <= now)
			rotateTime += PTimeInterval(0, 0, 0, 0, 7); // 1 week
		m_rotateTimer = Toolkit::Instance()->GetTimerManager()->RegisterTimer(
			&Gatekeeper::RotateOnTimer, rotateTime, 60*60*24*7
			);
		PTRACEX(5, "GK\tWeekly rotation enabled (first rotation scheduled at "
			<< rotateTime
		      );
		break;
		
	case Monthly:
		rotateTime = PTime(0, m_rotateMinute, m_rotateHour, 1,
			now.GetMonth(), now.GetYear(), now.GetTimeZone()
			);
		rotateTime += PTimeInterval(0, 0, 0, 0, m_rotateDay - 1);
		while (rotateTime.GetMonth() != now.GetMonth())				
			rotateTime -= PTimeInterval(0, 0, 0, 0, 1); // 1 day

		if (rotateTime <= now) {
			rotateTime = PTime(0, m_rotateMinute, m_rotateHour, 1,
				now.GetMonth() + (now.GetMonth() == 12 ? -11 : 1),
				now.GetYear() + (now.GetMonth() == 12 ? 1 : 0),
				now.GetTimeZone()
				);
			const int month = rotateTime.GetMonth();
			rotateTime += PTimeInterval(0, 0, 0, 0, m_rotateDay - 1);
			while (rotateTime.GetMonth() != month)				
				rotateTime -= PTimeInterval(0, 0, 0, 0, 1); // 1 day
		}
					
		m_rotateTimer = Toolkit::Instance()->GetTimerManager()->RegisterTimer(
			&Gatekeeper::RotateOnTimer, rotateTime
			);
		PTRACEX(5, "GK\tMonthly rotation enabled (first rotation scheduled at "
			<< rotateTime
			);
		break;
	}
}

void Gatekeeper::RotateOnTimer(
	GkTimer* timer
	)
{
	m_logFileMutex.Wait();
	if (m_rotateInterval == Monthly) {
		// setup next time for one-shot timer
		const PTime& rotateTime = timer->GetExpirationTime();
		PTime newRotateTime(rotateTime.GetSecond(), rotateTime.GetMinute(),
			rotateTime.GetHour(), 1,
			rotateTime.GetMonth() < 12 ? rotateTime.GetMonth() + 1 : 1,
			rotateTime.GetMonth() < 12 ? rotateTime.GetYear() : rotateTime.GetYear() + 1,
			rotateTime.GetTimeZone()
			);
	
		newRotateTime += PTimeInterval(0, 0, 0, 0, m_rotateDay - 1);
		
		const int month = newRotateTime.GetMonth();
		while (newRotateTime.GetMonth() != month)				
			newRotateTime -= PTimeInterval(0, 0, 0, 0, 1); // 1 day
		
		timer->SetExpirationTime(newRotateTime);
		timer->SetFired(false);
	}	
	m_logFileMutex.Signal();
	RotateLogFile();
}

bool Gatekeeper::SetLogFilename(
	const PString& filename
	)
{
	if (filename.IsEmpty())
		return false;
		
	PWaitAndSignal lock(m_logFileMutex);
	if (!m_logFilename && m_logFile != NULL && m_logFile->IsOpen()
		&& m_logFilename == filename)
		return true;

	if (m_logFile) {
		PTRACEX(1, "GK\tLogging redirected to the file '" << filename << '\'');
		EnableLogFileRotation(false);
	}
	
	PTrace::SetStream(&cerr);
	
#ifndef hasDeletingSetStream
	delete m_logFile;
#endif
	m_logFile = NULL;
	
	m_logFilename = filename;
	m_logFile = new PTextFile(m_logFilename, PFile::WriteOnly, PFile::Create);
	if (!m_logFile->IsOpen()) {
		delete m_logFile;
		m_logFile = NULL;
		return false;
	}
	m_logFile->SetPosition(0, PFile::End);
	PTrace::SetStream(m_logFile);
	return true;	
}
		
bool Gatekeeper::RotateLogFile()
{
	PWaitAndSignal lock(m_logFileMutex);

	if (m_logFile) {
		PTRACEX(1, "GK\tLogging closed (log file rotation)");
		PTrace::SetStream(&cerr); // redirect to cerr
#ifndef hasDeletingSetStream
		delete m_logFile;
#endif
		m_logFile = NULL;
	}

	if (m_logFilename.IsEmpty())
		return false;
	
	PFile* const oldLogFile = new PTextFile(m_logFilename, PFile::WriteOnly,
		PFile::MustExist
		);
	if (oldLogFile->IsOpen()) {
		// Backup of log file
		PFilePath filename = oldLogFile->GetFilePath();
		const PString timeStr = PTime().AsString("yyyyMMdd_hhmmss");
		const PINDEX lastDot = filename.FindLast('.');
		if (lastDot != P_MAX_INDEX)
			filename.Replace(".", "." + timeStr + ".", FALSE, lastDot);
		else
			filename += "." + timeStr;
		oldLogFile->Close();
		oldLogFile->Move(oldLogFile->GetFilePath(), filename);
	}
	delete oldLogFile;
		
	m_logFile = new PTextFile(m_logFilename, PFile::WriteOnly, PFile::Create);
	if (!m_logFile->IsOpen()) {
		cerr << "Warning: could not open the log file \""
		     << m_logFilename << "\" after rotation" << endl;
		delete m_logFile;
		m_logFile = NULL;
		return false;
	}

	m_logFile->SetPosition(0, PFile::End);
	PTrace::SetStream(m_logFile);
	PTRACEX(1, "GK\tLogging restarted.");
	return true;
}
	
bool Gatekeeper::ReopenLogFile()
{
	PWaitAndSignal lock(m_logFileMutex);

	if (m_logFile) {
		PTRACEX(1, "GK\tLogging closed (reopen log file)");
		PTrace::SetStream(&cerr); // redirect to cerr
#ifndef hasDeletingSetStream
		delete m_logFile;
#endif
		m_logFile = NULL;
	}

	if (m_logFilename.IsEmpty())
		return false;
	
	m_logFile = new PTextFile(m_logFilename, PFile::WriteOnly,
		PFile::MustExist
		);
	if (!m_logFile->IsOpen()) {
		delete m_logFile;
		m_logFile = NULL;
	}
	
	if (m_logFile == NULL) {	
		m_logFile = new PTextFile(m_logFilename, PFile::WriteOnly, PFile::Create);
		if (!m_logFile->IsOpen()) {
			cerr << "Warning: could not open the log file \""
			     << m_logFilename << "\" after rotation" << endl;
			delete m_logFile;
			m_logFile = NULL;
			return false;
		}
	}
	m_logFile->SetPosition(0, PFile::End);
	PTrace::SetStream(m_logFile);
	PTRACEX(1, "GK\tLogging restarted");
	return true;
}

void Gatekeeper::CloseLogFile()
{
	PWaitAndSignal lock(m_logFileMutex);

	if (m_logFile) {
		PTRACEX(1, "GK\tLogging closed");
	}
	PTrace::SetStream(&cerr);
#ifndef hasDeletingSetStream
	delete m_logFile;
#endif
	m_logFile = NULL;
}
#endif // PTRACING
