#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define GHOST _IOW ('a','a', struct ghost_map*)
#define UNGHOST _IOW ('a','b', int)

struct ghost_map                                                                
{                                                                               
	int pid;                                                                    
	int count_page;                                                             
	unsigned long *page_addr;                                                   
};
 
int* map()
{
    int* addr = (int*)mmap((void*)0x666000, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, 0, 0);
    printf("%p\n", addr);
    *addr = 0xcafebabe;
    return addr;
}
 
int main(int argc, char ** argv)
{
    int fd;
    int32_t value, number;

    unsigned long pa[] = {0x666000};

    int pid = strtol(argv[1], 0, 10);
    
    struct ghost_map to_map = 
    {
        .pid = pid, 
        .count_page = sizeof(pa) / sizeof(unsigned long), 
        .page_addr = pa
    };

    int* addr = map();

    printf("Opening Driver\n");

    fd = open("/dev/ghost_device", O_RDWR);

    if(fd < 0) 
    {
        printf("Cannot open device file...\n");
        return 0;
    }

    ioctl(fd, GHOST, &to_map); 

    fgetc(stdin);

    *addr = 0xfeedface;

    fgetc(stdin);

    ioctl(fd, UNGHOST, pid); 

    printf("Closing Driver\n");
    close(fd);
}
