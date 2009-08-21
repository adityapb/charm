/*
Converse-level debugger support

Collected from convcore.c, conv-ccs.c, register.c by
Orion Sky Lawlor, olawlor@acm.org, 4/10/2001
 */
#include <stdio.h> /*for sscanf*/
#include <string.h> /*for strcmp*/
#include "converse.h"
#include "conv-trace.h"
#include "queueing.h"
#include "conv-ccs.h"
#include <errno.h>

CpvStaticDeclare(int, freezeModeFlag);
CpvStaticDeclare(int, continueFlag);
CpvStaticDeclare(int, stepFlag);
CpvDeclare(void *, debugQueue);
int _debugHandlerIdx;
CpvDeclare(int, skipBreakpoint); /* This is a counter of how many breakpoints we should skip */

char ** memoryBackup;

#if ! CMK_HAS_NTOHL
uint32_t ntohl(uint32_t netlong) {
  union { uint32_t i; unsigned char c[4]; } uaw;
  uaw.i = netlong;
  netlong = uaw.c[0]<<24 + uaw.c[1]<<16 + uaw.c[2]<<8 + uaw.c[3];
  return netlong;
}
#endif

/***************************************************
  The CCS interface to the debugger
*/

#include <string.h>

#include "pup_c.h"

CpvDeclare(int, CpdSearchLeaks_Index);
CpvDeclare(int, CpdSearchLeaksDone_Index);
CpvStaticDeclare(CcsDelayedReply, leakSearchDelayedReply);

void CpdSearchLeaksDone(void *msg) {
  CmiInt4 ok = 1;
  CcsSendDelayedReply(CpvAccess(leakSearchDelayedReply), 4, &ok);
  CmiFree(msg);
}

void CpdSearchLeaks(char * msg) {
  LeakSearchInfo *info = (LeakSearchInfo *)(msg+CmiMsgHeaderSizeBytes);
  if (CmiMyPe() == info->pe || (info->pe == -1 && CmiMyPe() == 0)) {
    if (sizeof(char*) == 8) {
      info->begin_data = (((CmiUInt8)ntohl(((int*)&info->begin_data)[0]))<<32) + ntohl(((int*)&info->begin_data)[1]);
      info->end_data = (((CmiUInt8)ntohl(((int*)&info->end_data)[0]))<<32) + ntohl(((int*)&info->end_data)[1]);
      info->begin_bss = (((CmiUInt8)ntohl(((int*)&info->begin_bss)[0]))<<32) + ntohl(((int*)&info->begin_bss)[1]);
      info->end_bss = (((CmiUInt8)ntohl(((int*)&info->end_bss)[0]))<<32) + ntohl(((int*)&info->end_bss)[1]);
    } else {
      info->begin_data = ntohl((int)info->begin_data);
      info->end_data = ntohl((int)info->end_data);
      info->begin_bss = ntohl((int)info->begin_bss);
      info->end_bss = ntohl((int)info->end_bss);
    }
    info->quick = ntohl(info->quick);
    info->pe = ntohl(info->pe);
    CpvAccess(leakSearchDelayedReply) = CcsDelayReply();
    if (info->pe == -1) {
      CmiSetXHandler(msg, CpvAccess(CpdSearchLeaks_Index));
      CmiSetHandler(msg, _debugHandlerIdx);
      CmiSyncBroadcast(CmiMsgHeaderSizeBytes+sizeof(LeakSearchInfo), msg);
    }
  }
  check_memory_leaks(info);
  if (info->pe == CmiMyPe()) CpdSearchLeaksDone(msg);
  else if (info->pe == -1) {
    void *reduceMsg = CmiAlloc(0);
    CmiSetHandler(reduceMsg, CpvAccess(CpdSearchLeaksDone_Index));
    CmiReduce(reduceMsg, CmiMsgHeaderSizeBytes, CmiReduceMergeFn_random);
    CmiFree(msg);
  }
  else CmiAbort("Received allocationTree request for another PE!");
}

void * (*CpdDebugGetAllocationTree)(int *);
void (*CpdDebug_pupAllocationPoint)(pup_er p, void *data);
void (*CpdDebug_deleteAllocationPoint)(void *ptr);
void * (*CpdDebug_MergeAllocationTree)(int *size, void *data, void **remoteData, int numRemote);
CpvDeclare(int, CpdDebugCallAllocationTree_Index);
CpvStaticDeclare(CcsDelayedReply, allocationTreeDelayedReply);

static void CpdDebugReturnAllocationTree(void *tree) {
  pup_er sizer = pup_new_sizer();
  char *buf;
  pup_er packer;
  int i;
  CpdDebug_pupAllocationPoint(sizer, tree);
  buf = (char *)malloc(pup_size(sizer));
  packer = pup_new_toMem(buf);
  CpdDebug_pupAllocationPoint(packer, tree);
  /*CmiPrintf("size=%d tree:",pup_size(sizer));
  for (i=0;i<100;++i) CmiPrintf(" %02x",((unsigned char*)buf)[i]);
  CmiPrintf("\n");*/
  CcsSendDelayedReply(CpvAccess(allocationTreeDelayedReply), pup_size(sizer),buf);
  pup_destroy(sizer);
  pup_destroy(packer);
  free(buf);
}

static void CpdDebugCallAllocationTree(char *msg)
{
  int numNodes;
  int forPE;
  void *tree;
  sscanf(msg+CmiMsgHeaderSizeBytes, "%d", &forPE);
  if (CmiMyPe() == forPE) CpvAccess(allocationTreeDelayedReply) = CcsDelayReply();
  if (forPE == -1 && CmiMyPe()==0) {
    CpvAccess(allocationTreeDelayedReply) = CcsDelayReply();
    CmiSetXHandler(msg, CpvAccess(CpdDebugCallAllocationTree_Index));
    CmiSetHandler(msg, _debugHandlerIdx);
    CmiSyncBroadcast(CmiMsgHeaderSizeBytes+strlen(msg+CmiMsgHeaderSizeBytes)+1, msg);
  }
  tree = CpdDebugGetAllocationTree(&numNodes);
  if (forPE == CmiMyPe()) CpdDebugReturnAllocationTree(tree);
  else if (forPE == -1) CmiReduceStruct(tree, CpdDebug_pupAllocationPoint, CpdDebug_MergeAllocationTree,
                                CpdDebugReturnAllocationTree, CpdDebug_deleteAllocationPoint);
  else CmiAbort("Received allocationTree request for another PE!");
  CmiFree(msg);
}

void * (*CpdDebugGetMemStat)(void);
void (*CpdDebug_pupMemStat)(pup_er p, void *data);
void (*CpdDebug_deleteMemStat)(void *ptr);
void * (*CpdDebug_mergeMemStat)(int *size, void *data, void **remoteData, int numRemote);
CpvDeclare(int, CpdDebugCallMemStat_Index);
CpvStaticDeclare(CcsDelayedReply, memStatDelayedReply);

static void CpdDebugReturnMemStat(void *stat) {
#if CMK_CCS_AVAILABLE
  pup_er sizerNet = pup_new_network_sizer();
  pup_er sizer = pup_new_fmt(sizerNet);
  char *buf;
  pup_er packerNet;
  pup_er packer;
  int i;
  CpdDebug_pupMemStat(sizer, stat);
  buf = (char *)malloc(pup_size(sizer));
  packerNet = pup_new_network_pack(buf);
  packer = pup_new_fmt(packerNet);
  CpdDebug_pupMemStat(packer, stat);
  /*CmiPrintf("size=%d tree:",pup_size(sizer));
  for (i=0;i<100;++i) CmiPrintf(" %02x",((unsigned char*)buf)[i]);
  CmiPrintf("\n");*/
  CcsSendDelayedReply(CpvAccess(memStatDelayedReply), pup_size(sizer),buf);
  pup_destroy(sizerNet);
  pup_destroy(sizer);
  pup_destroy(packerNet);
  pup_destroy(packer);
  free(buf);
#endif
}

static void CpdDebugCallMemStat(char *msg) {
  int forPE;
  void *stat;
  sscanf(msg+CmiMsgHeaderSizeBytes, "%d", &forPE);
  if (CmiMyPe() == forPE) CpvAccess(memStatDelayedReply) = CcsDelayReply();
  if (forPE == -1 && CmiMyPe()==0) {
    CpvAccess(memStatDelayedReply) = CcsDelayReply();
    CmiSetXHandler(msg, CpvAccess(CpdDebugCallMemStat_Index));
    CmiSetHandler(msg, _debugHandlerIdx);
    CmiSyncBroadcast(CmiMsgHeaderSizeBytes+strlen(msg+CmiMsgHeaderSizeBytes)+1, msg);
  }
  stat = CpdDebugGetMemStat();
  if (forPE == CmiMyPe()) CpdDebugReturnMemStat(stat);
  else if (forPE == -1) CmiReduceStruct(stat, CpdDebug_pupMemStat, CpdDebug_mergeMemStat,
                                CpdDebugReturnMemStat, CpdDebug_deleteMemStat);
  else CmiAbort("Received allocationTree request for another PE!");
  CmiFree(msg);
}

static void * CpdDebugMerge(int *size,void *local,void **remote,int n) {
  void *reply;
#if CMK_CCS_AVAILABLE
  CcsImplHeader *hdr;
  int total = *size;
  int i;
  for (i=0; i<n; ++i) {
    hdr = (CcsImplHeader*)(((char*)remote[i])+CmiMsgHeaderSizeBytes);
    total += ChMessageInt(hdr->len);
  }
  reply = CmiAlloc(total);
  memcpy(reply, local, *size);
  ((CcsImplHeader*)(((char*)reply)+CmiMsgHeaderSizeBytes))->len = ChMessageInt_new(total-CmiMsgHeaderSizeBytes-sizeof(CcsImplHeader));
  CmiFree(local);
  char *ptr = ((char*)reply)+*size;
  for (i=0; i<n; ++i) {
    int len = ChMessageInt(((CcsImplHeader*)(((char*)remote[i])+CmiMsgHeaderSizeBytes))->len);
    memcpy(ptr, ((char*)remote[i])+CmiMsgHeaderSizeBytes+sizeof(CcsImplHeader), len);
    ptr += len;
  }
  *size = total;
#endif
  return reply;
}

static void CpdDebugHandler(char *msg)
{
    char name[128];
    sscanf(msg+CmiMsgHeaderSizeBytes, "%s", name);

    if (strcmp(name, "freeze") == 0) {
      CpdFreeze();
    }
    else if (strcmp(name, "unfreeze") == 0) {
      CpdUnFreeze();
    }
    else if (strncmp(name, "step", strlen("step")) == 0){
      /*CmiPrintf("step received\n");*/
      CpvAccess(stepFlag) = 1;
      CpdUnFreeze();
    }
    else if (strncmp(name, "continue", strlen("continue")) == 0){
      /*CmiPrintf("continue received\n");*/
      CpvAccess(continueFlag) = 1;
      CpdUnFreeze();
    }
    else if (strncmp(name, "status", strlen("status")) == 0) {
      ChMessageInt_t reply[2];
      reply[0] = ChMessageInt_new(CmiMyPe());
      reply[1] = ChMessageInt_new(CpdIsFrozen() ? 0 : 1);
      CcsSendReply(2*sizeof(ChMessageInt_t), reply);
    }
#if 0
    else if (strncmp(name, "setBreakPoint", strlen("setBreakPoint")) == 0){
      CmiPrintf("setBreakPoint received\n");
      temp = strstr(name, "#");
      temp++;
      setBreakPoints(temp);
    }
#endif
    else{
      CmiPrintf("bad debugger command:%s received,len=%ld\n",name,strlen(name));
    }
    CmiFree(msg);
}


/*
 Start the freeze-- call will not return until unfrozen
 via a CCS request.
 */
void CpdFreeze(void)
{
  CpdNotify(CPD_FREEZE,getpid());
  if (CpvAccess(freezeModeFlag)) return; /*Already frozen*/
  CpvAccess(freezeModeFlag) = 1;
  CpdFreezeModeScheduler();
}

void CpdUnFreeze(void)
{
  CpvAccess(freezeModeFlag) = 0;
}

int CpdIsFrozen(void) {
  return CpvAccess(freezeModeFlag);
}

/* Deliver a single message in the queue while not unfreezing the program */
void CpdNext(void) {

}

/* This converse handler is used by the debugger itself, to send messages
 * even when the scheduler is in freeze mode.
 */
void handleDebugMessage(void *msg) {
  CmiSetHandler(msg, CmiGetXHandler(msg));
  CmiHandleMessage(msg);
}

/* Special scheduler-type loop only executed while in
freeze mode-- only executes CCS requests.
*/
void CcsServerCheck(void);
extern int _isCcsHandlerIdx(int idx);
int (*CpdIsDebugMessage)(void *);

void CpdFreezeModeScheduler(void)
{
#if CMK_CCS_AVAILABLE
    void *msg;
    void *debugQ=CpvAccess(debugQueue);
    CsdSchedulerState_t state;
    CsdSchedulerState_new(&state);

    /* While frozen, queue up messages */
    while (CpvAccess(freezeModeFlag)) {
#if NODE_0_IS_CONVHOST
      if (CmiMyPe()==0) CcsServerCheck(); /*Make sure we can get CCS messages*/
#endif
      msg = CsdNextMessage(&state);

      if (msg!=NULL) {
        /*int hIdx=CmiGetHandler(msg);*/
	  /*
	  if(_isCcsHandlerIdx(hIdx))
	  / *A CCS request-- handle it immediately* /
          {
	    CmiHandleMessage(msg);
          }
	  else if (hIdx == _debugHandlerIdx ||
	          (hIdx == CmiGetReductionHandler() && CmiGetReductionDestination() == CpdDebugReturnAllocationTree)) {
	    / * Debug messages should be handled immediately * /
	    CmiHandleMessage(msg);
	  } else */
	  if (CpdIsDebugMessage(msg)) {
	    CmiHandleMessage(msg);
	  }
	  else
	  /*An ordinary charm++ message-- queue it up*/
	    CdsFifo_Enqueue(debugQ, msg);
      } else CmiNotifyIdle();
    }
    /* Before leaving freeze mode, execute the messages
       in the order they would have executed before.*/
    while (!CdsFifo_Empty(debugQ))
    {
	char *queuedMsg = (char *)CdsFifo_Dequeue(debugQ);
        CmiHandleMessage(queuedMsg);
    }
#endif
}


void CpdInit(void)
{
  CpvInitialize(int, freezeModeFlag);
  CpvAccess(freezeModeFlag) = 0;

  CpvInitialize(void *, debugQueue);
  CpvAccess(debugQueue) = CdsFifo_Create();

  CcsRegisterHandler("ccs_debug", (CmiHandler)CpdDebugHandler);
  CcsSetMergeFn("ccs_debug", CpdDebugMerge);

  CcsRegisterHandler("ccs_debug_allocationTree", (CmiHandler)CpdDebugCallAllocationTree);
  CpvInitialize(int, CpdDebugCallAllocationTree_Index);
  CpvAccess(CpdDebugCallAllocationTree_Index) = CmiRegisterHandler((CmiHandler)CpdDebugCallAllocationTree);
  
  CcsRegisterHandler("ccs_debug_memStat", (CmiHandler)CpdDebugCallMemStat);
  CpvInitialize(int, CpdDebugCallMemStat_Index);
  CpvAccess(CpdDebugCallMemStat_Index) = CmiRegisterHandler((CmiHandler)CpdDebugCallMemStat);

  CcsRegisterHandler("converse_memory_leak",(CmiHandler)CpdSearchLeaks);
  CpvInitialize(int, CpdSearchLeaks_Index);
  CpvAccess(CpdSearchLeaks_Index) = CmiRegisterHandler((CmiHandler)CpdSearchLeaks);
  CpvInitialize(int, CpdSearchLeaksDone_Index);
  CpvAccess(CpdSearchLeaksDone_Index) = CmiRegisterHandler((CmiHandler)CpdSearchLeaksDone);
  
  _debugHandlerIdx = CmiRegisterHandler((CmiHandler)handleDebugMessage);
#if 0
  CpdInitializeObjectTable();
  CpdInitializeHandlerArray();
  CpdInitializeBreakPoints();

  /* To allow start in freeze state: */
  msgListCleanup();
  msgListCache();
#endif

}

#include <stdarg.h>
void CpdNotify(int type, ...) {
  va_list list;
  va_start(list, type);
  switch (type) {
  case CPD_ABORT:
    CmiPrintf("CPD: %d Abort %s\n",CmiMyPe(), va_arg(list, char*));
    break;
  case CPD_SIGNAL:
    CmiPrintf("CPD: %d Signal %d\n",CmiMyPe(), va_arg(list, int));
    break;
  case CPD_FREEZE:
    CmiPrintf("CPD: %d Freeze %d\n",CmiMyPe(),getpid());
    break;
  case CPD_BREAKPOINT:
    CmiPrintf("CPD: %d BP %s\n",CmiMyPe(), va_arg(list, char*));
    break;
  }
  va_end(list);
}
