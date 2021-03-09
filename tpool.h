// RemoteBASH
// Thread Pool Header

typedef struct tpool {} tpool_t;

tpool_t tpool;

int tpool_init(void (*process_task)(int));

int tpool_add_task(int new_task);


// EOF
