#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dataplane.h"
#include "util/log.h"

#include "algo/maxmin.h"
#include "algo/judge_automorphism.h"
#include "plans/jupiter.h"
#include "networks/jupiter.h"
#include "traffic.h"
#include "algo/nauty/nausparse.h"

int jugde_equal(struct jupiter_network_t* net, unsigned* drain1, unsigned* drain2,unsigned len)
{
    /*for(int i = 0;i<len;i++)
    {
        printf("%d ",drain1[i]);
    }
    printf("drain1\n");
    for(int i = 0;i<len;i++)
    {
        printf("%d ",drain2[i]);
    }
    printf("drain2\n");*/
    DYNALLSTAT(int,lab1,lab1_sz);//define variable
    DYNALLSTAT(int,lab2,lab2_sz);
    DYNALLSTAT(int,ptn1,ptn1_sz);
    DYNALLSTAT(int,ptn2,ptn2_sz);
    DYNALLSTAT(int,orbits,orbits_sz);
    DYNALLSTAT(int,map,map_sz);
    static DEFAULTOPTIONS_SPARSEGRAPH(options);
    statsblk stats;
 /* Declare and initialize sparse graph structures */
    SG_DECL(sg); SG_DECL(sg2);//define graph
    SG_DECL(cg1); SG_DECL(cg2);

    int n,m,i;
    n = net->num_switches;

    options.getcanon = TRUE;
    options.defaultptn = FALSE;

    nauty_check(WORDSIZE,m,n,NAUTYVERSIONID);

    DYNALLOC1(int,lab1,lab1_sz,n,"malloc");//alloc varible
    DYNALLOC1(int,lab2,lab2_sz,n,"malloc");
    DYNALLOC1(int,ptn1,ptn1_sz,n,"malloc");
    DYNALLOC1(int,ptn2,ptn2_sz,n,"malloc");
    DYNALLOC1(int,orbits,orbits_sz,n,"malloc");
    DYNALLOC1(int,map,map_sz,n,"malloc");

    SG_ALLOC(sg,n,8*n,"malloc");
    sg.nv = n;              /* Number of vertices */
    sg.nde = 0;           /* Number of directed edges */

    for (int i = 0; i < net->num_switches; i++)
    {
        struct jupiter_located_switch_t* s_temp = net->get_sw(net,i);
        sg.v[s_temp->sid] = sg.nde;
        sg.d[s_temp->sid] = s_temp->neighbor_size;
        for (int j = 0; j < s_temp->neighbor_size; j++)
        {
            struct jupiter_located_switch_t* e_temp = net->get_sw(net,s_temp->neighbor_nodes[j]);
            sg.e[sg.nde++] = e_temp->sid;
        }
    }
    int last_type = 0,sum = 0;
    for (int i = 0; i < net->num_switches; i++)
    {
        int temp_flag = 0;
        for (int j = 0; j < len; j++)
        {
            if (drain1[j] == i)
            {
                temp_flag = 1;
                break;
            }
        }
        if(temp_flag)
            continue;
        lab1[sum] = i;
        struct jupiter_located_switch_t* s_temp = net->get_sw(net,i);
        if (last_type != s_temp->type)
            ptn1[sum-1] = 0;
        ptn1[sum] = 1;
        last_type = s_temp->type;
        sum++;
    }
    last_type = 0;
    for (int i = 0; i < len; i++)
    {
        lab1[sum] = drain1[i];
        struct jupiter_located_switch_t* s_temp = net->get_sw(net,drain1[i]);
        if (last_type != s_temp->type)
            ptn1[sum - 1] = 0;
        ptn1[sum] = 1;
        last_type = s_temp->type;
        sum++;
    }
    ptn1[sum - 1] = 0;

    last_type = 0, sum = 0;
    for (int i = 0; i < net->num_switches; i++)
    {
        int temp_flag = 0;
        for (int j = 0; j < len; j++)
        {
            if (drain2[j] == i)
            {
                temp_flag = 1;
                break;
            }
        }
        if(temp_flag)
            continue;
        lab2[sum] = i;
        struct jupiter_located_switch_t* s_temp = net->get_sw(net,i);
        if (last_type != s_temp->type)
            ptn2[sum - 1] = 0;
        ptn2[sum] = 1;
        last_type = s_temp->type;
        sum++;
    }
    last_type = 0;
    for (int i = 0; i < len; i++)
    {
        lab2[sum] = drain2[i];
        struct jupiter_located_switch_t* s_temp = net->get_sw(net,drain2[i]);
        if (last_type != s_temp->type)
            ptn2[sum - 1] = 0;
        ptn2[sum] = 1;
        last_type = s_temp->type;
        sum++;
    }
    ptn2[sum - 1] = 0;
    /*if(len == 5)
    {
        for(int i = 0;i < n;i++)
        printf("%d ",lab1[i]);
        printf("lab1\n");
        for(int i = 0;i < n;i++)
            printf("%d ",ptn1[i]);
        printf("ptn1\n");
        for(int i = 0;i < n;i++)
            printf("%d ",lab2[i]);
        printf("lab2\n");
        for(int i = 0;i < n;i++)
            printf("%d ",ptn2[i]);
        printf("ptn2\n");
    }*/
    
    sparsenauty(&sg,lab1,ptn1,orbits,&options,&stats,&cg1);
    sparsenauty(&sg,lab2,ptn2,orbits,&options,&stats,&cg2);
    if (aresame_sg(&cg1,&cg2))
        return 1;
    else
        return 0;
}