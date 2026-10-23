#include <iostream>
#include <string>
#include <cctype>
#include <vector>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
using namespace std;





bool splitargs(string &command,vector<string>&args){
    string tmp;
    bool inside_dbquote=false;
    bool inside_squote=false;
    bool escaped=false; //for  " \"sjv\" " type cases
    for(char c:command){
        if(escaped){
            tmp+=c;
            escaped=false;
        }
        else if(c=='\\'&&inside_dbquote)escaped=true;
        else if(static_cast<unsigned char>(c)=='"' && !inside_squote){
            inside_dbquote=!inside_dbquote;
        }
        else if(static_cast<unsigned char>(c)=='\'' && !inside_dbquote){
            inside_squote=!inside_squote;
        }
        else if(isspace(static_cast<unsigned char>(c)) && !inside_dbquote && !inside_squote){
            if(!tmp.empty()){
                args.push_back(tmp);
                tmp.clear();
            }
        }
        else tmp+=c;
    }
    if(!tmp.empty())args.push_back(tmp);
    if(inside_dbquote || inside_squote){
        cerr<<"Unmatched quote detected!"<<endl;
        return false;
    }
    return true;

}
vector<char*>buildargv(vector<string>&args){
    vector<char*>argv;
    for(string &arg:args){
    argv.push_back(arg.data()); //.data() gives pointer to first char of string
    }
    argv.push_back(nullptr);
    return argv;
}

void pwd(int *last_status){
    char cwd[4096];
    if(getcwd(cwd,sizeof(cwd))!=nullptr){ //getcwd expects a char pointer to buffer and size of the buffer to write the path. also,returns the pointer to the forst element after writing
        cout<<cwd<<endl;
        *last_status=0;
    }
    else{
        *last_status=1;
        perror("pwd");
    }
}
int findredirectpos(const vector<string>&args){
    for(int i=0;i<args.size();i++){
        if(args[i]==">" || args[i]==">>" || args[i]=="<")return i;
    }
    return -1;
}
bool BuildArgsForRedirection(vector<string>&args,int pos,string &sign,string &filename){
    if(pos!=-1){
        if(pos==0 || pos!=static_cast<int>(args.size()-2)){
            cerr<<"Invalid syntax near sign!"<<endl;
            return false;
        }
        sign=args[pos];
        filename=args[pos+1];
        args.erase(args.begin()+pos,args.end()); //remove arguments from redirect_pos to end
    }
    return true;
}
bool BuildFDforRedirection(string &filename,string &sign,int &fd){
    if(sign==">") fd=open(filename.c_str(),O_WRONLY|O_CREAT|O_TRUNC,0666); //learn again
    else if(sign==">>")fd=open(filename.c_str(),O_WRONLY|O_CREAT|O_APPEND,0666);
    else if(sign=="<")fd=open(filename.c_str(),O_RDONLY);
    if(fd==-1){
        perror("open");
        return false;
    }
    int target=(sign=="<")?STDIN_FILENO:STDOUT_FILENO;

    if(dup2(fd,target)==-1){
        perror("dup2");
        close(fd);
        return false;
    }
    close(fd);
    return true;
}


int findpipe(const vector<string>&args){
    for(int i=0;i<args.size();i++){
        if(args[i]=="|")return i;
    }
    return -1;
}
bool splitpipe(const vector<string>&args,vector<string>&left,vector<string>&right,int pos){
    if(pos<=0 || pos>=static_cast<int>(args.size()-1))return false;
    for(int i=0;i<pos;i++)left.push_back(args[i]);
    for(int i=pos+1;i<static_cast<int>(args.size());i++)right.push_back(args[i]);
    return true;
}
pid_t retry_waitpid(pid_t pid, int* last_status,int options=0){
    pid_t result;
    do{
        result= waitpid(pid,last_status,options);
    }while(result == -1 && errno==EINTR);
    return result;
}
void waitforchilds(vector<pid_t>pids){
    //wait for the generated childs to finish
    for(int i=0;i<pids.size();i++){
        int status;
        if(retry_waitpid(pids[i],&status,0)==-1){
            perror("waitpid");
            //continue reaping rest of childs
        }
    }
}
//MULTIPLE PIPING
void multiplepipe(vector<string>&args,string &sign,string &filename,int &last_status,bool isinteractive,pid_t shell_pid,bool background){
    vector<vector<string>>commands;
    vector<string>tmp;
    vector<pid_t>pids;
    pid_t first_child_pid;
    
    //build commands vector by splitting pipes
    for(string &s:args){
        if(s=="|"){
            if(tmp.empty()){ //reject cat || sort
                cerr<<"Invalid Pipe syntax!"<<endl;
                last_status=2;
                return;
            }
            commands.push_back(tmp);
            tmp.clear();
        }
        else{
            tmp.push_back(s);
        }
    }
    if(!tmp.empty()){
        commands.push_back(tmp);
    }
    else{ //reject cat |
        cerr<<"Invalid Pipe syntax!"<<endl;
        last_status=2;
        return;
    }

    //implementation
    int prev_read=-1;
    for(int i=0;i<commands.size();i++){
        int fd[2];
        bool pipeflag=false;


        if(i<commands.size()-1){
            if(pipe(fd)==-1){
                perror("pipe");
                //close previous childs
                if(prev_read!=-1)close(prev_read);
                //wait for the generated childs to finish
                waitforchilds(pids);
                last_status=1;
                return;
            }
            pipeflag=true;
        }


        cout.flush();
        pid_t pid =fork();
        if(pid==0){
            //restore terminal job control signals in child
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            if(i==0){ //first child
                if(setpgid(0,0)<0){
                    perror("setpgid");
                }
            }
            else{
                if(setpgid(0,first_child_pid)<0){
                    perror("setpgid");
                }
            }
            if(prev_read!=-1){ //not the first command
                if(dup2(prev_read,STDIN_FILENO)==-1){
                    perror("dup2 stdin");
                    _exit(1);
                }
                close(prev_read);
            }
            if(pipeflag){ //pipe was created(not last command)
                if(dup2(fd[1],STDOUT_FILENO)==-1){
                    perror("dup2 out");
                    _exit(1);
                }
                close(fd[0]);
                close(fd[1]);
            }
            int redirect_pos=findredirectpos(commands[i]);
            if(redirect_pos!=-1){
                if(!BuildArgsForRedirection(commands[i],redirect_pos,sign,filename)){
                    _exit(1);
                }
                int fd=-1;
                if(!BuildFDforRedirection(filename,sign,fd)){
                    _exit(1);
                }
            }
            vector<char*>argv=buildargv(commands[i]);
            execvp(argv[0],argv.data());
            perror("Failed to execute command");
            _exit(127);
            
        }
        //fork failed
        else if(pid<0){
            perror("fork");
            if(pipeflag){
                close(fd[0]);
                close(fd[1]);
            }
            if(prev_read!=-1)close(prev_read);
            last_status=1;
            waitforchilds(pids);
            return;
        }
        //parent
        if(i==0){ //first child
            if(setpgid(pid,pid)<0){
                perror("setpgid");
            }
            first_child_pid=pid;
        }
        else{
            if(setpgid(pid,first_child_pid)<0){
                perror("setpgid");
            }
        }
        pids.push_back(pid);
        if(prev_read!=-1)close(prev_read);
        if(pipeflag){
            close(fd[1]);
            prev_read=fd[0];
        }
        
        
    }
    if(!background){
        bool waitpid_failed=false;
        int pipe_status;
        if(isinteractive){
            if(tcsetpgrp(STDIN_FILENO,first_child_pid)<0){
                perror("tcsetpgrp");
                last_status=1;
            }
        }   
        //wait for childs to finish
        for(int i=0;i<pids.size();i++){
            int status;
            if(retry_waitpid(pids[i],&status)==-1){
                perror("waitpid");
                waitpid_failed=true;
                continue; //try waitiing for other childs
            }
            //last pid
            if(i==pids.size()-1){
                if(WIFEXITED(status)){
                    pipe_status=WEXITSTATUS(status);
                }
                else if(WIFSIGNALED(status)){
                    pipe_status=128+WTERMSIG(status);
                }
            }
        }
        last_status=waitpid_failed?1:pipe_status;
    }   
    else{
        cout<<"Process group with pgid ["<<first_child_pid<<"]"<<"has started"<<endl;
    }
    
    if(isinteractive && !background){
    if(tcsetpgrp(STDIN_FILENO,shell_pid)<0){
        perror("tcsetpgrp");
    }
    } 
    return;

}

void reapchilds(){
    int status;
    pid_t pid;
    while((pid=(waitpid(-1,&status,WNOHANG)))>0){
        //-1: check all childs
        //WNOHANG: return immediately if no child finished
        if (WIFEXITED(status)) {
        cout << "[" << pid << "] exited with status "
             << WEXITSTATUS(status) << endl;
        }
        else if (WIFSIGNALED(status)) {
            cout << "[" << pid << "] terminated by signal "
                << WTERMSIG(status) << endl;

        }
    }
}
































int main(){

    string command;

    int last_status=0; //exit status of the most recently executed foreground command
    bool is_interactive=isatty(STDIN_FILENO); //checks if the shell is interactive, not redirected inputs like ./myshell < commmands.txt
    pid_t shellpgid=getpgrp();
    if(is_interactive){
        //wait until shell's pgid is in foreground
        while(tcgetpgrp(STDIN_FILENO)!=(shellpgid=getpgrp())){
            kill(-shellpgid,SIGTTIN);// '-' sign means we're targetting each process in that process group.
            //SIGTTIN:Stop a background process group attempting terminal input
        }
        //The shell must not be stopped by terminal job control signals
        signal(SIGINT, SIG_IGN);
        signal(SIGQUIT, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        //set shell's pid to its own process group
        shellpgid=getpid();
        if(setpgid(shellpgid,shellpgid)<0){
            perror("setpgid");
            return 1;
        }
        //set shell pgid to foreground
        if(tcsetpgrp(STDIN_FILENO,shellpgid)<0){
            perror("tcsetpgrp");
            return 1;
        }
        //debug line
        cout << "Shell PID: " << getpid() << endl;
        cout << "Shell PGID: " << getpgrp() << endl;
        cout << "Terminal foreground PGID: "
            << tcgetpgrp(STDIN_FILENO) << endl;
    }

    while(true){
        reapchilds(); //get updates each iterations
        //ignore ctrl+c for terminating the whole shell
        signal(SIGINT,SIG_IGN); //SIGINT: signal produced by ctrl+c, SIG_IGN: ignore that signal
        vector<string>args;
        vector<char*>argv;
        string filename;
        string sign; // >, >>, <
        bool background=false;
        int pipepos;
        int redirect_pos;

        cout<<"myshell$ ";
        if(!getline(cin,command)){
            cout<<endl;
            break;
        }
        if(command=="exit")break;


        //implementation
        if(!splitargs(command,args)){
            last_status=2;
            continue;
        }
        if(args.empty()){
            continue;
        }
        else if(args.back()=="&"){
            background=true;
            args.pop_back();
            if(args.empty()){
                last_status=2; //syntax error
                continue;
            }
        }


        //error for commands
        if(args.size()==2 && args[0]=="echo" && args[1]=="$?"){
            cout<<last_status<<endl;
            continue;
        }

        //pwd(built-in)
        if(args[0]=="pwd"){
            pwd(&last_status);
            continue;
        }
        //cd(built-in)
        else if(args[0]=="cd"){
            const char*path;
            //consider cd expects atmost 1 path
            if(args.size()>2){
                cerr<<"Too Many arguments!"<<endl;
                last_status=1;
                continue;
            }
            else if(args.size()==1){//if no argument passed after cd
                path=getenv("HOME");
                if(path==nullptr){
                    cerr<<"cd: Home not set!"<<endl;
                    last_status=1;
                    continue;
                }
            }
            else path=args[1].c_str(); //chdir expects char* style string
            if(chdir(path)==-1){
                perror("cd");
                last_status=1;
            }
            else last_status=0;
            continue;
        }


        //command1 Pipe command2
        /*pipepos=findpipe(args);
        if(pipepos!=-1){
            vector<string>left;
            vector<string>right;
            if(splitpipe(args,left,right,pipepos)){
                int fd[2];
                if(pipe(fd)==-1){
                    perror("pipe");
                    last_status=1;
                    continue;
                }
                //producer child
                cout.flush();
                pid_t pid1=fork();
                if(pid1==0){
                    if(dup2(fd[1],STDOUT_FILENO)==-1){
                        perror("dup2");
                        _exit(1);
                    }
                    close(fd[0]);
                    close(fd[1]);

                    //if redirection present
                    redirect_pos=findredirectpos(left);
                    if(redirect_pos!=-1){
                        
                        if(!BuildArgsForRedirection(left,redirect_pos,sign,filename)){
                            _exit(1);
                        }
                        int fd=-1;
                        if(!BuildFDforRedirection(filename,sign,fd)){
                            _exit(1);
                        }
                            
                        

                    }

                    argv=buildargv(left);
                    execvp(argv[0],argv.data());
                    perror("Failed to execute command!");
                    _exit(127);

                }
                else if(pid1>0){ //parent
                    //consumer child
                    cout.flush();
                    pid_t pid2=fork();
                    if(pid2==0){
                        if(dup2(fd[0],STDIN_FILENO)==-1){
                            perror("dup2");
                
                            _exit(1);
                        }
                        
                        close(fd[0]);
                        close(fd[1]);

                        //if redirection present
                        redirect_pos=findredirectpos(right);
                        if(redirect_pos!=-1){
                            
                            if(!BuildArgsForRedirection(right,redirect_pos,sign,filename)){
                                _exit(1);
                            }
                            int fd=-1;
                            if(!BuildFDforRedirection(filename,sign,fd)){
                                _exit(1);
                            }
                            

                        }
                        
                        argv=buildargv(right);
                        execvp(argv[0],argv.data());
                        perror("Failed to execute command");
                        _exit(127);

                    }
                    
                    else if(pid2>0){ //back to parent
                        int status1,status2;
                        close(fd[0]);
                        close(fd[1]);
                        if(waitpid(pid1,&status1,0)==-1){
                            perror("waitpid");
                            return 1;
                        }
                        if(waitpid(pid2,&status2,0)==-1){
                            perror("waitpid");
                            return 1;
                        }
                        //faliure depends on second command after pipe
                        if(WIFEXITED(status2)){
                            last_status=WEXITSTATUS(status2);
                        }
                        else if(WIFSIGNALED(status2)){
                            last_status=128+WTERMSIG(status2);
                        }

                        
                    }
                    else { //fist child success, second child fork failed
                        close(fd[0]);
                        close(fd[1]);
                        int status1; //wait for child1 to finish
                        if(waitpid(pid1,&status1,0)==-1){
                            perror("waitpid");
                            return 1;
                        }
                        if(WIFEXITED(status1)){
                            last_status=WEXITSTATUS(status1);
                        }
                        else if(WIFSIGNALED(status1)){
                            last_status=128+WTERMSIG(status1);
                        }


                    }
                }
                else{ //first fork failed, no childs produced
                    perror("fork producer");
                    last_status=1;
                    close(fd[0]);
                    close(fd[1]);
                    continue;

                }




            }
            else{
                cerr<<"Invalid pipe position!"<<endl;
                last_status=1;
                continue;
            }
            continue;


        } 
        */
       pipepos=findpipe(args);
       if(pipepos!=-1){
            multiplepipe(args,sign,filename,last_status,is_interactive,shellpgid,background);
            continue;
        }
        
        
        // command >,>>,<.<< filename
        redirect_pos=findredirectpos(args);
        if(redirect_pos!=-1){
            
            if(!BuildArgsForRedirection(args,redirect_pos,sign,filename)){
                last_status=2;
                continue;
            }

        }
        //general command

        cout.flush();
        pid_t pid=fork();
        if(pid==0){
            //set child's pid to its own pgid
            if(setpgid(0,0)<0){//0: caller's pid
                perror("setpgid");
                _exit(1);
            } 

            /*//restore ctrl+c behaviour for child processes
            signal(SIGINT,SIG_DFL); //SIG_DFL:change to default*/

            //restore terminal job control signals in child
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            signal(SIGTSTP, SIG_DFL);
            signal(SIGTTIN, SIG_DFL);
            signal(SIGTTOU, SIG_DFL);
            // command > filename
            if(redirect_pos!=-1){
                int fd=-1;
                if(!BuildFDforRedirection(filename,sign,fd)){;
                    _exit(2);
                }
                
            }

            //process the char** argv
            argv=buildargv(args);
            execvp(argv[0],argv.data()); //returns iff execvp fails so we write the error in the next lines
            perror("Failed to execute command!");
            _exit(127);
            
    
        }
        else if(pid>0){
            
            if(setpgid(pid,pid)<0){//implement the same in parent block to account for race condition
                perror("setpgid");
                last_status=1;
            } 
            //if background process, we dont waitpid
            if(background){
                cout<<"process"<<" ["<<pid<<"] "<<"has started"<<endl;
                
            }
            else{
                int status;
                //give foreground access to the child pgid
                if(is_interactive){
                    if(tcsetpgrp(STDIN_FILENO,pid)<0){
                        perror("tcsetpgrp");
                    }
                }    
                if(retry_waitpid(pid,&status,0)==-1){
                    perror("waitpid");
                    last_status=1;
                }
                else if(WIFEXITED(status)){
                    last_status= WEXITSTATUS(status);
                }
                else if(WIFSIGNALED(status)){
                    last_status=128+WTERMSIG(status);
                }
                //recover foreground access to shell
                if(is_interactive){
                    if(tcsetpgrp(STDIN_FILENO,shellpgid)<0){
                        perror("tcsetpgrp");
                    }
                }    
            }
        }
        else {
            last_status=1;
            cout<<"Process creation failed"<<endl;
        }

    }
    
    
return 0;
}