#include "cachelab.h"
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include<stdio.h>

typedef struct LineItemStruct {
    int id;
    char valid;
    int blockSize;
    int insertClock;
    struct LineItemStruct* next;
} LineItem;

typedef struct GroupStruct {
    int lineCnt;
    int used;
    int clock;
    LineItem* lineItemHead;
    struct GroupStruct* next;
}GroupItem;

typedef struct CacheStruct {
    int groupCnt;
    GroupItem* groupItemHead;
} Cache;

typedef struct TraceStruct {
    char type;
    long long  addr;
    int size;
}Trace;

int VERBOSE = 0;
int hitCntGlobal = 0, missCntGlobal = 0, evictCntGlobal = 0;
int str2int(char* optarg) {
    int res = 0, idx = 0;
    while (optarg[idx] != '\0') {
        res = res * 10 + optarg[idx++] - '0';
    }
    return res;
}
void parseTrace(Trace* trace, char* str) {
    int idx = 0;
    if (str[idx] == ' ')idx++;
    trace->type = str[idx];
    idx += 2;
    trace->addr = 0;
    int tmp = 0;
    while (str[idx] != ',') {
        // hexadecimal
        if (str[idx] >= '0' && str[idx] <= '9')tmp = str[idx++] - '0';
        else tmp = str[idx++] - 'a' + 10;
        trace->addr = trace->addr * 16 + tmp;
    }
    idx++;
    trace->size = 0;
    while (str[idx] != '\n') {
        trace->size = trace->size * 10 + str[idx++] - '0';
    }
}

void initCache(Cache* cache, int s, int E, int b) {
    cache->groupCnt = (1 << s);
    cache->groupItemHead = NULL;
    for (int i = cache->groupCnt; i >= 1; i--) {
        // for every group, init lines
        GroupItem* groupItem = (GroupItem*)malloc(sizeof(GroupItem));
        groupItem->next = cache->groupItemHead;
        cache->groupItemHead = groupItem;
        groupItem->lineCnt = E;
        groupItem->used = 0;
        groupItem->clock = 0;
        groupItem->lineItemHead = NULL;
        for (int j = E; j >= 1; j--) {
            LineItem* lineItem = (LineItem*)malloc(sizeof(LineItem));
            lineItem->blockSize = (1 << b);
            lineItem->valid = 0;
            lineItem->insertClock = 0;
            lineItem->next = groupItem->lineItemHead;
            groupItem->lineItemHead = lineItem;
        }
    }
}
GroupItem* getGroupByIndex(Cache* cache, int index) {
    GroupItem* res = cache->groupItemHead;
    while (index--) {
        res = res->next;
    }
    return res;
}
LineItem* getLineByIndex(GroupItem* group, int index) {
    LineItem* res = group->lineItemHead;
    while (index--) {
        res = res->next;
    }
    return res;
}

LineItem* checkInCache(GroupItem* currentGoup, int id) {
    LineItem* lineItem = currentGoup->lineItemHead;
    while (lineItem != NULL) {
        if (lineItem->valid && lineItem->id == id)break;
        lineItem = lineItem->next;
    }
    return lineItem;
}
void setCache(GroupItem* currentGoup, int id) {
    LineItem* lineItem = currentGoup->lineItemHead;
    while (lineItem->valid)lineItem = lineItem->next;
    lineItem->valid = 1;
    lineItem->id = id;
    currentGoup->used++;
    currentGoup->clock++;
    lineItem->insertClock = currentGoup->clock;
}
void updateCacheClock(GroupItem* currentGoup, LineItem* lineItem) {
    currentGoup->clock++;
    lineItem->insertClock = currentGoup->clock;
}
void replaceCacheWithLRU(GroupItem* currentGoup, int id) {
    // find the line that longest time no access
    LineItem* lineItem = currentGoup->lineItemHead;
    LineItem* toReplaceLineItem;
    int minClock = lineItem->insertClock;
    while (lineItem != NULL) {
        if (lineItem->insertClock <= minClock) {
            minClock = lineItem->insertClock;
            toReplaceLineItem = lineItem;
        }
        lineItem = lineItem->next;
    }
    toReplaceLineItem->id = id;
    currentGoup->clock++;
    toReplaceLineItem->insertClock = currentGoup->clock;
}
void handleModify(GroupItem* currentGoup, int id) {
    LineItem* lineItem = checkInCache(currentGoup, id);
    // miss
    if (lineItem == NULL) {
        // printf("miss ");
        missCntGlobal++;
        // need to replace with LRU policy
        if (currentGoup->used == currentGoup->lineCnt) {
            // printf("eviction ");
            evictCntGlobal++;
            replaceCacheWithLRU(currentGoup, id);
        }
        // insert to group
        else {
            setCache(currentGoup, id);
        }
        // printf("hit ");
        hitCntGlobal++;
    }
    // hit
    else {
        // printf("hit hit");
        hitCntGlobal += 2;
        updateCacheClock(currentGoup, lineItem);
    }
}
void handleLoad(GroupItem* currentGoup, int id) {
    LineItem* lineItem = checkInCache(currentGoup, id);
    // miss
    if (lineItem == NULL) {
        // printf("miss ");
        missCntGlobal++;
        // need to replace with LRU policy
        if (currentGoup->used == currentGoup->lineCnt) {
            // printf("eviction ");
            evictCntGlobal++;
            replaceCacheWithLRU(currentGoup, id);
        }
        // insert to group
        else {
            setCache(currentGoup, id);
        }
    }
    // hit
    else {
        // printf("hit ");
        hitCntGlobal++;
        updateCacheClock(currentGoup, lineItem);
    }
}
void handleStore(GroupItem* currentGoup, int id) {
    LineItem* lineItem = checkInCache(currentGoup, id);
    // miss
    if (lineItem == NULL) {
        // printf("miss ");
        missCntGlobal++;
        // need to replace with LRU policy
        if (currentGoup->used == currentGoup->lineCnt) {
            // printf("eviction ");
            evictCntGlobal++;
            replaceCacheWithLRU(currentGoup, id);
        }
        // insert to group
        else {
            setCache(currentGoup, id);
        }
    }
    // hit
    else {
        // printf("hit ");
        hitCntGlobal++;
        updateCacheClock(currentGoup, lineItem);
    }
}


void handleOneTraceItem(Cache* cache, Trace* trace, int s, int E, int b) {
    if (trace->type == 'I') {
        // printf("\nINSTRUCTION\n");
        return;
    }
    // cache info
    // addr:    | id  | groupIdx(s-bits)  |  blockOff(b-bits) |
    int groupIdx = (trace->addr >> b) % (1 << s);
    int id = (trace->addr >> (b + s));
    GroupItem* currentGoup = getGroupByIndex(cache, groupIdx);
    if (trace->type == 'M') {
        handleModify(currentGoup, id);
        // printf("\n");
    }
    else if (trace->type == 'L') {
        handleLoad(currentGoup, id);
        // printf("\n");
    }
    else if (trace->type == 'S') {
        handleStore(currentGoup, id);
        // printf("\n");
    }
}
void desructCache(Cache* cahe) {
    GroupItem* groupItem = cahe->groupItemHead;
    while (groupItem != NULL) {
        GroupItem* nextGroupItem = groupItem->next;

        LineItem* lineItem = groupItem->lineItemHead;
        while (lineItem != NULL) {
            LineItem* nextLineItem = lineItem->next;

            free(lineItem);
            lineItem = NULL;

            lineItem = nextLineItem;
        }

        free(groupItem);
        groupItem = NULL;

        groupItem = nextGroupItem;
    }


    free(cahe);
    cahe = NULL;
}
int main(int argc, char* argv[]) {
    int ch;
    int argcS, argcE, argcB;
    char filename[30];
    while ((ch = getopt(argc, argv, "s:E:b:t:v")) != -1) {
        switch (ch) {
        case 's':
            argcS = str2int(optarg);
            break;
        case 'E':
            argcE = str2int(optarg);
            break;
        case 'b':
            argcB = str2int(optarg);
            break;
        case 't':
            strcpy(filename, optarg);
            break;
        case 'v':
            VERBOSE = 1;
            break;
        }
    }
    Cache* cahe = (Cache*)malloc(sizeof(Cache));
    // argcS = 5, argcE = 1, argcB = 5;

    initCache(cahe, argcS, argcE, argcB);

    int MAX_LINE_LEN = 1024;
    // parse file by line
    char line[MAX_LINE_LEN];
    FILE* fp = fopen(filename, "r");
    // FILE* fp = fopen("cache/traces/long.trace", "r");
    if (fp == NULL) {
        printf("open file %s error!\n", filename);
        exit(1);
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        Trace trace;
        parseTrace(&trace, line);
        handleOneTraceItem(cahe, &trace, argcS, argcE, argcB);
    }

    fclose(fp);
    desructCache(cahe);
    cahe = NULL;
    printSummary(hitCntGlobal, missCntGlobal, evictCntGlobal);
    // printf("%d %d %d\n", hitCntGlobal, missCntGlobal, evictCntGlobal);
    return 0;
}
