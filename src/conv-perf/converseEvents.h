
#ifndef __CONVERSE_EVENTS_H__
#define __CONVERSE_EVENTS_H__

#include "traceCoreAPI.h"
#include "converseProjections.h"

/* Language ID */
#define _CONVERSE_LANG_ID	1	// language ID for converse

/* Event IDs */
#define _E_MSG_SENT			0
#define _E_MSG_QUEUED		1 	// DOUBT: Queued where ?
#define _E_MSG_RECV_MC		2	// Message received in machine layer
#define _E_MSG_RECV_SC		3	// Message received in scheduler
#define _E_HANDLER_BEGIN 	4
#define _E_HANDLER_END		5 

/* Trace Macros */
#define REGISTER_CONVERSE \
	{ RegisterLanguage(_CONVERSE_LANG_ID, "converse\0"); \
	  RegisterEvent(_CONVERSE_LANG_ID, _E_MSG_SENT     ); \
	  RegisterEvent(_CONVERSE_LANG_ID, _E_MSG_QUEUED   ); \
	  RegisterEvent(_CONVERSE_LANG_ID, _E_MSG_RECV_MC  ); \
	  RegisterEvent(_CONVERSE_LANG_ID, _E_MSG_RECV_SC  ); \
	  RegisterEvent(_CONVERSE_LANG_ID, _E_HANDLER_BEGIN); \
	  RegisterEvent(_CONVERSE_LANG_ID, _E_HANDLER_END  ); \
	  \
	}

#ifdef CMK_OPTIMIZE
#define _LOG_E_MSG_SENT(destPE, size)
#define _LOG_E_MSG_QUEUED()
#define _LOG_E_MSG_RECV_MC()
#define _LOG_E_MSG_RECV_SC()
#define _LOG_E_HANDLER_BEGIN(handlerIdx)
#define _LOG_E_HANDLER_END(handlerIdx)
#else
#define _LOG_E_MSG_SENT(destPE, size) \
	{ converse_msgSent(destPE, size); }
#define _LOG_E_MSG_QUEUED() \
	{ LogEvent(_CONVERSE_LANG_ID, _E_MSG_QUEUED); }		//TODO
#define _LOG_E_MSG_RECV_MC() \
	{ LogEvent(_CONVERSE_LANG_ID, _E_MSG_RECV_MC); }	//TODO
#define _LOG_E_MSG_RECV_SC() \
	{ LogEvent(_CONVERSE_LANG_ID, _E_MSG_RECV_SC); }	//TODO
#define _LOG_E_HANDLER_BEGIN(handlerIdx) \
	{ converse_handlerBegin(handlerIdx); }
#define _LOG_E_HANDLER_END(handlerIdx) \
	{ converse_handlerEnd(handlerIdx); }
#endif

#endif
