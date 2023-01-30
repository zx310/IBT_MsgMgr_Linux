#ifndef __IBT_MSGLIST_H__
#define __IBT_MSGLIST_H__

#include <pthread.h>
#include <stddef.h>
#include <stdbool.h>

#ifndef container_of
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})
#endif

typedef struct IBTList IBTList;

struct IBTList {
	IBTList			*prev;
	IBTList			*next;
};

typedef struct IBTListQueue {
	pthread_mutex_t		mutex;
	IBTList				queue;
} IBTListQueue;

void IBT_List_Init(IBTList *list);
bool IBT_List_IsSelf(IBTList *list);
void IBT_List_QueueInit(IBTListQueue *list_queue);
void IBT_List_QueueDeInit(IBTListQueue *list_queue);
void IBT_List_PushQueueToHead(IBTListQueue *list_queue, IBTList *head, IBTList *new);
void IBT_List_PushQueueToTail(IBTListQueue *list_queue, IBTList *head, IBTList *new);
void IBT_List_PushQueueList(IBTListQueue *list_queue, IBTList *prev, IBTList *next, IBTList *new);
void IBT_List_PopQueueList(IBTListQueue *list_queue, IBTList *list);
void IBT_List_PopQueueFromHead(IBTListQueue *list_queue, IBTList *head, IBTList **new);
int  IBT_List_IsEmptyQueue(IBTListQueue *list_queue, IBTList *head);

#define IBT_List_For_Each_Entry(pos, head, member)				\
	for (pos = container_of((head)->next, typeof(*pos), member);	\
	     (head)->next != (head) && pos->member.next != (head); 	\
	     pos = container_of(pos->member.next, typeof(*pos), member))

#endif
