#ifndef __IBT_MSGMGR_H__
#define __IBT_MSGMGR_H__

#include <semaphore.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
#include <pthread.h>
#include "IBT_List.h"



#define MAX_RECVD_MSG_CNT			32
#define MAX_RECVD_MSG_BUF_SIZE		2048
#define MAX_MSG_NAME_SIZE			32

typedef enum {
	IBT_INV_MSG_TYPE						= 0,
	IBT_FIRST_MSG_TYPE,
	IBT_TEST_MSG_TYPE,
	IBT_MAX_MSG_TYPE,
} IBTMsgType;

typedef enum {
	IBT_MSG_STATUS_UNREGISTER = 0,
	IBT_MSG_STATUS_REGISTER,
} IBTMsgRegStatus;

typedef enum {
	IBT_MSG_STATUS_EMPTY = 0,
	IBT_MSG_STATUS_USAGE,
} IBTMsgProcStatus;


typedef struct {
	IBTMsgType			msgType;    //Send msg type
	char				*msgRecvBuf; //send data pointer or recv data pointer address pointer
	unsigned int		msgRecvSize;  //send data size or recv data size
} IBTMsgRecvInfoCtx;

#define IBT_MSG_RECV_INFO_DEFAULT	{IBT_FIRST_MSG_TYPE, NULL, 0}


typedef struct {
	IBTList				msgRecvList;
	IBTMsgRecvInfoCtx	msgRecvInfoCtx;
} IBTMsgRecvInfo;

typedef struct {
	IBTMsgRecvInfo		*msgRecvInfo;
	unsigned int		msgRecvInfoCnt;
	unsigned int		msgRecvBufSize;

	IBTListQueue		msgRecvInitQueue;
	sem_t				msgRecvInitSem;
	IBTListQueue		msgRecvDoneQueue;
	sem_t				msgRecvDoneSem;
} IBTMsgRecv;

typedef struct IBTMsgInfo IBTMsgInfo;
typedef struct IBTMsgMgrHandler IBTMsgMgrHandler;

typedef struct {
	IBTMsgMgrHandler	*msgMgr;
} IBTHandler;

typedef int (*IBTMsgProcessFunc)(void* param, int paramSize, void* priv);

/*register msg info struct*/
struct IBTMsgInfo {
	IBTMsgType				msgType;  //msg type, refer to IBTMsgType
	char*				    msgName;  //msg name
	IBTMsgProcessFunc		msgProcess; //when recv msgType, call this function
	void*					msgProcessParam; //Intermediate variable pointer, used to temporarily store the data of the sending function
	unsigned int		    msgProcessParamSize; //Intermediate variable pointer data size
	char					priv[0];  //unused
};


typedef struct {
	pthread_mutex_t		msgMutex;
	pthread_cond_t		msgCond;
	IBTList				msgRegList;
	IBTMsgRegStatus		msgRegStatus;
	IBTList				msgProcList;
	IBTMsgProcStatus	msgProcStatus;
	IBTMsgInfo			msgInfo; /*support MsgType*/
} IBTMsg;

typedef struct {
	IBTMsg				*msg;   //all support msg pointer, set size to IBT_MAX_MSG_TYPE
	unsigned int		msgCnt; //supported msg number, refer to IBTMsgType
} IBTMsgPool;

typedef struct {
	pthread_mutex_t		msgRegMutex;
	IBTListQueue		msgRegQueue;
} IBTMsgReg;

typedef struct {
	IBTListQueue		msgProcDoneQueue;
	sem_t				msgProcDoneSem;
	pthread_t			msgListenTid;
	pthread_t			msgProcessTid;
} IBTMsgProc;

struct IBTMsgMgrHandler {
	IBTMsgRecv			msgRecv;
	IBTMsgPool			msgPool; //Store supported msg information
	IBTMsgReg			msgReg;
	IBTMsgProc			msgProc;
};


/**
 * @fn void IBT_MsgMgr_Init()
 * @brief
 * 		msgmgr?????????
 * @pre
 * @post
 * @return
 */
void IBT_MsgMgr_Init();
/**
 * @fn IBT_VOID IBT_MsgMgr_DeInit()
 * @brief
 * 		msgmgr????????????
 * @pre
 * @post
 * @return
 */
void IBT_MsgMgr_DeInit();
/**
 * @fn IBT_INT IBT_MsgMgr_RegisterMsgs(IBTMsgInfo*)
 * @brief
 * 		????????????
 * @pre
 * @post
 * @param msgInfo	????????????????????????
 * @return
 */
int IBT_MsgMgr_RegisterMsgs(IBTMsgInfo *msgInfo);
/**
 * @fn IBT_INT IBT_MsgMgr_UnRegisterMsgs(IBTMsgType, IBT_BOOL)
 * @brief
 * 		??????????????????
 * @pre
 * @post
 * @param msgType		????????????
 * @param isFreeParam	????????????msgProcessParam
 * @return
 */
int IBT_MsgMgr_UnRegisterMsgs(IBTMsgType msgType, bool isFreeParam);
/**
 * @fn IBT_INT IBT_MsgMgr_SendMsgs(IBTMsgMgrHandler*, IBTMsgRecvInfoCtx*, IBT_INT)
 * @brief
 * 		??????????????????????????????????????????????????????timeOutMs????????????0
 *
 * 		????????????????????????0???
 * 		???????????????????????????????????????????????????????????????IBT_MsgMgr_ImmediateResponseMsgs??????????????????????????????????????????
 * 		??????????????????????????????????????????IBT_MsgMgr_ImmediateResponseMsgs?????????????????????????????????????????????
 * @pre
 * @post
 * @param msgCodeCtx ???????????????
 * @param timeOutMs  ??????????????????????????????
 * @return 0????????? -1?????????
 */
int IBT_MsgMgr_SendMsgs(IBTMsgRecvInfoCtx *msgCodeCtx, unsigned int timeOutMs);
/**
 * @fn IBT_INT IBT_MsgMgr_ImmediateResponseMsgs(IBTMsgMgrHandler*, IBTMsgRecvInfoCtx*)
 * @brief
 * 		?????????????????????????????????????????????????????????
 *
 * 		????????????????????????????????????????????????????????????????????????????????????
 * 		???????????????msgCodeCtx?????????????????????????????????????????????????????????????????????
 * 			IBTMsgRecvInfoCtx profilesCtx;
			char result[10] = "hello";
			memset(result, '\0', sizeof(result));
			memset(&profilesCtx, 0, sizeof(IBTMsgRecvInfoCtx));

			profilesCtx.msgType = IBT_PROFILES_READ;
			profilesCtx.msgRecvBuf = result;
			profilesCtx.msgRecvSize = sizeof(result);

 * 		???????????????msgCodeCtx??????????????????????????????????????????????????????????????????????????????????????????
 * 			IBTMsgRecvInfoCtx profilesCtx;
			IBT_UL profiles_read_buf = 0UL;
			char result[10];
			memset(result, '\0', sizeof(result));
			//??? IBT_UL?????????????????????????????????UL???????????????
			profiles_read_buf = (IBT_UL)result; //!???????????????????????????????????????IBT_PROFILES_READ???????????????????????????????????????????????????????????????????????????????????????
			memset(&profilesCtx, 0, sizeof(IBTMsgRecvInfoCtx));
			profilesCtx.msgType = IBT_PROFILES_READ;
			profilesCtx.msgRecvBuf = (char*)&profiles_read_buf;
			profilesCtx.msgRecvSize = sizeof(profiles_read_buf);
 *
 * @pre
 * @post
 * @param msgCodeCtx	???????????????
 * @return
 */
int IBT_MsgMgr_ImmediateResponseMsgs(IBTMsgRecvInfoCtx *msgCodeCtx);

#endif
