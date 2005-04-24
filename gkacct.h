/*
 * gkacct.h
 *
 * Accounting modules for GNU Gatekeeper. Provides generic
 * support for accounting to the gatekeeper.
 *
 * Copyright (c) 2003, Quarcom FHU, Michal Zygmuntowicz
 *
 * This work is published under the GNU Public License (GPL)
 * see file COPYING for details.
 * We also explicitely grant the right to link this code
 * with the OpenH323 library.
 *
 * $Log$
 * Revision 1.17  2005/01/17 08:42:25  zvision
 * Compilation error fixed (missing std:: prefix before map)
 *
 * Revision 1.16  2005/01/16 22:37:15  zvision
 * Redundant config reload mutex removed
 *
 * Revision 1.15  2005/01/10 23:49:06  willamowius
 * provide mechanism for accounting modules to escape the parameters
 *
 * Revision 1.14  2005/01/05 15:42:40  willamowius
 * new accounting event 'connect', parameter substitution unified in parent class
 *
 * Revision 1.13  2004/11/15 23:57:41  zvision
 * Ability to choose between the original and the rewritten dialed number
 *
 * Revision 1.12  2004/11/10 18:30:41  zvision
 * Ability to customize timestamp strings
 *
 * Revision 1.11  2004/06/25 13:33:18  zvision
 * Better Username, Calling-Station-Id and Called-Station-Id handling.
 * New SetupUnreg option in Gatekeeper::Auth section.
 *
 * Revision 1.10  2004/05/22 12:17:12  zvision
 * Parametrized FileAcct CDR format
 *
 * Revision 1.9  2004/05/12 11:49:08  zvision
 * New flexible CDR file rotation
 *
 * Revision 1.8  2004/04/17 11:43:42  zvision
 * Auth/acct API changes.
 * Header file usage more consistent.
 *
 * Revision 1.7  2003/10/31 00:01:23  zvision
 * Improved accounting modules stacking control, optimized radacct/radauth a bit
 *
 * Revision 1.6  2003/10/08 12:40:48  zvision
 * Realtime accounting updates added
 *
 * Revision 1.5  2003/09/29 16:11:44  zvision
 * Added cvs Id keyword to header #define macro
 *
 * Revision 1.4  2003/09/16 11:25:10  zvision
 * Optimization for configurations without accounting enabled
 *
 * Revision 1.3  2003/09/14 21:09:29  zvision
 * Added new FileAcct logger from Tamas Jalsovszky. Thanks!
 * Fixed module stacking. Redesigned API.
 *
 * Revision 1.2  2003/09/12 16:31:16  zvision
 * Accounting initially added to the 2.2 branch
 *
 * Revision 1.1.2.1  2003/06/19 15:36:04  zvision
 * Initial generic accounting support for GNU GK.
 *
 */
#ifndef __GKACCT_H
#define __GKACCT_H "@(#) $Id$"

#include <list>
#include "name.h"
#include "factory.h"
#include "RasTbl.h"

/** Module for logging accounting events
	generated by the gatekeeper.
*/
class GkAcctLogger : public NamedObject
{
public:
	/// Processing type for this module
	enum Control 
	{
		/// if cannot log an accounting event
		/// silently ignore the module and process remaining acct modules
		Optional, 
		/// if cannot log an accounting event do not allow futher 
		/// call processing (e.g. call should not be connected, etc.)
		/// always process remaining acct modules
		Required,
		/// if cannot log an accounting event do not allow futher 
		/// call processing (e.g. call should not be connected, etc.)
		/// - always process remaining acct modules,
		/// if the event has been logged, do not process remaining acct modules
		/// and allow further call processing
		Sufficient,
		/// if cannot log an accounting event ignore the module
		/// and process remaining acct modules,
		/// if the event has been logged, do not process remaining acct modules
		/// and allow further call processing
		Alternative
	};

	/// status of the acct event processing
	enum Status 
	{
		Ok = 1,		/// acct event has been logged
		Fail = -1,	/// acct event has not been logged (failure)
		Next = 0	/// acct event has not been logged because the event type
					/// is not supported/not configured for this module
	};

	/// accounting event types
	enum AcctEvent 
	{
		AcctStart = 0x0001, /// log call start
		AcctStop = 0x0002, /// log call stop (disconnect)
		AcctUpdate = 0x0004, /// /// call progress update
		AcctOn = 0x0008, /// accounting enabled (GK start)
		AcctOff = 0x0010, /// accounting disabled (GK stop)
		AcctConnect = 0x0020, /// call connected
		AcctAll = -1,
		AcctNone = 0
	};
	
	/// Construct new accounting logger object.
	GkAcctLogger(
		/// module name (should be unique among different module types)
		const char* moduleName,
		/// name for a config section with logger settings
		/// pass NULL to use the moduleName as the section name
		const char* cfgSecName = NULL
		);
		
	virtual ~GkAcctLogger();

	/** @return
		Control flag determining processing behaviour for this module
		(optional,sufficient,required).
	*/
	Control GetControlFlag() const { return m_controlFlag; }

	/** @return
		Flags signalling which accounting events (see #AcctEvent enum#)
		should be logged. The flags are ORed.
	*/
	int GetEnabledEvents() const { return m_enabledEvents; }

	/** @return
		All events supported by the module ORed together.
	*/
	int GetSupportedEvents() const { return m_supportedEvents; }

	/** Log an accounting event with this logger.
	
		@return
		Status of this logging operation (see #Status enum#)
	*/
	virtual Status Log(		
		AcctEvent evt, /// accounting event to log
		const callptr& call /// a call associated with the event (if any)
		);

protected:
	/** @return
		A pointer to configuration settings for this logger.
	*/
	PConfig* GetConfig() const { return m_config; }
	
	/** @return
		A name of the config file section with settings for the logger module.
	*/
	const PString& GetConfigSectionName() const { return m_configSectionName; }

	/** Set which accounting events should be processed by this module.
		It is important to call this from derived module constructor
		to set which accounting events are recognized by this module.
	*/
	void SetSupportedEvents(
		const int events
		) { m_supportedEvents = events; }

	/** Fill the map with accounting parameters (name => value associations).
	*/	
	virtual void SetupAcctParams(
		/// accounting parameters (name => value) associations
		std::map<PString, PString>& params,
		/// call (if any) associated with an accounting event being logged
		const callptr& call,
		/// timestamp formatting string
		const PString& timestampFormat
		) const;

	/** Replace accounting parameters placeholders (%a, %{Name}, ...) with 
	    actual values.

	    @return
	    New string with all parameters replaced.
	*/
	PString ReplaceAcctParams(
		/// parametrized accounting string
		const PString& cdrStr,
		/// parameter values
		const std::map<PString, PString>& params
	) const;

	/** Escape accounting parameters; called for each value before inserting.
		Subclass this for all accounting modules that need escaping.
		Default implementation doesn't modify the parameter.

		@return
		escaped string
	 */
	virtual PString EscapeAcctParam(const PString& param) const;
		
	/** Read a list of events to be logged (ORed #AccEvent enum# constants) 
		from the passed tokens. Override this method if new event types
		are being introduced in derived loggers, then invokde it from constructor.
	
		@return
		Resulting event mask.
	*/
	int GetEvents( 
		const PStringArray& tokens /// event names (start from index 1)
		) const;

	/** @return
	    A string that can be used to identify an account name
	    associated with the call.
	*/
	virtual PString GetUsername(
		/// call (if any) associated with the RAS message
		const callptr& call
		) const;

	/** @return
	    A string that can be used to identify a calling number.
	*/
	virtual PString GetCallingStationId(
		/// call associated with the accounting event
		const callptr& call
		) const;

	/** @return
	    A string that can be used to identify a calling number.
	*/
	virtual PString GetCalledStationId(
		/// call associated with the accounting event
		const callptr& call
		) const;

	/// @return	A number actually dialed by the user (before rewrite)
	virtual PString GetDialedNumber(
		/// call associated with the accounting event
		const callptr& call
		) const;

private:
	GkAcctLogger();
	GkAcctLogger(const GkAcctLogger&);
	GkAcctLogger & operator=(const GkAcctLogger&);
	 
private:
	/// processing behaviour (see #Control enum#)
	Control m_controlFlag;
	/// status for "default" logger - accept (Ok) or reject (Fail)
	Status m_defaultStatus;
	/// ORed #AcctEvent enum# constants - define which events are logged
	int m_enabledEvents;
	/// all supported (recongized) event types ORed together
	int m_supportedEvents;
	/// module settings
	PConfig* m_config;
	/// name for the config section with logger settings
	PString m_configSectionName;
};

/**
	Plain text file accounting module for GNU Gatekeeper.
	Based on source source code from Tamas Jalsovszky
		Copyright (c) 2003, eWorld Com, Tamas Jalsovszky
*/
class GkTimer;
class FileAcct : public GkAcctLogger
{
public:
	enum Constants {
		/// events recognized by this module
		FileAcctEvents = AcctStop
	};

	enum RotationIntervals {
		Hourly,
		Daily,
		Weekly,
		Monthly,
		RotationIntervalMax
	};
	
	/// Create GkAcctLogger for plain text file accounting
	FileAcct( 
		/// name from Gatekeeper::Acct section
		const char* moduleName,
		/// name for a config section with logger settings
		/// pass NULL to use the moduleName as the section name
		const char* cfgSecName = NULL
		);
		
	/// Destroy the accounting logger
	virtual ~FileAcct();

	/// override from GkAcctLogger
	virtual Status Log(
		AcctEvent evt,
		const callptr& call
		);

	/** Rotate the detail file, saving old file contents to a different
	    file and starting with a new one. This is a callback function
	    called when the rotation timer expires.
	*/
	void RotateOnTimer(
		GkTimer* timer /// timer object that triggered rotation
		);
	
protected:
	/** Called to get CDR text to be stored in the CDR file.
		Can be overriden to provide custom CDR text.
		
		@return
		true if the text is available, false otherwise
	*/
	virtual bool GetCDRText(
		PString& cdrString, /// PString for the resulting CDR line
		AcctEvent evt, /// accounting event being processed
		const callptr& call /// call associated with this request (if any)
		);

	/** @return
	    True if the CDR file should be rotated.
	*/
	virtual bool IsRotationNeeded();
		
	/** @return
	    Pointer to the opened file or NULL, if the operation failed.
	*/
	virtual PTextFile* OpenCDRFile(
		const PFilePath& fn /// name of the file to open
		);
		
	/** Rotate the detail file, saving old file contents to a different
		file and starting with a new one.
	*/
	void Rotate();
		
private:
	/// parse rotation interval from the config
	void GetRotateInterval(
		PConfig& cfg, /// the config
		const PString& section /// name of the config section to check
		);
		
	/* No default constructor allowed */
	FileAcct();
	/* No copy constructor allowed */
	FileAcct(const FileAcct&);
	/* No operator= allowed */
	FileAcct& operator=(const FileAcct&);
	
private:
	/// Plain text file name
	PString m_cdrFilename;
	/// File object
	PTextFile* m_cdrFile;
	/// for mutual file access
	PMutex m_cdrFileMutex;
	/// rotate file after the specified amount of lines is reached (if > 0)
	long m_rotateLines;
	/// rotate file after the specified fize size is reached (if > 0)
	long m_rotateSize;
	/// rotate file after the specified period of time (if >= 0)
	int m_rotateInterval;
	/// a minute when the interval based rotation should occur
	int m_rotateMinute;
	/// an hour when the interval based rotation should occur
	int m_rotateHour;
	/// day of the month (or of the week) for the interval based rotation
	int m_rotateDay;
	/// timer for rotation events
	GkTimer* m_rotateTimer;
	/// number of CDRs written (if rotation per number of lines is enabled)
	long m_cdrLines;
	/// if true, ignore CDR string and write CDRs in a standard format
	bool m_standardCDRFormat;
	/// parametrized CDR string
	PString m_cdrString;
	/// timestamp formatting string
	PString m_timestampFormat;
	/// human readable names for rotation intervals
	static const char* const m_intervalNames[];
};

/// Factory for instances of GkAcctLogger-derived classes
template<class Acct>
struct GkAcctLoggerCreator : public Factory<GkAcctLogger>::Creator0 {
	GkAcctLoggerCreator(const char* moduleName) : Factory<GkAcctLogger>::Creator0(moduleName) {}
	virtual GkAcctLogger* operator()() const { return new Acct(m_id); }	
};

class GkAcctLoggerList 
{
public:
	/** Construct an empty list of accounting loggers.
	*/
	GkAcctLoggerList();
	
	/** Destroy the list of accounting loggers.
	*/
	~GkAcctLoggerList();

	/** Apply configuration changes to the list of accounting loggers.
		Usually it means destroying the old list and creating a new one.
	*/	
	void OnReload();
	
	/** Log accounting event with all active accounting loggers.
	
		@return
		true if the event has been successfully logged, false otherwise.
	*/
	bool LogAcctEvent( 
		GkAcctLogger::AcctEvent evt, /// the accounting event to be logged
		const callptr& call, /// a call associated with the event (if any)
		time_t now = 0 /// "now" timestamp for accounting update events
		);

private:
	GkAcctLoggerList(const GkAcctLogger&);
	GkAcctLoggerList& operator=(const GkAcctLoggerList&);
	
private:
	/// head of the accounting loggers list
	std::list<GkAcctLogger*> m_loggers;
	/// interval (seconds) between subsequent accounting updates for calls
	long m_acctUpdateInterval;
};

#endif  /* __GKACCT_H */
