#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_NUM ((MAX_CACHE_SIZE) / (MAX_OBJECT_SIZE))
#define	MAXLINE	 8192
typedef struct{
    char uri[MAXLINE];
    unsigned char object_content[MAX_OBJECT_SIZE];
    unsigned int clock;
} cache_line;
typedef struct{
    cache_line cache_lines[CACHE_NUM];
    int cnt;
    unsigned int global_clock;
}caches;
void cache_init(caches *mycaches);
void get_cache(caches *mycaches, char *uri, unsigned char *content, int *status);
void put_cache(caches *mycaches, char *uri, unsigned char *content, size_t content_size, int *status);