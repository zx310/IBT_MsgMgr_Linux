/*
 ============================================================================
 Name        : IBTMsgMgr.c
 Author      : 
 Version     :
 Copyright   : Your copyright notice
 Description : Hello World in C, Ansi-style
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>

#include "IBT_MsgMgr.h"
int test_data = 100;
static int data;
IBTMsgInfo info;
IBTMsgRecvInfoCtx recvInfoCtx = IBT_MSG_RECV_INFO_DEFAULT;

int test_func(void*  param, int paramSize, void* priv)
{
	int* msg = (int*)param;
	printf("Hello %d\n", *msg);
	sleep(10);
	return 0;
}

void *TestThread(void *arg)
{
	pthread_detach(pthread_self());
	test_data = 10;
	IBT_MsgMgr_ImmediateResponseMsgs(&recvInfoCtx);
	pthread_exit(NULL);
}
int main(void) {

	info.msgType = IBT_TEST_MSG_TYPE;
	info.msgName = "test";
	info.msgProcess = test_func;
	info.msgProcessParam = (void*)&data;
	info.msgProcessParamSize = sizeof(data);

	recvInfoCtx.msgType = info.msgType;
	recvInfoCtx.msgRecvBuf = (char*)&test_data;
	recvInfoCtx.msgRecvSize = sizeof(test_data);

	IBT_MsgMgr_Init();

	IBT_MsgMgr_RegisterMsgs(&info);

	IBT_MsgMgr_SendMsgs(&recvInfoCtx, 1000);
	pthread_t test_id;
	pthread_create(&test_id, NULL, TestThread, NULL);

#if 0
	for(int i = 0; i < 33; i++)
	{
		test_data = i;
		if(i == 32)
		{
			IBT_MsgMgr_SendMsgs(&recvInfoCtx, 0);

		}else
		{
			IBT_MsgMgr_SendMsgs(&recvInfoCtx, 1000);
		}
	}
#endif
	printf("Done!!!\n");
	pause();

	return EXIT_SUCCESS;
}
