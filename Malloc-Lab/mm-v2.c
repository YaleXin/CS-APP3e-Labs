/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 *
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

 /*********************************************************
  * NOTE TO STUDENTS: Before you do anything else, please
  * provide your team information in the following struct.
  ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

// 单字占用的字节数
#define WSIZE       4
// 双字占用字节数
#define DSIZE       8
// 扩展堆的时候的大小
#define CHUNKSIZE   (1<<12)

#define MAX(x, y)   ((x) > (y) ? (x) : (y))
#define MIN(x, y)   ((x) < (y) ? (x) : (y))

// 将块大小和分配状态（已分配或者空闲）进行打包
#define PACK_SIZE_ALLOC(size, alloc)    ((size) | (alloc))
// 读写一个字
#define GET_WORD(p) (*(unsigned int *)(p))
#define PUT_WORD(p, val) (*(unsigned int *)(p) = (val))

// 从给定头部地址，获取块的大小和分配状态
#define GET_BLOCK_SIZE(p)   (GET_WORD(p) & ~0x7)
#define GET_BLOCK_ALLOC(p)   (GET_WORD(p) & 0x1)

// 给定有效载荷的开始地址，返回头部地址和脚部地址
#define HEADER_PTR(bp)  ((char *)(bp) - WSIZE)
#define FOOTER_PTR(bp)  ((char *)(bp) + GET_BLOCK_SIZE(HEADER_PTR(bp)) - DSIZE)

// 给定一个区块的有效载荷的开始地址，计算下一块和前一块的有效载荷的开始地址
#define NEXT_BLOCK_PAYLOAD_PTR(bp)  ((char *)(bp) + GET_BLOCK_SIZE(((char *)(bp) - WSIZE)))
#define PREV_BLOCK_PAYLOAD_PTR(bp)  ((char *)(bp) - GET_BLOCK_SIZE(((char *)(bp) - DSIZE)))

// 给定空闲块的有效载荷的开始地址，返回前驱所在地址和后继所在地址
#define PRED_PTR(bp)  ((char *)(bp))
#define SUCC_PTR(bp)  ((char *)(bp) + WSIZE)

// 堆的起始位置
static char* heap_listp;

static void* extend_heap(size_t words);
static void* find_fit(size_t asize);
static void place(void* bp, size_t asize);
static void* coalesce(void* bp);

int mm_init(void) {
    // 创建初始新堆(一个字用于填充，第二第三个字用于序言块，第四用于结尾块)
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void*)-1) {
        return -1;
    }
    PUT_WORD(heap_listp, 0);
    PUT_WORD(heap_listp + (1 * WSIZE), PACK_SIZE_ALLOC(DSIZE, 1));
    PUT_WORD(heap_listp + (2 * WSIZE), PACK_SIZE_ALLOC(DSIZE, 1));
    PUT_WORD(heap_listp + (3 * WSIZE), PACK_SIZE_ALLOC(0, 1));
    // 指针指向序言块头部往后一个字的位置，即有效载荷（初始化后，有效载荷大小为0）
    heap_listp += (2 * WSIZE);
    // 扩展堆，获得第一个空闲块
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) {
        return -1;
    }
    return 0;
}
static void* extend_heap(size_t words) {
    char* bp;
    size_t size;
    // 为了做到双字边界对齐，实际分配的字数大小必然是偶数
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE;
    if ((long)(bp = mem_sbrk(size)) == -1) {
        return NULL;
    }

    // 初始化空闲块的头部和脚部，以及尾块的头部
    PUT_WORD(HEADER_PTR(bp), PACK_SIZE_ALLOC(size, 0));
    PUT_WORD(FOOTER_PTR(bp), PACK_SIZE_ALLOC(size, 0));
    // 新的尾块的头部
    PUT_WORD(HEADER_PTR(NEXT_BLOCK_PAYLOAD_PTR(bp)), PACK_SIZE_ALLOC(0, 1));

    // 如果前面一块是空闲的，则进行合并
    return coalesce(bp);
}


void* mm_malloc(size_t size) {
    size_t asize;
    size_t extendsize;
    char* bp;
    if (size == 0)return NULL;
    if (size < DSIZE) {
        // 8字节用于存放头部和脚部，另外的8字节用于对齐
        asize = 2 * DSIZE;
    }
    else {
        // 调整到满足对齐要求和能够存放头部脚部的要求下，尽量接近8的整数倍
        asize = DSIZE * ((size + (DSIZE) + (DSIZE - 1)) / DSIZE);
    }
    // 寻找空闲链表
    if ((bp = find_fit(asize)) != NULL) {
        place(bp, asize);
        return bp;
    }

    // 如果没有找到足够大小的空闲块，则请求更多的内存空间
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize / WSIZE)) == NULL) {
        return NULL;
    }
    place(bp, asize);
    return bp;
}

static void* find_fit(size_t asize) {
    // 第一个空闲块，往后搜寻
    // 获取第一个空闲块地址
    char *first_free_block_bp = GET_WORD((char *)(heap_listp) - 2 * WSIZE);
    char *bp = first_free_block_bp;
    for(;bp != NULL;bp = GET_WORD(SUCC_PTR(bp))){
        if (GET_BLOCK_SIZE(HEADER_PTR(bp)) >= asize ) {
            return bp;
        }
    }
    return NULL;
}
// 如果bp指向的块比asize足够大，则进行分割
static void place(void* bp, size_t asize) {
    delete_free_block(bp);
    size_t current_block_size = GET_BLOCK_SIZE((HEADER_PTR(bp)));
    size_t remain_size = current_block_size - asize;
    char* old_header_ptr = HEADER_PTR(bp), * old_footer_ptr = FOOTER_PTR(bp);
    // 分割后新的空闲块
    char* new_header_ptr, * new_footer_ptr;
    // 如果当前块的大小减去用户需要的大小，满足最小块要求
    if (remain_size >= 2 * DSIZE) {
        // 切割，标记前半部分为“已分配”（修改头部和脚部）
        PUT_WORD(old_header_ptr, PACK_SIZE_ALLOC(asize, 1));
        PUT_WORD(old_header_ptr + asize - WSIZE, PACK_SIZE_ALLOC(asize, 1));
        new_header_ptr = old_header_ptr + asize - WSIZE + WSIZE;
        new_footer_ptr = old_footer_ptr;
        PUT_WORD(new_header_ptr, PACK_SIZE_ALLOC(remain_size, 0));
        PUT_WORD(new_footer_ptr, PACK_SIZE_ALLOC(remain_size, 0));
        coalesce(NEXT_BLOCK_PAYLOAD_PTR(bp));
    }
    else {
        PUT_WORD(HEADER_PTR(bp), PACK_SIZE_ALLOC(current_block_size, 1));
        PUT_WORD(FOOTER_PTR(bp), PACK_SIZE_ALLOC(current_block_size, 1));
    }

}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void* bp) {

    size_t size = GET_BLOCK_SIZE(HEADER_PTR(bp));
    // 修改分配状态并尝试合并
    PUT_WORD(HEADER_PTR(bp), PACK_SIZE_ALLOC(size, 0));
    PUT_WORD(FOOTER_PTR(bp), PACK_SIZE_ALLOC(size, 0));
    coalesce(bp);
}
// 根据教材596页的4种情况，对区块和并（如果有需要）
static void* coalesce(void* bp) {
    // 二话不说，先从空闲链表中删除该记录
    delete_free_block(bp);

    // 查看分配状态
    size_t prev_alloc = GET_BLOCK_ALLOC(FOOTER_PTR(PREV_BLOCK_PAYLOAD_PTR(bp)));
    size_t next_alloc = GET_BLOCK_ALLOC(HEADER_PTR(NEXT_BLOCK_PAYLOAD_PTR(bp)));
    // 查看当前块的大小
    size_t size = GET_BLOCK_SIZE(HEADER_PTR(bp));

    // 情况1：前后都分配了
    if (prev_alloc && next_alloc) {
        // 什么都不做
    }
    // 情况2：后一块是空闲的
    else if (prev_alloc && !next_alloc) {
        delete_free_block(NEXT_BLOCK_PAYLOAD_PTR(bp));
        size += GET_BLOCK_SIZE(HEADER_PTR(NEXT_BLOCK_PAYLOAD_PTR(bp)));
        // 重新写入分配状态和区块大小
        PUT_WORD(HEADER_PTR(bp), PACK_SIZE_ALLOC(size, 0));
        PUT_WORD(FOOTER_PTR(bp), PACK_SIZE_ALLOC(size, 0));
    }
    // 情况3：前一块是空闲的
    else if (!prev_alloc && next_alloc) {
        delete_free_block(PREV_BLOCK_PAYLOAD_PTR(bp));
        size += GET_BLOCK_SIZE(HEADER_PTR(PREV_BLOCK_PAYLOAD_PTR(bp)));
        PUT_WORD(FOOTER_PTR(bp), PACK_SIZE_ALLOC(size, 0));
        PUT_WORD(HEADER_PTR(PREV_BLOCK_PAYLOAD_PTR(bp)), PACK_SIZE_ALLOC(size, 0));
        bp = PREV_BLOCK_PAYLOAD_PTR(bp);
    }
    // 情况4：前后都是空闲的
    else if (!prev_alloc && !next_alloc) {
        delete_free_block(PREV_BLOCK_PAYLOAD_PTR(bp));
        delete_free_block(NEXT_BLOCK_PAYLOAD_PTR(bp));
        size += GET_BLOCK_SIZE(HEADER_PTR(PREV_BLOCK_PAYLOAD_PTR(bp))) + GET_BLOCK_SIZE(HEADER_PTR(NEXT_BLOCK_PAYLOAD_PTR(bp)));
        PUT_WORD(HEADER_PTR(PREV_BLOCK_PAYLOAD_PTR(bp)), PACK_SIZE_ALLOC(size, 0));
        PUT_WORD(FOOTER_PTR(NEXT_BLOCK_PAYLOAD_PTR(bp)), PACK_SIZE_ALLOC(size, 0));
        bp = PREV_BLOCK_PAYLOAD_PTR(bp);
    }

    // 再插入新的空闲块到空闲链表中
    // 初始化前驱和后继
    PUT_WORD(PRED_PTR(bp), NULL);
    PUT_WORD(SUCC_PTR(bp), NULL);
    insert_free_block(bp);

    return bp;
}
/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void* mm_realloc(void* bp, size_t size) {
    if(bp == NULL){
        return mm_malloc(size);
    }
    else if (size == (unsigned int)(0) && bp != NULL){
        mm_free(bp);
    }
    // 新块和旧块的部分内容必须相同
    // 相同部分的大小 = min(new_size, old_size)
    // 先申请一个新块，再拷贝，再free旧块
    // 这里所说的size指的是字节数，由于是双字对齐，
    // 因此size必然是8的整数倍，拷贝过程可以一次性拷贝4个字节
    char* new_bp = mm_malloc(size);
    if(new_bp == NULL){
        return NULL;
    }
    size_t copy_size = MIN(size, GET_BLOCK_SIZE(HEADER_PTR(bp)));
    char *src_ptr = bp, *dst_ptr = new_bp;
    size_t finish_size = 0;
    unsigned int word_content;
    for(; finish_size < copy_size; finish_size += 4){
        word_content = GET_WORD(src_ptr);
        PUT_WORD(dst_ptr, word_content);
        dst_ptr += 4;
        src_ptr += 4;
    }
    // 释放旧块
    mm_free(bp);
    return new_bp;
}

// 将未分配的空闲块插入到未分配链表中，使用插入头节点的方式
void insert_free_block(void *bp){
    // 获取第一个空闲块地址
    char *first_free_block_bp = GET_WORD((char *)(heap_listp) - 2 * WSIZE);
    // 如果之前没有空闲块
    if(first_free_block_bp == NULL){
        // 更新第一个空闲块的地址
        PUT_WORD((char *)(heap_listp) - 2 * WSIZE, bp);
        // 第一个空闲块的前驱指0
        PUT_WORD(PRED_PTR(bp), NULL);
        // 第一个空闲块的后继指0
        PUT_WORD(SUCC_PTR(bp), NULL);
        return;
    }
    // 第一个空闲块的前驱指向新的空闲块
    PUT_WORD(PRED_PTR(first_free_block_bp), bp);
    // 新的空闲块的后继指向第一个空闲块
    PUT_WORD(SUCC_PTR(bp), first_free_block_bp);
    // 更新第一个空闲块的地址
    PUT_WORD((char *)(heap_listp) - 2 * WSIZE, bp);

}
// 将空闲块从空闲链表中删除
void delete_free_block(void *bp){
    char *first_free_block_bp = GET_WORD((char *)(heap_listp) - 2 * WSIZE);
    char *find_bp = first_free_block_bp;
    for(;find_bp != NULL;find_bp = GET_WORD(SUCC_PTR(find_bp))){
        if(((char *)(bp)) == find_bp)break;
    }
    if(find_bp == NULL){
        // 如果没有找到，则直接返回（对应于刚刚申请的区块）
        return;
    }
    char *pred_block_bp = GET_WORD(PRED_PTR(find_bp));
    char *succ_block_bp = GET_WORD(SUCC_PTR(find_bp));
    
    // 如果删除后空闲链表为空
    if(first_free_block_bp == find_bp && succ_block_bp == NULL){
        PUT_WORD((char *)(heap_listp) - 2 * WSIZE, NULL);
        return;
    }
    // 删除中间
    if(succ_block_bp != NULL && pred_block_bp != NULL){
        PUT_WORD(PRED_PTR(succ_block_bp), pred_block_bp);
        PUT_WORD(SUCC_PTR(pred_block_bp), succ_block_bp);
    }

    // 如果是删除最后一个
    else if(succ_block_bp == NULL){
        PUT_WORD(SUCC_PTR(pred_block_bp), succ_block_bp);
    }
    // 如果是删除第一个
    else if(pred_block_bp == NULL){
        PUT_WORD(PRED_PTR(succ_block_bp), pred_block_bp);
        PUT_WORD((char *)(heap_listp) - 2 * WSIZE, succ_block_bp);
    }

}

