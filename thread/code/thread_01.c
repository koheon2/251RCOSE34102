#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

void* magic_box(void* arg) {
    printf("multiplying %d by 6...\n", (int)arg);
    pthread_exit((int)arg * 6);
}


int main(int argc, char* argv[]){
    pthread_t tid;
    int* new_number;
    
    printf("Hey magic boxs, multiply 10 by 6\n");
                      
    int status = pthread_create(&tid, NULL, magic_box, (void*)10); //pthread_</1>(</2>);

    if(status!=0){
        printf("error");
        return -1;
    }

    pthread_join(tid, (void**)&new_number);//pthread_</3.1>(</3.2>);

    printf("the new number is %d\n", new_number);

    return 0;
}

/*
Expected output:

Hey magic box, multiply 10 by 6
multiplying 10 by 6...
the new number is 60
*/

