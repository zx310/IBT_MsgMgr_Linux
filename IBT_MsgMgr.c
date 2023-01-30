#include "IBT_MsgMgr.h"

#define LOG_TAG "IBT_MsgMgr"
#define SET_THREAD_NAME(name) (prctl(PR_SET_NAME, (unsigned long)name))
#define min(x, y) ({ 				\
		const typeof(x) _x = (x);  \
		const typeof(y) _y = (y);  \
		(void) (&_x == &_y); 		\
		_x < _y ? _x : _y; })



static IBTMsgMgrHandler *h = NULL;

// 获取自系统启动的调单递增的时间
static uint64_t GetTimeConvSeconds( struct timespec* curTime, uint32_t factor )
{
    // CLOCK_MONOTONIC：从系统启动这一刻起开始计时,不受系统时间被用户改变的影响
    clock_gettime( CLOCK_MONOTONIC, curTime );
    return (uint64_t)(curTime->tv_sec) * factor;
}

// 获取自系统启动的调单递增的时间 -- 转换单位为微秒
static uint64_t GetMonnotonicTime()
{
	struct timespec curTime;
    uint64_t result = GetTimeConvSeconds( &curTime, 1000000 );
    result += (uint32_t)(curTime.tv_nsec) / 1000;
    return result;
}

// sem_trywait + usleep的方式实现
// 如果信号量大于0，则减少信号量并立马返回true
// 如果信号量小于0，则阻塞等待，当阻塞超时时返回false
static bool IBT_SemTimeTryWait( sem_t *sem, size_t timeoutMs )
{
    const size_t timeoutUs = timeoutMs * 1000; // 延时时间由毫米转换为微秒
    const size_t maxTimeWait = 10000; // 最大的睡眠的时间为10000微秒，也就是10毫秒

    size_t timeWait = 1; // 睡眠时间，默认为1微秒
    size_t delayUs = 0; // 剩余需要延时睡眠时间

    const uint64_t startUs = GetMonnotonicTime(); // 循环前的开始时间，单位微秒
    uint64_t elapsedUs = 0; // 过期时间，单位微秒

    int ret = 0;

    do
    {
        // 如果信号量大于0，则减少信号量并立马返回true
        if( sem_trywait( sem ) == 0 )
        {
            return true;
        }

        // 系统信号则立马返回false
        if( errno != EAGAIN )
        {
        	printf("%s: errno != EAGAIN\n", __func__);
            return false;
        }

        // delayUs一定是大于等于0的，因为do-while的条件是elapsedUs <= timeoutUs.
        delayUs = timeoutUs - elapsedUs;

        // 睡眠时间取最小的值
        timeWait = min( delayUs, timeWait );

        // 进行睡眠 单位是微秒
        ret = usleep( timeWait );
        if( ret != 0 )
        {
            return false;
        }

        // 睡眠延时时间双倍自增
        timeWait *= 2;

        // 睡眠延时时间不能超过最大值
        timeWait = min( timeWait, maxTimeWait );

        // 计算开始时间到现在的运行时间 单位是微秒
        elapsedUs = GetMonnotonicTime() - startUs;
    } while( elapsedUs <= timeoutUs ); // 如果当前循环的时间超过预设延时时间则退出循环

    // 超时退出，则返回false
    printf("%s: timeout\n", __func__);
    return false;
}

void *MsgProcessThread(void *arg)
{
	int retErrnum = 0;
	IBTMsgMgrHandler *h = (IBTMsgMgrHandler *)arg;
	IBTList	*msgProcList = NULL;
	IBTMsg *msg = NULL;

	SET_THREAD_NAME("MsgProcess");

	while (1) {
		retErrnum = sem_wait(&h->msgProc.msgProcDoneSem);
		if (retErrnum < 0) {
			if (errno == EINTR) {
				printf(" Interrupted system call\n");
				continue;
			} else {
				printf("sem_wait msgProcDoneSem failed:%s\n", strerror(errno));
				goto err_sem_wait_msgProcDoneSem;
			}
		}

		IBT_List_PopQueueFromHead(&h->msgProc.msgProcDoneQueue, &h->msgProc.msgProcDoneQueue.queue, &msgProcList);
		if (msgProcList == NULL) {
			printf("msgProcList is empty\n");
			goto err_PopQueueFromHead_msgProcDoneQueue;
		}

		msg = container_of(msgProcList, IBTMsg, msgProcList);
		pthread_mutex_lock(&msg->msgMutex);
		if (msg->msgInfo.msgProcess) {
			retErrnum = msg->msgInfo.msgProcess(msg->msgInfo.msgProcessParam, msg->msgInfo.msgProcessParamSize, msg->msgInfo.priv);
			if (retErrnum < 0) {
				printf("msgName=%s:msgProcess failed, ret=0x%08x\n",
						msg->msgInfo.msgName, retErrnum);
			}
		}
		msg->msgProcStatus = IBT_MSG_STATUS_EMPTY;
		pthread_mutex_unlock(&msg->msgMutex);
		pthread_cond_signal(&msg->msgCond);
	}

	return NULL;

err_PopQueueFromHead_msgProcDoneQueue:
err_sem_wait_msgProcDoneSem:
	return (void *)-1;
}

void *MsgListenThread(void *arg)
{
	int retErrnum = 0;
	IBTMsgMgrHandler *h = (IBTMsgMgrHandler *)arg;

	IBTList	*msgRecvList = NULL;
	IBTMsgRecvInfo *msgRecvInfo = NULL;
	IBTMsg *msg = NULL;

	SET_THREAD_NAME("MsgListen");

	while (1) {
		retErrnum = sem_wait(&h->msgRecv.msgRecvDoneSem);
		if (retErrnum < 0) {
			if (errno == EINTR) {
				printf(" Interrupted system call\n");
				continue;
			} else {
				printf("sem_wait failed:%s\n", strerror(errno));
				goto err_sem_wait_msgRecvDoneSem;
			}
		}

		IBT_List_PopQueueFromHead(&h->msgRecv.msgRecvDoneQueue, &h->msgRecv.msgRecvDoneQueue.queue, &msgRecvList);
		if (msgRecvList == NULL) {
			printf("msgRecvList is empty\n");
			goto err_PopQueueFromHead_msgRecvDoneQueue;
		}

		msgRecvInfo = container_of(msgRecvList, IBTMsgRecvInfo, msgRecvList);

		msg = &h->msgPool.msg[msgRecvInfo->msgRecvInfoCtx.msgType];

		printf("MsgListenThread: wait mutex unlock!\n");
		pthread_mutex_lock(&msg->msgMutex);
		if ((msg->msgRegStatus == IBT_MSG_STATUS_UNREGISTER) || (msg->msgInfo.msgType != msgRecvInfo->msgRecvInfoCtx.msgType)
				|| (msg->msgInfo.msgProcess == NULL)) {
			printf("Unreg or Invalid msgType(%d) to msgType(%d),msgRegStatus=%d,msgProcess=%p\n",
					msg->msgRegStatus, msg->msgInfo.msgType, msgRecvInfo->msgRecvInfoCtx.msgType,
					msg->msgInfo.msgProcess);
			goto err_unregistered_msgType;
		}

		if (msg->msgProcStatus == IBT_MSG_STATUS_USAGE) {
			pthread_cond_wait(&msg->msgCond, &msg->msgMutex);
		}
		memset(msg->msgInfo.msgProcessParam, 0, msg->msgInfo.msgProcessParamSize);
		memcpy(msg->msgInfo.msgProcessParam, msgRecvInfo->msgRecvInfoCtx.msgRecvBuf, msgRecvInfo->msgRecvInfoCtx.msgRecvSize < msg->msgInfo.msgProcessParamSize ? msgRecvInfo->msgRecvInfoCtx.msgRecvSize : msg->msgInfo.msgProcessParamSize);
		msg->msgProcStatus = IBT_MSG_STATUS_USAGE;
		pthread_mutex_unlock(&msg->msgMutex);

		IBT_List_PushQueueToTail(&h->msgProc.msgProcDoneQueue, &h->msgProc.msgProcDoneQueue.queue, &msg->msgProcList);
		sem_post(&h->msgProc.msgProcDoneSem);

		IBT_List_PushQueueToTail(&h->msgRecv.msgRecvInitQueue, &h->msgRecv.msgRecvInitQueue.queue, &msgRecvInfo->msgRecvList);
		sem_post(&h->msgRecv.msgRecvInitSem);
	}

	return NULL;

err_unregistered_msgType:
	pthread_mutex_unlock(&msg->msgMutex);
	IBT_List_PushQueueToTail(&h->msgRecv.msgRecvInitQueue, &h->msgRecv.msgRecvInitQueue.queue, &msgRecvInfo->msgRecvList);
	sem_post(&h->msgRecv.msgRecvInitSem);
err_PopQueueFromHead_msgRecvDoneQueue:
err_sem_wait_msgRecvDoneSem:
	return (void *)-1;
}

/**
 * @fn IBT_VOID IBT_MsgMgr_Init()
 * @brief
 * 		msgmgr初始化
 * @pre
 * @post
 * @return
 */
void IBT_MsgMgr_Init()
{
	int retErrnum = 0, i = 0;

	h = calloc(1, sizeof(IBTMsgMgrHandler));
	if (h == NULL)
	{
		printf("calloc IBTMsgMgrHandler|IBTHandler failed");
		goto err_calloc_IBTMsgMgrHandler;
	}

	h->msgRecv.msgRecvInfoCnt = MAX_RECVD_MSG_CNT;
	h->msgRecv.msgRecvInfo = calloc(h->msgRecv.msgRecvInfoCnt, sizeof(IBTMsgRecvInfo));
	if (h->msgRecv.msgRecvInfo == NULL) {
		printf("calloc msgRecvInfo failed");
		goto err_calloc_msgRecvInfo;
	}
	h->msgRecv.msgRecvBufSize = MAX_RECVD_MSG_BUF_SIZE;
	for (i = 0; i < h->msgRecv.msgRecvInfoCnt; i++) {
		IBT_List_Init(&h->msgRecv.msgRecvInfo[i].msgRecvList);
		h->msgRecv.msgRecvInfo[i].msgRecvInfoCtx.msgType = IBT_INV_MSG_TYPE;
		h->msgRecv.msgRecvInfo[i].msgRecvInfoCtx.msgRecvBuf = calloc(1, h->msgRecv.msgRecvBufSize);
		if (h->msgRecv.msgRecvInfo[i].msgRecvInfoCtx.msgRecvBuf == NULL) {
			printf("calloc msgRecvBuf failed");
			goto err_calloc_msgRecvBuf;
		}
		h->msgRecv.msgRecvInfo[i].msgRecvInfoCtx.msgRecvSize = 0;
	}

	//init msgRecvInitQueue and msgRecvInitSem MAX_RECVD_MSG_CNT number
	IBT_List_QueueInit(&h->msgRecv.msgRecvInitQueue);
	for (i = 0; i < h->msgRecv.msgRecvInfoCnt; i++) {
		IBT_List_PushQueueToTail(&h->msgRecv.msgRecvInitQueue, &h->msgRecv.msgRecvInitQueue.queue, &h->msgRecv.msgRecvInfo[i].msgRecvList);
	}
	retErrnum = sem_init(&h->msgRecv.msgRecvInitSem, 0, h->msgRecv.msgRecvInfoCnt);
	if (retErrnum < 0) {
		printf("sem_init msgRecvInitSem failed:%s", strerror(errno));
		goto err_sem_init_msgRecvInitSem;
	}

	IBT_List_QueueInit(&h->msgRecv.msgRecvDoneQueue);
	retErrnum = sem_init(&h->msgRecv.msgRecvDoneSem, 0, 0);
	if (retErrnum < 0) {
		printf("sem_init msgRecvDoneSem failed:%s", strerror(errno));
		goto err_sem_init_msgRecvDoneSem;
	}

	h->msgPool.msgCnt = IBT_MAX_MSG_TYPE;
	h->msgPool.msg = calloc(h->msgPool.msgCnt, sizeof(IBTMsg));
	if (h->msgPool.msg == NULL) {
		printf("calloc msg failed:%s\n", strerror(errno));
		goto err_calloc_msg;
	}

	for (i = 0; i < h->msgPool.msgCnt; i++) {
		retErrnum = pthread_mutex_init(&h->msgPool.msg[i].msgMutex, NULL);
		if (retErrnum != 0) {
			printf("pthread_mutex_init msg[%d].msgMutex failed", i);
			goto err_pthread_mutex_init_msgi_msgMutex;
		}

		retErrnum = pthread_cond_init(&h->msgPool.msg[i].msgCond, NULL);
		if (retErrnum != 0) {
			printf("pthread_cond_init msg[%d].msgCond failed\n", i);
			goto err_pthread_mutex_init_msgi_msgCond;
		}

		IBT_List_Init(&h->msgPool.msg[i].msgRegList);

		h->msgPool.msg[i].msgRegStatus = IBT_MSG_STATUS_UNREGISTER;
		IBT_List_Init(&h->msgPool.msg[i].msgProcList);
		h->msgPool.msg[i].msgProcStatus = IBT_MSG_STATUS_EMPTY;
	}

	retErrnum = pthread_mutex_init(&h->msgReg.msgRegMutex, NULL);
	if (retErrnum != 0) {
		printf("pthread_mutex_init msgRegMutex failed");
		goto err_pthread_mutex_init_msgRegMutex;
	}
	IBT_List_QueueInit(&h->msgReg.msgRegQueue);

	IBT_List_QueueInit(&h->msgProc.msgProcDoneQueue);

	retErrnum = sem_init(&h->msgProc.msgProcDoneSem, 0, 0);
	if (retErrnum < 0) {
		printf("sem_init msgProcDoneSem failed");
		goto err_sem_init_msgProcDoneSem;
	}

	retErrnum = pthread_create(&h->msgProc.msgProcessTid, NULL, MsgProcessThread, (void *)h);
	if (retErrnum < 0) {
		printf("pthread_create MsgProcessThread failed:%s", strerror(retErrnum));
		goto err_pthread_create_MsgProcessThread;
	}

	retErrnum = pthread_create(&h->msgProc.msgListenTid, NULL, MsgListenThread, (void *)h);
	if (retErrnum < 0) {
		printf("pthread_create MsgListenThread failed:%s\n", strerror(retErrnum));
		goto err_pthread_create_MsgListenThread;
	}
	printf("IBT_MsgMgr_Init success\n");
	return;
err_pthread_create_MsgListenThread:
	pthread_cancel(h->msgProc.msgListenTid);
	pthread_join(h->msgProc.msgProcessTid, NULL);
err_pthread_create_MsgProcessThread:
	sem_destroy(&h->msgProc.msgProcDoneSem);
err_sem_init_msgProcDoneSem:
	IBT_List_QueueDeInit(&h->msgProc.msgProcDoneQueue);
	IBT_List_QueueDeInit(&h->msgReg.msgRegQueue);
	pthread_mutex_destroy(&h->msgReg.msgRegMutex);
err_pthread_mutex_init_msgRegMutex:
	i = h->msgPool.msgCnt;
err_pthread_mutex_init_msgi_msgCond:
	if (i != h->msgPool.msgCnt) {
		pthread_cond_destroy(&h->msgPool.msg[i].msgCond);
	}
err_pthread_mutex_init_msgi_msgMutex:
	for (--i; i >= 0; i--) {
		pthread_cond_destroy(&h->msgPool.msg[i].msgCond);
		pthread_mutex_destroy(&h->msgPool.msg[i].msgMutex);
	}
	free(h->msgPool.msg);
err_calloc_msg:
	sem_destroy(&h->msgRecv.msgRecvDoneSem);
err_sem_init_msgRecvDoneSem:
	IBT_List_QueueDeInit(&h->msgRecv.msgRecvDoneQueue);
	sem_destroy(&h->msgRecv.msgRecvInitSem);
err_sem_init_msgRecvInitSem:
	IBT_List_QueueDeInit(&h->msgRecv.msgRecvInitQueue);
	i = h->msgRecv.msgRecvInfoCnt;
err_calloc_msgRecvBuf:
	for (--i; i >= 0; i--) {
		free(h->msgRecv.msgRecvInfo[i].msgRecvInfoCtx.msgRecvBuf);
	}
	free(h->msgRecv.msgRecvInfo);
err_calloc_msgRecvInfo:
	free(h);
err_calloc_IBTMsgMgrHandler:
	return;
}
/**
 * @fn IBT_VOID IBT_MsgMgr_DeInit()
 * @brief
 * 		msgmgr反初始化
 * @pre
 * @post
 * @return
 */
void IBT_MsgMgr_DeInit()
{
	if (h) {
		int i = 0;
		pthread_cancel(h->msgProc.msgListenTid);
		pthread_join(h->msgProc.msgListenTid, NULL);
		pthread_cancel(h->msgProc.msgProcessTid);
		pthread_join(h->msgProc.msgProcessTid, NULL);
		sem_destroy(&h->msgProc.msgProcDoneSem);
		IBT_List_QueueDeInit(&h->msgProc.msgProcDoneQueue);
		IBT_List_QueueDeInit(&h->msgReg.msgRegQueue);
		for (i = h->msgPool.msgCnt - 1; i >= 0; i--) {
			pthread_cond_destroy(&h->msgPool.msg[i].msgCond);
			pthread_mutex_destroy(&h->msgPool.msg[i].msgMutex);
		}
		free(h->msgPool.msg);
		sem_destroy(&h->msgRecv.msgRecvDoneSem);
		IBT_List_QueueDeInit(&h->msgRecv.msgRecvDoneQueue);
		sem_destroy(&h->msgRecv.msgRecvInitSem);
		IBT_List_QueueDeInit(&h->msgRecv.msgRecvInitQueue);
		i = h->msgRecv.msgRecvInfoCnt;
		for (i = h->msgRecv.msgRecvInfoCnt - 1; i >= 0; i--) {
			free(h->msgRecv.msgRecvInfo[i].msgRecvInfoCtx.msgRecvBuf);
		}
		free(h->msgRecv.msgRecvInfo);
		free(h);
		h = NULL;
	}
}
/**
 * @fn IBT_INT IBT_MsgMgr_RegisterMsgs(IBTMsgInfo*)
 * @brief
 * 		消息注册
 * @pre
 * @post
 * @param msgInfo	注册的消息结构体
 * @return
 */
int IBT_MsgMgr_RegisterMsgs(IBTMsgInfo *msgInfo)
{
	if (!h || !msgInfo || (msgInfo->msgType < IBT_FIRST_MSG_TYPE) || (msgInfo->msgType > IBT_MAX_MSG_TYPE)) {
		printf("invalid h=%p, msgInfo=%p, msgType=%d\n", h, msgInfo, msgInfo ? msgInfo->msgType : 0);
		return -1;
	}
	pthread_mutex_lock(&h->msgPool.msg[msgInfo->msgType].msgMutex);
	if (h->msgPool.msg[msgInfo->msgType].msgRegStatus > IBT_MSG_STATUS_UNREGISTER) {
		printf("msgType=%d has been registered\n", msgInfo->msgType);
		pthread_mutex_unlock(&h->msgPool.msg[msgInfo->msgType].msgMutex);
		return -1;
	}
	memcpy(&h->msgPool.msg[msgInfo->msgType].msgInfo, msgInfo, sizeof(IBTMsgInfo));
	h->msgPool.msg[msgInfo->msgType].msgRegStatus = IBT_MSG_STATUS_REGISTER;
	pthread_mutex_unlock(&h->msgPool.msg[msgInfo->msgType].msgMutex);

	IBT_List_PushQueueToTail(&h->msgReg.msgRegQueue, &h->msgReg.msgRegQueue.queue, &h->msgPool.msg[msgInfo->msgType].msgRegList);

	return 0;
}
/**
 * @fn IBT_INT IBT_MsgMgr_UnRegisterMsgs(IBTMsgType, IBT_BOOL)
 * @brief
 * 		取消注册消息
 * @pre
 * @post
 * @param msgType		消息类型
 * @param isFreeParam	是否释放msgProcessParam
 * @return
 */
int IBT_MsgMgr_UnRegisterMsgs(IBTMsgType msgType, bool isFreeParam)
{
	if (!h || (msgType < IBT_FIRST_MSG_TYPE) || (msgType > IBT_MAX_MSG_TYPE)) {
		printf("Invalid h=%p, msgType=%d\n", h, msgType);
		return -1;
	}
	if (h->msgPool.msg[msgType].msgRegStatus == IBT_MSG_STATUS_UNREGISTER) {
		printf("msgType=%d hasn't been registered\n", msgType);
		return -1;
	}
	IBT_List_PopQueueList(&h->msgReg.msgRegQueue, &h->msgPool.msg[msgType].msgRegList);
	pthread_mutex_lock(&h->msgPool.msg[msgType].msgMutex);
	if (isFreeParam && h->msgPool.msg[msgType].msgInfo.msgProcessParam) {
		free(h->msgPool.msg[msgType].msgInfo.msgProcessParam);
	}
	memset(&h->msgPool.msg[msgType].msgInfo, 0, sizeof(IBTMsgInfo));
	h->msgPool.msg[msgType].msgRegStatus = IBT_MSG_STATUS_UNREGISTER;
	pthread_mutex_unlock(&h->msgPool.msg[msgType].msgMutex);

	return 0;
}
/**
 * @fn IBT_INT IBT_MsgMgr_SendMsgs(IBTMsgRecvInfoCtx*, IBT_INT)
 * @brief
 * 		带超时功能的，发送命令和数据的函数，timeOutMs可以小于0
 *
 * 		假设传入的值小于0：
 * 		如果是需要使用此函数修改一个值然后立即使用IBT_MsgMgr_ImmediateResponseMsgs读取回来，那大概率是失败的。
 * 		这种情况修改值和获取值都使用IBT_MsgMgr_ImmediateResponseMsgs才能保证执行顺序是先修改后读。
 * @pre
 * @post
 * @param msgCodeCtx 消息结构体
 * @param timeOutMs  超时时间，单位：毫秒
 * @return 0：成功 -1：失败
 */
int IBT_MsgMgr_SendMsgs(IBTMsgRecvInfoCtx *msgCodeCtx, unsigned int timeOutMs)
{
	bool retErrnum = false;
	IBTMsgRecvInfo *msgRecvInfo = NULL;
	IBTList *msgRecvList = NULL;
	IBTMsg *msg = NULL;

	if ((msgCodeCtx->msgType < IBT_FIRST_MSG_TYPE) || (msgCodeCtx->msgType > IBT_MAX_MSG_TYPE)) {
		printf("Invalid msgType=%d\n", msgCodeCtx->msgType);
		goto err_invalid_msgType;
	}

	if (timeOutMs > 0)
	{
		retErrnum = IBT_SemTimeTryWait(&h->msgRecv.msgRecvInitSem, timeOutMs);
		if (!retErrnum)
		{
			printf("IBT_SemTimeTryWait failed:%s\n", strerror(errno));
			goto err_sem_timedwait;
		}
	} else if (timeOutMs == 0)
	{
		if (sem_wait(&h->msgRecv.msgRecvInitSem) < 0)
		{
			printf("sem_wait failed:%s", strerror(errno));
			goto err_sem_wait;
		}
	}

	printf("IBT_MsgMgr_SendMsgs get sem!\n");
	IBT_List_PopQueueFromHead(&h->msgRecv.msgRecvInitQueue, &h->msgRecv.msgRecvInitQueue.queue, &msgRecvList);
	if (msgRecvList == NULL) {
		printf("PopQueueFromHead's msgRecvList is empty\n");
		goto err_PopQueueFromHead;
	}

	msgRecvInfo = container_of(msgRecvList, IBTMsgRecvInfo, msgRecvList);

	msgRecvInfo->msgRecvInfoCtx.msgType = msgCodeCtx->msgType;
	msg = &h->msgPool.msg[msgRecvInfo->msgRecvInfoCtx.msgType];

	if (h->msgRecv.msgRecvBufSize < msgCodeCtx->msgRecvSize) {
		printf("msgRecvBufSize(%d) < msgRecvSize(%d)\n", h->msgRecv.msgRecvBufSize, msgCodeCtx->msgRecvSize);
		goto err_invalid_msgCodeCtx_Size;
	}

	if (msg->msgInfo.msgProcessParamSize < msgCodeCtx->msgRecvSize) {
		printf("WARN: msgProcessParamSize(%d) < msgRecvSize(%d)\n", msg->msgInfo.msgProcessParamSize, msgCodeCtx->msgRecvSize);
	}

	memcpy(msgRecvInfo->msgRecvInfoCtx.msgRecvBuf, msgCodeCtx->msgRecvBuf, msgCodeCtx->msgRecvSize);
	msgRecvInfo->msgRecvInfoCtx.msgRecvSize = msgCodeCtx->msgRecvSize;
	IBT_List_PushQueueToTail(&h->msgRecv.msgRecvDoneQueue, &h->msgRecv.msgRecvDoneQueue.queue, &msgRecvInfo->msgRecvList);
	sem_post(&h->msgRecv.msgRecvDoneSem);
	printf("IBT_MsgMgr_SendMsgs end!\n");
	return 0;

err_invalid_msgCodeCtx_Size:
	IBT_List_PushQueueToTail(&h->msgRecv.msgRecvInitQueue, &h->msgRecv.msgRecvInitQueue.queue, &msgRecvInfo->msgRecvList);
	sem_post(&h->msgRecv.msgRecvInitSem);
err_PopQueueFromHead:
err_sem_wait:
err_sem_timedwait:
err_invalid_msgType:
	return -1;
}
/**
 * @fn IBT_INT IBT_MsgMgr_ImmediateResponseMsgs(IBTMsgRecvInfoCtx*)
 * @brief
 * 		发送命令并且要求相应的注册函数立即执行
 *
 * 		可以用来发送命令和数据或者获取数据，例如获取视频的分辨率
 * 		发送数据时msgCodeCtx承载的是需要发送的数据，例如：发送一段字符串：
 * 			IBTMsgRecvInfoCtx profilesCtx;
			char result[10] = "hello";
			memset(result, '\0', sizeof(result));
			memset(&profilesCtx, 0, sizeof(IBTMsgRecvInfoCtx));

			profilesCtx.msgType = IBT_PROFILES_READ;
			profilesCtx.msgRecvBuf = result;
			profilesCtx.msgRecvSize = sizeof(result);

 * 		获取数据时msgCodeCtx承载的是需要获取的数据的指针位置，例如，获取的数据是字符串：
 * 			IBTMsgRecvInfoCtx profilesCtx;
			IBT_UL profiles_read_buf = 0UL;
			char result[10];
			memset(result, '\0', sizeof(result));
			//！ IBT_UL千万记得，某些机器可能UL都不够使用
			profiles_read_buf = (IBT_UL)result; //!传的是指针，相当于直接告诉IBT_PROFILES_READ的回调函数这个指针地址，回调函数直接操作内存即可修改这个值
			memset(&profilesCtx, 0, sizeof(IBTMsgRecvInfoCtx));
			profilesCtx.msgType = IBT_PROFILES_READ;
			profilesCtx.msgRecvBuf = (char*)&profiles_read_buf;
			profilesCtx.msgRecvSize = sizeof(profiles_read_buf);
 *
 * @pre
 * @post
 * @param msgCodeCtx	消息结构体
 * @return
 */
int IBT_MsgMgr_ImmediateResponseMsgs(IBTMsgRecvInfoCtx *msgCodeCtx)
{
	int retErrnum = 0;
	IBTMsg *msg = NULL;

	if ((msgCodeCtx->msgType < IBT_FIRST_MSG_TYPE) || (msgCodeCtx->msgType > IBT_MAX_MSG_TYPE)) {
		printf("Invalid msgType=%d\n", msgCodeCtx->msgType);
		return -1;
	}

	if(h == NULL) {
		printf("Invalid IBTMsgMgrHandler\n");
		return -1;
	}

	msg = &h->msgPool.msg[msgCodeCtx->msgType];
	printf("IBT_MsgMgr_ImmediateResponseMsgs: wait mutex unlock!\n");
	pthread_mutex_lock(&msg->msgMutex);
	if ((msg->msgRegStatus == IBT_MSG_STATUS_UNREGISTER) || (msg->msgInfo.msgType != msgCodeCtx->msgType) || (msg->msgInfo.msgProcess == NULL)) {
		printf("Unreg or Invalid msgType(%d) to msgType(%d),msgRegStatus=%d,msgProcess=%p\n",
				msg->msgInfo.msgType, msgCodeCtx->msgType, msg->msgRegStatus,
				msg->msgInfo.msgProcess);
		pthread_mutex_unlock(&msg->msgMutex);
		return -1;
	}

	msg->msgProcStatus = IBT_MSG_STATUS_USAGE;

	retErrnum = msg->msgInfo.msgProcess(msgCodeCtx->msgRecvBuf, msgCodeCtx->msgRecvSize, msg->msgInfo.priv);
	if (retErrnum < 0) {
		printf("msgName=%s:msgProcess failed, ret=0x%08x\n", msg->msgInfo.msgName, retErrnum);
	}
	msg->msgProcStatus = IBT_MSG_STATUS_EMPTY;
	pthread_mutex_unlock(&msg->msgMutex);

	return retErrnum;
}
