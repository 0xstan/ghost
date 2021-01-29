#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>

int main()
{
    int* addr = (int*)mmap((void*)0x666000, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, 0, 0);
    printf("%p\n", addr);
    *addr = 0xdeadbeef;
    while(1)
    {
        printf("%x\n", *addr);
        sleep(1);
    }
    return 0;
}
