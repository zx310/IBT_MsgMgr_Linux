#include "IBT_List.h"

inline void IBT_List_Init(IBTList *list)
{
	list->next = list->prev = list;
}

inline bool IBT_List_IsSelf(IBTList *list)
{
	return (list->next == list) && (list->prev == list);
}

void IBT_List_QueueInit(IBTListQueue *list_queue)
{
	pthread_mutex_init(&list_queue->mutex, NULL);

	pthread_mutex_lock(&list_queue->mutex);
	list_queue->queue.prev = (list_queue->queue.next = &list_queue->queue);
	pthread_mutex_unlock(&list_queue->mutex);
}

void IBT_List_QueueDeInit(IBTListQueue *list_queue)
{
	pthread_mutex_lock(&list_queue->mutex);
	list_queue->queue.prev = (list_queue->queue.next = NULL);
	pthread_mutex_unlock(&list_queue->mutex);

	pthread_mutex_destroy(&list_queue->mutex);
}

void IBT_List_PushQueueToHead(IBTListQueue *list_queue, IBTList *head, IBTList *new)
{
	pthread_mutex_lock(&list_queue->mutex);
	new->next = head->next;
	new->prev = head;
	head->next->prev = new;
	head->next = new;
	pthread_mutex_unlock(&list_queue->mutex);
}

void IBT_List_PushQueueToTail(IBTListQueue *list_queue, IBTList *head, IBTList *new)
{
	pthread_mutex_lock(&list_queue->mutex);
	new->next = head;
	new->prev = head->prev;
	head->prev->next = new;
	head->prev = new;
	pthread_mutex_unlock(&list_queue->mutex);
}

void IBT_List_PushQueueList(IBTListQueue *list_queue, IBTList *prev, IBTList *next, IBTList *new)
{
	pthread_mutex_lock(&list_queue->mutex);
	new->next = next;
	new->prev = prev;
	prev->next = new;
	next->prev = new;
	pthread_mutex_unlock(&list_queue->mutex);
}

void IBT_List_PopQueueList(IBTListQueue *list_queue, IBTList *list)
{
	pthread_mutex_lock(&list_queue->mutex);
	list->prev->next = list->next;
	list->next->prev = list->prev;
	list->next = (list->prev = list);
	pthread_mutex_unlock(&list_queue->mutex);
}

void IBT_List_PopQueueFromHead(IBTListQueue *list_queue, IBTList *head, IBTList **new)
{
	pthread_mutex_lock(&list_queue->mutex);
	if (head->next == head) {
		*new = NULL;
	} else {
		*new = head->next;
		head->next = head->next->next;
		head->next->prev = head;
		(*new)->next = (*new)->prev = *new;
	}
	pthread_mutex_unlock(&list_queue->mutex);
}

int IBT_List_IsEmptyQueue(IBTListQueue *list_queue, IBTList *head)
{
	pthread_mutex_lock(&list_queue->mutex);
	return (head->next == head) && (head->prev == head);
	pthread_mutex_unlock(&list_queue->mutex);
}
