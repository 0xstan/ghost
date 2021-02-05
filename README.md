# ghost 

linux kernel module that helps you inject page into a running process

## compilation

`make`

## usage

The lkm exposes 2 ioctls: 

```
#define GHOST _IOW ('a','a', struct ghost_map*)
#define UNGHOST _IOW ('a','b', int)

struct ghost_map                                                                
{                                                                               
    int pid;                                                                    
    int count_page;                                                             
    unsigned long *page_addr;                                                   
};
```

`GHOST` is for injection ( takes a pointer to `struct ghost_map` as argument).

`UNGHOST` for cleanup (takes a int (pid) as argument).

For injection, `pid` is the target pid, `count_page` is the number of pages in
the array `page_addr`, and `page_addr` is an array of virtual address of pages
that will be shared to the target process.

All pages must exist in the two processes (target and injector). The pages
of the target will be erased with the injector's one.
