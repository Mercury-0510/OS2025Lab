#include"kernel/types.h"
#include"user.h"

int main(int argc, char*argv[]){
    int ftc[2];
    int ctf[2];
    pipe(ftc);
    pipe(ctf);
    
    int farther_pid = getpid();
    
    int pid = fork();
    if(pid < 0){
        printf("Fork failed.\n");
        exit(-1);
    }
    else if(pid > 0){
        close(ftc[0]);
        close(ctf[1]);
        
        char msg[] = "ping";
        
        write(ftc[1], msg, sizeof(msg));
        close(ftc[1]);
        
        char buf[64];
        read(ctf[0], buf, sizeof(buf));
        printf("%d: received pong from pid %d\n", farther_pid, pid);
        
        close(ctf[0]);
        wait(0);
        exit(0);
    }
    else{
        close(ftc[1]);
        close(ctf[0]);
        
        int child_pid = getpid();
        
        char buf[64];
        read(ftc[0], buf, sizeof(buf));
        printf("%d: received ping from pid %d\n", child_pid, farther_pid);
        
        char msg[] = "pong";
        write(ctf[1], msg, sizeof(msg));
        
        close(ftc[0]);
        close(ctf[1]);
        exit(0);
    }
}