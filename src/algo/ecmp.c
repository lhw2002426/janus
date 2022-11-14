#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dataplane.h"
#include "util/log.h"

#include "algo/maxmin.h"
#include "algo/ecmp.h"
#include "plans/jupiter.h"
#include "networks/jupiter.h"
#include "traffic.h"
//#include <queue>
//分配流量，并测试是否超过限制
typedef uint32_t QDataType;
typedef struct QueueNode
{
	struct QueueNode* next;
	QDataType data;
}QNode;
typedef struct Queue//
{
	QNode* tail;//头指针
	QNode* head;//尾指针
}Queue;

void QueueInit(Queue* pq)
{
	assert(pq);
	pq->head = pq->tail = NULL;
}

void QueuePush(Queue* pq, QDataType x)
{
	assert(pq);
	QNode* newnode = (QNode*)malloc(sizeof(QNode));
	if (newnode == NULL)
	{
		printf("malloc fail\n");
		exit(-1);
	}
	newnode->data = x;
	newnode->next = NULL;
	if (pq->tail == NULL)
	{
		pq->head = pq->tail = newnode;
	}
	else
	{
		pq->tail->next = newnode;
		pq->tail = newnode;
	}
}

void QueuePop(Queue* pq)
{
	assert(pq);
	assert(pq->head);

	// 1、一个
	// 2、多个
	if (pq->head->next == NULL)
	{
		free(pq->head);
		pq->head = pq->tail = NULL;
	}
	else
	{
		QNode* next = pq->head->next;
		free(pq->head);
		pq->head = next;
	}
}

QDataType QueueFront(Queue* pq)
{
	assert(pq);
	assert(pq->head);

	return pq->head->data;
}

QDataType QueueBack(Queue* pq)
{
	assert(pq);
	assert(pq->head);

	return pq->tail->data;
}


uint32_t QueueEmpty(Queue* pq)
{
	assert(pq);
	return pq->head == NULL;
}

void QueueDestory(Queue* pq)
{
	assert(pq);

	QNode* cur = pq->head;
	while (cur)
	{
		QNode* next = cur->next;
		free(cur);
		cur = next;
	}

	pq->head = pq->tail = NULL;
}

void debug_out(struct jupiter_network_t* net)
{
    for (int i = 0; i < net->num_switches; i++)
    {
        printf("id:%d, pod: %d, type: %d, state: %d, traffic: %lf\n", net->klotski_switches[i].sid, net->klotski_switches[i].pod, net->klotski_switches[i].type, net->klotski_switches[i].stat, net->klotski_switches[i].traffic);
        struct jupiter_located_switch_t* s_temp = net->get_sw(net, i);
        for (int j = 0; j < s_temp->neighbor_size; j++)
        {
            struct jupiter_located_switch_t* e_temp = net->get_sw(net,s_temp->neighbor_nodes[j]);
            printf("    id: %d, type: %d, traffic: %lf\n", e_temp->sid, e_temp->type, net->edge_traffic[i][s_temp->neighbor_nodes[j]]);
        }
    }
}

int ecmp(struct jupiter_network_t* net,double * traffic)
{   
    //printf("ecmp begin\n");
    //printf("test\n");
    //while(1);
    net->clear(net);
    double max_ratio = 0,tmp_traffic = 100000000.0;
    *traffic = -1;
    struct Queue* q;
    q = malloc(sizeof(struct Queue));
    QueueInit(q);
    for (int I = 0; I < net->pair_num; I++)
    {
        net->clear_dis(net);
        struct pair_t* pair = net->pairs[I];
        QueuePush(q,pair->dst);
        int debug = 0;
        //printf("\n\n\nnow pair src: %d, dst: %d demand: %lf\n", pair->src, pair->dst, pair->demand);
        while (!QueueEmpty(q))
        {
            uint32_t temp = QueueFront(q);
            QueuePop(q);
            struct jupiter_located_switch_t* s_temp = net->get_sw(net,temp);
            if (s_temp->stat == DOWN)
                continue;
            for (int i = 0;i<s_temp->neighbor_size;i++)
            {
                struct jupiter_located_switch_t* e_temp = net->get_sw(net,s_temp->neighbor_nodes[i]);
                if (e_temp->dis != 0||e_temp->stat == DOWN || e_temp->sid == pair->dst)
                    continue;
                e_temp->dis = s_temp->dis + 1;
                QueuePush(q,s_temp->neighbor_nodes[i]);
                debug++;
            }
        }
        while (!QueueEmpty(q))
            QueuePop(q);
        QueuePush(q,pair->src);
        struct jupiter_located_switch_t* temp_sw = net->get_sw(net,pair->src);
        if(temp_sw->dis == 0)
        {
            //debug_out(net);
            //printf("test in ecmp -2\n");
            return -1;
        }
            
        temp_sw->traffic = pair->demand;
        uint32_t next_links[2000], next_sum = 0;
        uint8_t visited[1200];
        memset(visited, 0, sizeof(visited));
        double next_traffic = 0;
        while (!QueueEmpty(q))
        {
            memset(next_links, 0, sizeof(next_links));
            next_sum = 0;
            uint32_t temp = QueueFront(q);
            QueuePop(q);
            struct jupiter_located_switch_t* s_temp = net->get_sw(net,temp);
            if (s_temp->stat == DOWN)
                continue;
            for (int i = 0;i<s_temp->neighbor_size;i++)
            {
                struct jupiter_located_switch_t* e_temp = net->get_sw(net,s_temp->neighbor_nodes[i]);//注意我们要更新switch的
                if (e_temp->stat == DOWN)
                    continue;
                if (e_temp->dis == s_temp->dis - 1)
                {
                    next_links[next_sum++] = s_temp->neighbor_nodes[i];
                    if(!visited[s_temp->neighbor_nodes[i]])
                        QueuePush(q,s_temp->neighbor_nodes[i]);
                    visited[s_temp->neighbor_nodes[i]] = 1;
                }
            }
            next_traffic = s_temp->traffic / next_sum;
            for (int i = 0; i < next_sum; i++)
            {
                struct jupiter_located_switch_t* l_temp = net->get_sw(net,next_links[i]);
                l_temp->traffic += next_traffic;
                net->edge_traffic[next_links[i]][temp] += next_traffic;
                net->edge_traffic[temp][next_links[i]] += next_traffic;
                if(max_ratio<(double)net->edge_traffic[next_links[i]][temp] / (double)net->cap[next_links[i]][temp])
                    max_ratio = (double)net->edge_traffic[next_links[i]][temp] / (double)net->cap[next_links[i]][temp];
                if(tmp_traffic>(double)net->cap[next_links[i]][temp] - (double)net->edge_traffic[next_links[i]][temp])
                    tmp_traffic = (double)net->cap[next_links[i]][temp] - (double)net->edge_traffic[next_links[i]][temp];
                if (max_ratio > net->theta)
                {
                    //debug_out(net);
                    //printf("maxratio: %lf src: %d dst: %d traffic: %lf capacity: %lf theta:%f\n", max_ratio, temp, next_links[i], (double)net->edge_traffic[next_links[i]][temp], (double)net->cap[next_links[i]][temp],net->theta);
                    //printf("test in ecmp -1\n");
                    //while(1);
                    return -1;
                }
                    
            }
        }
        //debug_out(net);
    }
    //printf("test in ecmp 1: %f\n",tmp_traffic);
    //while(1);
    if(traffic!=0)
        *traffic = tmp_traffic;
    return 1;
}