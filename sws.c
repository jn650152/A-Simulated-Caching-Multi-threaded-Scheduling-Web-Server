 /*
 File: sws.c
 Author: Jing Luo
 Date: 2015/5/30
 Purpose: This file is to simulate a simple web server which schedule client requests.
 */
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include"network.h"
#include <unistd.h>
#include <netdb.h>
#include <netinet/in.h>
#include <regex.h>
#include <sys/stat.h>
#include <math.h>
#include <time.h>
//#include <semaphore.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/syscall.h>
struct RCB//the struct for PCB
{
	int socket;
	FILE *fp;
	int left_len;
	int quantum;
	int seq_num;
	struct RCB *next;
	char path[1000];
};
struct linklist//the struct for ready queue
{
	struct RCB *head;
	struct RCB *tail;
};
//struct for the fourth algorithm
struct path_node{//a node for storing information about a file in buffer
	char array[1000];//used to store the path of this file
	char *file;//a pointer to the buffer storing  all the content of a file which is recently requested 
	struct path_node *next;//pointer to the next path_node
	struct path_node *prev;//pointer to the previous path_node
};
struct path_linklist{//used to create a buffer
	struct path_node * head;
	struct path_node *tail;
};

//declaration
char *readFile(FILE *file, int len);//read the content of file
int Match(char *a, char *b, int len, int indicator);//check which algorithm is applied or whether two paths are the same for LRU
void Submit_Scheduler(char *path, int match, FILE *fp, struct RCB *current_worker_queue_head);//intialize fields of RCB and submit it to scheduler
void add_to_readyQueue(int match, struct RCB *New_RCB);//add request to a single ready queue
void SJF_sort(struct RCB *New_RCB);//sorting request according to size
void service(int match, struct RCB * To_Run_Request,int from_queue);//service request
void service_SJF(struct RCB *To_Run_Request);//service SJF algorithm
void service_RR(struct RCB *To_Run_Request, int match);//service RR algorithm
void service_MLFB(struct RCB *To_Run_Request, int from_queue);//service MLFB algorithm
void service_LRU(struct RCB *To_Run_Request);//service LRU
void add_to_queue(struct RCB *To_Run_Request, int mark);//add to multilevel queues
void modify_path_buffer(char *file, char *path);//modify buffer for LRU
/*void Display_ready_queue(int match);//display the content of ready queues for testing*/
void service_FCFS(struct RCB *To_Run_Request);//service FCFS
/*void Display_buffer();//display buffer for recent requested files*/
void * worker_thread(void *arg);//worker threads' function 




//global variables
int i = 0;//indicate the number of requests in system
int j = 0;//used to indicate the number of files in buffer.(for LRU)
int working_thread_num = 0;//indicates how many worker threads are working currently
struct linklist *ready_queue;//create ready queue pointers for different algorithms
struct linklist *ready_queue_1;
struct linklist *ready_queue_2;
struct linklist *ready_queue_3;
struct linklist *work_queue;//create work queue
//count represents the number of requests in a single ready queue(for RR and SJF)
int count = 0;
//count1, count2, count3 are respectively represent the number of requests in '8KB' queue, '64KB' queue, 'RR'queue in MLFB algorithm
int count1 = 0,count2 = 0, count3 = 0;
struct path_linklist *path_buffer;//create a buffer for recording all the information for each recently requested file
int job_num = 0;
//parameters for semaphores and mutex locks
pthread_mutex_t mutex0 = PTHREAD_MUTEX_INITIALIZER;//used to proctect variable 'i', which is used to set the seq_num
pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER;//used to proctect variable 'count', which is the number of nodes in the single ready queue
pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;//used to proctect variable 'count1', which is the number of nodes in the first level queue
pthread_mutex_t mutex3 = PTHREAD_MUTEX_INITIALIZER;//used to proctect variable 'count2', which is the nomber of nodes in the second level queue
pthread_mutex_t mutex4 = PTHREAD_MUTEX_INITIALIZER;//used to proctect variable 'count3',which is the nomber of nodes in the third level queue
pthread_mutex_t workqueue = PTHREAD_MUTEX_INITIALIZER;//used to proctect work queue
pthread_mutex_t workingthreadnum = PTHREAD_MUTEX_INITIALIZER;//used to proctect working_thread_num variable
pthread_cond_t availableworkerthread = PTHREAD_COND_INITIALIZER;//condition variable used to suspend and resume worker threads
pthread_mutex_t readyqueue = PTHREAD_MUTEX_INITIALIZER;//used to protect ready queue for SJF, RR, MLFB
pthread_t worker_threads[1000];//used to store tid
pthread_mutex_t jobnum = PTHREAD_MUTEX_INITIALIZER;//used to protect variable 'job_num'
pthread_cond_t maximumrequests = PTHREAD_COND_INITIALIZER;//condition variable used to suspend and resume the thread to make sure the web server cannot  open connection when it concurrently handles more than or equal to 100 requests

int main(int argc, const char *argv[]){
    //get the argument
    int port_num = atoi(argv[1]);//get the port number
    char scheduler[5] ={0,0,0,0,0};
    strcpy(scheduler,argv[2]);//get the name of scheduler
    //check which algorithm is used
    char check_array[5][4]= {"SJF0","RR00","MLFB","LRU0","FCFS"};
    //use a number to represent scheduling algorithms:1 for SJF, 2 for RR, 3 for MLFB, 4 for LRU, 5 for FCFS 
    int match = 0;
    int iter_scheduler;
    for(iter_scheduler = 0; iter_scheduler < 5; iter_scheduler++)
    {
	if(Match(scheduler,check_array[iter_scheduler],4,1))
	{
	 	match = iter_scheduler+1;
		break;
	}
    }
    int worker_thread_num = atoi(argv[3]);//get the number of threads
    //check whether arguments are valid
    if( port_num < 1024 || port_num > 65335|| match == 0||argc != 4){
        printf("command argument is invalid\n");
	fflush(stdout);
    }else{
        int socket = 0;
        //char path[1000];
        //char request[1000];
        //memset(path,'\0', 1000);
        /*bind the web server to a port number and initialize */
        network_init(port_num);
	//allocate and initialize work queue
	work_queue =(struct linklist *)malloc(sizeof(struct linklist *));
	work_queue -> head = NULL;
	work_queue -> tail = NULL;
        //establish ready queue
	if(match == 1||match ==2||match ==4 || match == 5)
	{
		//initialize a single ready queue for SJF or RR or LRU or FCFS algorithm
		ready_queue =(struct linklist *)malloc(sizeof(struct linklist));
		ready_queue -> head = ready_queue -> tail = NULL;
		if(match == 4){//create a buffer for temporarily storing recently requested files
               		path_buffer =(struct path_linklist *)malloc(sizeof(struct path_linklist));
                	path_buffer -> head = path_buffer -> tail = NULL;
		}
	}else{
		//initialize three queues for MLFB
                ready_queue_1 =(struct linklist *)malloc(sizeof(struct linklist));
                ready_queue_1 -> head = ready_queue_1 -> tail = NULL;

                ready_queue_2 =(struct linklist *)malloc(sizeof(struct linklist));
                ready_queue_2 -> head = ready_queue_2 -> tail = NULL;
			
                ready_queue_3 =(struct linklist *)malloc(sizeof(struct linklist));
                ready_queue_3 -> head = ready_queue_3 -> tail = NULL;
	}
	working_thread_num = worker_thread_num;//reset the number of currently running worker threads as the total number of worker threads
	int a;
	for(a = 0; a < worker_thread_num; a++){//create threads
		pthread_create(&worker_threads[a], NULL, worker_thread,(void*) &match);
	}
	while(1){//wait until all worker threads are successfully created and go to sleep.
		int again = 1;//an indicator to tell whether all the worker threads go to sleep
		pthread_mutex_lock(&workingthreadnum);//protect the variable 'working_thread_num'
                if(working_thread_num != 0){//if there are still some worker threads running, do nothing
			;	
		}else{
			again = 0;//if all worker threads go to sleep, set again as 0
		}
		pthread_mutex_unlock(&workingthreadnum);	
		if(again == 0){break;}//if all worker threads go to sleep, break the loop
	}
	// a infinite loop for waiting, scheduling and servicing requests from clients all the time
        while(1){
	    	network_wait();//wait for clients to connect
		pthread_mutex_lock(&jobnum);//check the number of request being handled.
		if(job_num >= 100){//if there are already 100 requests being handled
			pthread_cond_wait(&maximumrequests, &jobnum);//the thread is suspended and not open new connection any more
		}	
		pthread_mutex_unlock(&jobnum);
	    	socket = network_open();//open connection
            	//create and set a new RCB
            	struct RCB *New_RCB;
            	New_RCB = (struct RCB *)malloc(sizeof(struct RCB));
		New_RCB -> socket = socket;
		New_RCB -> next= NULL;
	    	//insert the new RCB into work queue
	    	pthread_mutex_lock(&workqueue);//protect work queue
		if(work_queue -> head == NULL){
			work_queue -> head = work_queue -> tail = New_RCB;
		}else{
			work_queue -> tail -> next = New_RCB;
			work_queue -> tail = New_RCB;
		}
		pthread_mutex_unlock(&workqueue);
		//if there are worker threads waiting, wake up a waiting threads and increase the number of worker threads which are currently running
		pthread_mutex_lock(&workingthreadnum);//protect 'working_thread_num' variable
		if(working_thread_num < worker_thread_num){
			pthread_cond_signal(&availableworkerthread);
			working_thread_num++;
		}
	   	pthread_mutex_unlock(&workingthreadnum);
	}
    }
    return 0;
}
//worker threads' function
void * worker_thread(void * match_address)
{
	int match = *((int *)match_address);
	while(1){
		struct RCB *current_worker_queue_head = NULL;
		//dequeue from work queue
		pthread_mutex_lock(&workqueue);//protect work queue
		current_worker_queue_head = work_queue -> head;
		if(current_worker_queue_head != NULL){
			work_queue -> head = work_queue -> head -> next;
			if(work_queue -> head == NULL){
				work_queue -> tail = NULL;
			}
		pthread_mutex_lock(&jobnum);//protect the number of requests being processed
		job_num++;
		pthread_mutex_unlock(&jobnum);
		}
		pthread_mutex_unlock(&workqueue);
		if(current_worker_queue_head != NULL){//if there is request in work queue, submit the request to scheduler
        		char path[1000];
        		char request[1000];
        		memset(path,'\0', 1000);
        		memset(request,'\0', 1000);
			//parse the request					
	    		// get request from a client
            		read(current_worker_queue_head -> socket, request, 1000);
	    		// parse path from the request
            		regex_t regex;
            		int reti = regcomp(&regex, "GET .* HTTP/1.1", 0);
            		if (reti) {
               		        fprintf(stderr, "Could not compile regex\n");
                		exit(1);
            		}
            		reti = regexec(&regex, request, 0, NULL, 0);
            
                	//if the request is in the right format
            		if (!reti) {
                		//get the path of the requested file
                		sscanf(request,"GET /%s HTTP/1.1\n", path);
				FILE *fp;
                		fp = fopen(path,"r");// open and read file
				// if the file doesn't exist, it displays "Not found"
                		if(fp == NULL){
                    			write(current_worker_queue_head -> socket, "HTTP/1.1 404 File not found\n\n",29);
                    			//disconnect
		    			close(current_worker_queue_head -> socket);
                		}else{ //if the file exists, the request is submited to scheduler and send back "OK"
					Submit_Scheduler(path, match, fp, current_worker_queue_head);
                    			write(current_worker_queue_head -> socket, "HTTP/1.1 200 OK\n\n",17);
		     			// Display_ready_queue(match);
                		}	
            		}else if (reti == REG_NOMATCH) {//if the request doesn't match the right format, it send back "bad request"
                		write(current_worker_queue_head -> socket, "HTTP/1.1 400 Bad request\n\n",26);
               			//disconnect
               			close(current_worker_queue_head -> socket); 
            		}
           		regfree(&regex);
		}else{
			struct RCB *current_ready_queue_head = NULL;
			int lock_queue_num = -1;//an indicator represents which ready queue will be locked. 0 for the single ready queue, 1 for 1st level queue, 2 for 2nd level queue, 3 for 3rd level queue
			if(match != 3){//for SJF,RR, LRU, FCFS
				pthread_mutex_lock(&readyqueue);//protect ready queue	
				lock_queue_num = 0;//reset the indicator of which queue is locked as 0
				current_ready_queue_head = ready_queue -> head;//get the head node of ready queue
			}else{//MLFB, get the next job to run from ready queues and reset the indicator of which queue is locked 
                                pthread_mutex_lock(&readyqueue);//protect the single ready queue        
				if(count1 != 0){
					lock_queue_num = 1;
					current_ready_queue_head = ready_queue_1 -> head;
				}else if(count2 != 0){
					lock_queue_num = 2;
					current_ready_queue_head = ready_queue_2 -> head;
				}else if(count3 !=0){
					lock_queue_num = 3;
					current_ready_queue_head = ready_queue_3 -> head;
				}
			}
			if(current_ready_queue_head != NULL){
				//dequeue from ready queue
				if(match != 3){//for RR, SJF, LRU, FCFS, dequeue
					ready_queue -> head = ready_queue -> head -> next;
					if(ready_queue -> head == NULL){
						ready_queue -> tail = NULL;
					}
					pthread_mutex_lock(&mutex1);//protect 'count' variable
					count--;//decreases 'count', which is the number of nodes in ready queue
					pthread_mutex_unlock(&mutex1);
				}else{//for MLFB, dequeue from ready queue and decrease the number of nodes in each ready queue
					
					if(count1 != 0){
						ready_queue_1 -> head = ready_queue_1 -> head -> next;
						if(ready_queue_1 -> head == NULL){
							ready_queue_1 -> tail = NULL;
						}	
						pthread_mutex_lock(&mutex2);
						count1--;
						pthread_mutex_unlock(&mutex2);
					}else if(count2 != 0){
						ready_queue_2 -> head = ready_queue_2 -> head -> next;
						if(ready_queue_2 -> head == NULL){
							ready_queue_2 -> tail = NULL;
						}
						pthread_mutex_lock(&mutex3);
						count2--;
						pthread_mutex_unlock(&mutex3);
					}else if(count3 !=0){
						ready_queue_3 -> head = ready_queue_3 -> head -> next;
						if(ready_queue_3 -> head == NULL){
							ready_queue_3 -> tail = NULL;
						}
						pthread_mutex_lock(&mutex4);
						count3--;
						pthread_mutex_unlock(&mutex4);
					}
				
				}
			}
			pthread_mutex_unlock(&readyqueue);
			
			
			if(current_ready_queue_head != NULL){//if no request is in work queue and ready queue is not empty, service request.
				//service 
				service(match, current_ready_queue_head, lock_queue_num);
			}else{//if no request is in work queue and ready queue is empty, the thread goes to sleep 
				pthread_mutex_lock(&workingthreadnum);
				working_thread_num--;
				pthread_cond_wait(&availableworkerthread, &workingthreadnum);
				pthread_mutex_unlock(&workingthreadnum);
			}
		}

		
	}
}
//function used to store the content of a file in a buffer  and return a pointer to that buffer
char *readFile(FILE *file, int len) {//len is the maximum btyes can be read
    
    if (file == NULL) {// no valid file
        printf("Error: file pointer is null.");
        exit(1);
    }
    
    int maximumLineLength = 128;//set a default size of a buffer
    char *lineBuffer = (char *)malloc(sizeof(char) * maximumLineLength);//dynamically allocate memory space for 
    
    if (lineBuffer == NULL) {//if buffer failed to create
        printf("Error allocating memory for line buffer.");
        exit(1);
    }
    //save the content of a file in a buffer which can be reallocated
    char ch = getc(file);
    int count = 0;
    len = len -1;// spare one for '\0'
    while (ch != EOF && len != 0) {//if it is not the end of the file and doesn't achieve its assigned length, continue the loop 
        if (count == maximumLineLength) {//if the size of buffer is larger than the current size, the buffer get reallocate
            maximumLineLength += 128;
            lineBuffer = realloc(lineBuffer, maximumLineLength);
            if (lineBuffer == NULL) {//if it fails to reallocate, print error
                printf("Error reallocating space for line buffer.");
                exit(1);
            }
        }
        lineBuffer[count] = ch;
        count++;
        ch = getc(file);
	len--;
    }
    lineBuffer[count] = '\0';
    
    //save memory space for storing the content of the file
    char *line = (char *)malloc(count + 1);
    strncpy(line, lineBuffer, (count + 1));
    free(lineBuffer);
    char *constLine = line;
    return constLine;
}

//check which algorithm it uses according to characters(indicator == 1) or check whether two paths are the same for LRU(indicator == 2)
int Match(char *a, char *b, int len, int indicator)
{
	int match =0;
	int i;
	if(indicator == 1){
		for(i = 0; i < len && *b!= '0'; i++)
		{
			if(*a == *b)
			{
				a = a + 1;
				b = b +1;
				if(len-1 == i||*b == '0')
				{
					match = 1;
				}
			}else{
				break;
			}
		}
	}else{
		for(i = 0; i < len; i++)
		{
			if(*a == *b)
			{
				a = a + 1;
				b = b +1;
				if(len-1 == i)
				{
					match = 1;
				}
			}else{
				break;
			}
		}
	}
	return match;
}


//creat a new RCB in Request table and submit it to scheduler
void Submit_Scheduler(char *path, int match, FILE *fp, struct RCB * current_worker_queue_head)
{
	//create and set a new RCB
	struct RCB *New_RCB = current_worker_queue_head;
	New_RCB -> fp = fp;
	memset(New_RCB -> path,'\0', 1000);
	strcpy(New_RCB -> path, path);
        // get the size of the requested file
        struct stat *buf;
        buf = malloc(sizeof(struct stat));
	stat(path, buf);
        New_RCB -> left_len = buf -> st_size;
        free(buf);
	pthread_mutex_lock(&mutex0);
	New_RCB -> seq_num = ++i;//set sequence number for RCB
	pthread_mutex_unlock(&mutex0);
	New_RCB -> next = NULL;
	//set quantum
	if(match ==1 || match == 4|| match == 5)
	{
		New_RCB -> quantum = New_RCB -> left_len;//SJF, LRU, FCFS
	}else if(match == 2)
	{
		New_RCB -> quantum = pow(2,13);//RR
	}else{
		New_RCB -> quantum = pow(2,13);//MLFB
	}
	//add to ready queue according to different algorithms	
	add_to_readyQueue(match, New_RCB);
}

//sorting requests according to size
void SJF_sort(struct RCB *New_RCB)
{
	struct RCB *temp = ready_queue -> head;
	struct RCB *temp0 = NULL;
        //find the position ofa node in ready queue where the 
	//size of new RCB is smaller or equal to the node's
	while(New_RCB -> left_len >= temp -> left_len) 
	{
		temp0 = temp;
		temp = temp -> next;	
		if(temp == NULL){break;}
	}
	if(ready_queue -> head == temp)//add to the head
	{
		New_RCB -> next = temp;
		ready_queue -> head = New_RCB;

	}else if(temp == NULL){//add to the tail
		temp0 -> next = New_RCB;
		ready_queue -> tail = New_RCB;
	}else{//add to the proper postion in ready queue apart from the head and the tail.
		temp0 -> next = New_RCB;
		New_RCB -> next = temp;
	}
}
void add_to_readyQueue(int match, struct RCB *New_RCB)
{
        if(match !=3)// for RR, SJF and LRU, FCFS
        {
		pthread_mutex_lock(&readyqueue);  
                if(ready_queue -> head == NULL && ready_queue -> tail == NULL)//if ready queue is empty
                {
			ready_queue -> head = ready_queue -> tail = New_RCB;//set the new RC as both head and tail
                }else{//if ready queue is not empty
			if(match == 2|| match == 4 || match == 5){//for RR, LRU and FCFS
                        	ready_queue -> tail -> next = New_RCB;//add new RCB to the tail
                	        ready_queue -> tail = New_RCB;
			}else{//for SJF
				SJF_sort(New_RCB);//sorting according to size
			}
                }
		pthread_mutex_lock(&mutex1);
		count++;//increase the number of request in ready queue
		pthread_mutex_unlock(&mutex1);
		printf("Request for file %s admitted.\n", New_RCB -> path);
                fflush(stdout);
		pthread_mutex_unlock(&readyqueue);  
        }else{//for MLFB
		pthread_mutex_lock(&readyqueue);  
                if(ready_queue_1 -> head == NULL  && ready_queue_1 -> tail == NULL)//if the first level ready queue of MLFB is empty
                {
                        ready_queue_1 -> head = ready_queue_1 -> tail = New_RCB;//set new RCB as both head and tail
                }else{//if ready queue is not empty, add to the tail
                        ready_queue_1 -> tail -> next = New_RCB;
                        ready_queue_1 -> tail = New_RCB;
                }
		pthread_mutex_lock(&mutex2);
		count1++;//increase the number of request in first level ready queue
		pthread_mutex_unlock(&mutex2);
		printf("Request for file %s admitted.\n", New_RCB -> path);
		fflush(stdout);
		pthread_mutex_unlock(&readyqueue);  	
        }
}

//service request according to different algorithms
void service(int match, struct RCB * To_Run_Request, int from_queue)
{
	if(match == 1)//run SJF
	{
		service_SJF(To_Run_Request);
	}else if(match == 2){//run RR
		service_RR(To_Run_Request, match);
	}else if(match == 3){//run MLFB
		service_MLFB(To_Run_Request, from_queue);
	}else if (match == 4){//run LRU
		service_LRU(To_Run_Request);	
	}else{//run FCFS
		service_FCFS(To_Run_Request);
	}
}

//service SJF
void service_SJF(struct RCB *To_Run_Request)
{
                // store the content of a file in a buffer
                char *file = readFile(To_Run_Request -> fp, To_Run_Request -> quantum);
                //write the content of the requested file back to the client
                write(To_Run_Request -> socket, file,To_Run_Request -> left_len);
		printf("Sent %d bytes of file %s.\n",To_Run_Request -> left_len, To_Run_Request -> path);
                fflush(stdout);
                printf("Request for file %s completed.\n",To_Run_Request -> path);
		fflush(stdout);
		fclose(To_Run_Request -> fp);// close open file
                free(file);//release memory
                close(To_Run_Request -> socket);//disconnect
		free(To_Run_Request);//free request node
		pthread_mutex_lock(&jobnum);
		job_num--;
		pthread_mutex_unlock(&jobnum);
}

//service RR
void service_RR(struct RCB *To_Run_Request, int match)//this function can be called in both RR and MLFB algorithms
{
		//get the size that the file will be read next 
                // store the content of a file in a buffer
                int size;
                if(To_Run_Request -> quantum < To_Run_Request -> left_len){
                        size = To_Run_Request -> quantum;
                }else{
                        size = To_Run_Request -> left_len;
		 }
                char *file = readFile(To_Run_Request -> fp,size);//save in a buffer
                //write the content of the requested file back to the client
                write(To_Run_Request -> socket, file,size);
		printf("Sent %d bytes of file %s.\n",size, To_Run_Request -> path);
                fflush(stdout);
		if(size == To_Run_Request -> quantum && To_Run_Request -> quantum != To_Run_Request -> left_len ){//if it doesn;t yet  compeletely finish its job
                        To_Run_Request -> left_len = To_Run_Request -> left_len - To_Run_Request -> quantum;//reset the left length of the RCB
             		if(match == 2){//RR:insert into ready queue
				To_Run_Request ->next = NULL;
				pthread_mutex_lock(&readyqueue);
				
				if(ready_queue -> head == NULL){
					ready_queue -> head = ready_queue -> tail = To_Run_Request;
				}else{
					ready_queue -> tail ->next = To_Run_Request;
					ready_queue ->tail = To_Run_Request;
				}
				pthread_mutex_lock(&mutex1);
				count++;	
				pthread_mutex_unlock(&mutex1);
				pthread_mutex_unlock(&readyqueue);
			}else{//MLFB:insert into ready queue3
			
				To_Run_Request ->next = NULL;
				if(ready_queue_3 -> head == NULL){
					ready_queue_3 -> head = ready_queue_3 -> tail = To_Run_Request;
				}else{
					ready_queue_3 -> tail ->next = To_Run_Request;
					ready_queue_3 ->tail = To_Run_Request;
				}
				pthread_mutex_lock(&mutex4);
				count3++;	
				pthread_mutex_unlock(&mutex4);
				pthread_mutex_unlock(&readyqueue);
			}
			free(file);//release memory
                }else{//if the request already finished its job
                        To_Run_Request -> left_len = 0;//set left length as 0
			printf("Request for file %s completed.\n",To_Run_Request -> path);
			fflush(stdout);
			free(file);//release memory
                        fclose(To_Run_Request -> fp);// close open file
			//disconnect
                        close(To_Run_Request -> socket);
                        free(To_Run_Request);
			pthread_mutex_lock(&jobnum);
			job_num--;
			pthread_mutex_unlock(&jobnum);
		 }
}

//service MLFB
void service_MLFB(struct RCB *To_Run_Request, int from_queue)
{
	if(from_queue == 1){//the to run job is from  the first level queue
                // store the content of a file in an array
                int size;//used to represent the time current job will run 
                if(To_Run_Request -> quantum < To_Run_Request -> left_len){
                        size = To_Run_Request -> quantum;
                }else{
                        size = To_Run_Request -> left_len;
                }
                char *file = readFile(To_Run_Request -> fp,size);//save the content of a file in a buffer
                //write the content of the requested file back to the client
                write(To_Run_Request -> socket, file,size);
		printf("Sent %d bytes of file %s.\n",size, To_Run_Request -> path);
                fflush(stdout);
		if(size == To_Run_Request -> quantum){//if the job doesn't yet finish, reset fields of this request and ready queue
                        To_Run_Request -> left_len = To_Run_Request -> left_len - To_Run_Request -> quantum;
			To_Run_Request -> quantum = pow(2,16);
                        To_Run_Request -> next = NULL;
			add_to_queue(To_Run_Request, 2);//this RCB adds to ready_queue_2
			free(file);//release memory
                }else{//if this job finishes its job, reset fields of the RCB and the first level ready queue
                        To_Run_Request -> left_len = 0;
                        printf("Request for file %s completed.\n",To_Run_Request -> path);
			fflush(stdout);
			free(file);//release memory
                        fclose(To_Run_Request -> fp);// close open file
                        close(To_Run_Request -> socket);//disconnect
                        free(To_Run_Request);
			pthread_mutex_lock(&jobnum);
			job_num--;
			pthread_mutex_unlock(&jobnum);
                }
	}else if(from_queue ==2){//else if there are requests in the second level ready queue
                // store the content of a file in an array
                int size;//get the size to run next
                if(To_Run_Request -> quantum < To_Run_Request -> left_len){
                        size = To_Run_Request -> quantum;
                }else{
                        size = To_Run_Request -> left_len;
                }
                char *file = readFile(To_Run_Request -> fp,size);//save in a buffer
                //write the content of the requested file back to the client
                write(To_Run_Request -> socket, file,size);
		printf("Sent %d bytes of file %s.\n",size, To_Run_Request -> path);
		fflush(stdout);
		//if it doesn't yet finished, reset fields of the RCB and the second level ready queue
                if(size == To_Run_Request -> quantum){
                        To_Run_Request -> left_len = To_Run_Request -> left_len - To_Run_Request -> quantum;
                        To_Run_Request -> next = NULL;
                        add_to_queue(To_Run_Request, 3);//demote the third level ready queue
                        free(file);//release memory
                }else{//else it finishes its job, reset fields of the RCB
                        To_Run_Request -> left_len = 0;
                        printf("Request for file %s completed.\n",To_Run_Request -> path);
			fflush(stdout);
			free(file);//release memory
                        fclose(To_Run_Request -> fp);// close open file
                        //disconnect
                        close(To_Run_Request -> socket);
                        free(To_Run_Request);
		pthread_mutex_lock(&jobnum);
		job_num--;
		pthread_mutex_unlock(&jobnum);
                }
	}else{//else if there are requests in the third level ready queue
		service_RR(To_Run_Request, 3);//apply RR algorithm 
		
	}
}

//used to demote to the next ready queue
void add_to_queue(struct RCB *To_Run_Request, int mark)
{
	if(mark == 2){//demote the second level ready queue
				pthread_mutex_lock(&readyqueue);
		//if the ready queue the current RCB needs to demote to is empty
		//set the RCB as the head and tail of the ready queue
		if(ready_queue_2 -> head == NULL){
			ready_queue_2 -> head = ready_queue_2 -> tail = To_Run_Request;
		}else{//else add the RCB to the tail of the ready queue
			ready_queue_2 -> tail -> next = To_Run_Request;
			ready_queue_2 -> tail = To_Run_Request;
		}
		pthread_mutex_lock(&mutex3);
		count2++;
		pthread_mutex_unlock(&mutex3);
				pthread_mutex_unlock(&readyqueue);
	}else if(mark == 3){//demote the third level ready queue
		pthread_mutex_lock(&readyqueue);
		//if the ready queue the current RCB needs to demote to is empty
		//set the RCB as the head and tail of the ready queue
		if(ready_queue_3 -> head == NULL){
			ready_queue_3 -> head = ready_queue_3 -> tail = To_Run_Request;
		}else{//else add the RCB to the tail of the ready queue
			ready_queue_3 -> tail -> next = To_Run_Request;
			ready_queue_3 -> tail = To_Run_Request;
		}
		pthread_mutex_lock(&mutex4);
		count3++;
		pthread_mutex_unlock(&mutex4);
		pthread_mutex_unlock(&readyqueue);
	}
}
//service LRU
void service_LRU(struct RCB *To_Run_Request){
	To_Run_Request = ready_queue -> head;
        struct path_node *node = NULL;
	if(path_buffer -> head != NULL){
		node = path_buffer -> head;
	}
	int num;
	for(num =1; num <= j; num++){//check whether requested file is in buffer
		if(Match(node -> array, To_Run_Request -> path, sizeof(To_Run_Request -> path),2)){//requested file is already in buffer
			write(To_Run_Request -> socket, node -> file, To_Run_Request -> left_len);
			if(num == j){//if the requested file's path matches with the last one in buffer, nothing needs to change in the linklist of buffer
				;
			}else if(num != 1){//if it matches with the one neither the first one nor the last one in the linklist of buffer, move it to the tail of the linklist
				node -> prev -> next = node -> next;
				node -> next -> prev = node -> prev;
				node -> prev = path_buffer -> tail;
				node -> next = NULL;
				path_buffer -> tail -> next = node;
				path_buffer -> tail = node;
			}else{//if it matches with the first one in the linklist of buffer, move this node from head node to the tail node of the linklist
				path_buffer -> head = node -> next;
				node -> prev = path_buffer -> tail;
				node -> next = NULL;
				path_buffer -> tail -> next = node;
				path_buffer -> tail = node;
			}
			break;
		}
		node = node-> next;
	}
	if(num  == j+1){//request file is not in buffer	
 		struct path_node *newnode;
		if(j < 3){//there is still available space in buffer
			newnode = (struct path_node *)malloc(sizeof(struct path_node));
		}else{//the buffer is full
			newnode = path_buffer -> head;
		}
		memset(newnode -> array,'\0', 1000);
		strcpy(newnode -> array, To_Run_Request -> path);
		newnode -> prev = NULL;
		struct path_node *newnode_next = NULL;
		if(j==3){
			newnode_next = newnode -> next;
		}
		newnode -> next = NULL;	
		newnode -> file = readFile(To_Run_Request -> fp,To_Run_Request -> left_len); 
		write(To_Run_Request -> socket, newnode -> file, To_Run_Request -> left_len);
		if(j < 3){//if the buffer is not full
			if(path_buffer -> head == NULL){//if the buffer is empty
				path_buffer -> head = path_buffer -> tail = newnode;
			}else{//if the buffer is not empty
				newnode -> prev = path_buffer -> tail;
				path_buffer -> tail -> next = newnode;
		 		path_buffer -> tail = newnode;
			}
			j++;
		}else{//if the buffer is full			
			path_buffer -> head = newnode_next;
			newnode -> prev = path_buffer -> tail;
			path_buffer -> tail -> next = newnode;
			path_buffer -> tail = newnode;
		}

	}
	//Display_buffer();
        ready_queue -> head = To_Run_Request -> next;//change the head of ready queue
        if(ready_queue -> head == NULL){//if no more request in ready queue
                ready_queue -> tail = NULL;//set tail as NULL
        }
        fclose(To_Run_Request -> fp);// close open file
        close(To_Run_Request -> socket);//disconnect
	printf("Request for file %s completed.\n",To_Run_Request -> path);
	fflush(stdout);
	free(To_Run_Request);//free request node
printf("1028: before pthread_mutex_lock(&jobnum);\n");
fflush(stdout);
		pthread_mutex_lock(&jobnum);
printf("1028: after pthread_mutex_lock(&jobnum);\n");
fflush(stdout);
		job_num--;
printf("1034: before pthread_mutex_unlock(&jobnum);\n");
fflush(stdout);
		pthread_mutex_unlock(&jobnum);
printf("1034: after pthread_mutex_unlock(&jobnum);\n");
fflush(stdout);
}
//FCFS
void service_FCFS(struct RCB *To_Run_Request){	
	//get the head of ready queue as the next job to run
        To_Run_Request = ready_queue -> head;
        //printf("service request %d: len is %d bytes\n",To_Run_Request -> seq_num, To_Run_Request -> left_len);
        // store the content of a file in a buffer
        char *file = readFile(To_Run_Request -> fp, To_Run_Request -> quantum);
        //write the content of the requested file back to the client
        write(To_Run_Request -> socket, file,To_Run_Request -> quantum);
        ready_queue -> head = To_Run_Request -> next;//change the head of ready queue
        if(ready_queue -> head == NULL){//if no more request in ready queue
              	ready_queue -> tail = NULL;//set tail as NULL
        }
        fclose(To_Run_Request -> fp);// close open file
        free(file);//release memory
        close(To_Run_Request -> socket);//disconnect
	printf("Request for file %s completed.\n",To_Run_Request -> path);
	fflush(stdout);
	free(To_Run_Request);//free request node
printf("1059: before pthread_mutex_lock(&jobnum);\n");
fflush(stdout);
		pthread_mutex_lock(&jobnum);
printf("1059: after pthread_mutex_lock(&jobnum);\n");
fflush(stdout);
		job_num--;
printf("1059: before pthread_mutex_unlock(&jobnum);\n");
fflush(stdout);
		pthread_mutex_unlock(&jobnum);
printf("1059: after pthread_mutex_unlock(&jobnum);\n");
fflush(stdout);
}
/*//used to display the current ready queue for testing
void Display_ready_queue(int match)
{
	if(match == 1 || match == 2|| match == 4|| match == 5)//if it is for RR, SJF or LRU
	{
		printf("the single ready queue :\n");
		struct RCB *temp = ready_queue -> head;
		if(temp == NULL){//if no request in ready queue, print nothing in queue
			printf("nothing is in ready queue\n");
		}else{//else, display the request in ready queue from the head to the tail
			do{
			printf("Request %d, left_len: %d bytes, path:%s\n",temp -> seq_num, temp -> left_len, temp -> path);
			temp = temp -> next;
			}while(temp != NULL);
		}
	}else{//if it is MLFB
		printf("MLFB ready queues:\n");
		//print '8KB queue'
		printf("'8 KB' ready queue:\n");
                struct RCB *temp = ready_queue_1 -> head;
                if(temp == NULL){
			printf("nothing is in '8KB' queue\n");
		}else{
			do{
                	printf("Request %d, left_len: %d bytes\n",temp -> seq_num, temp -> left_len);
			temp = temp -> next;
               		 }while(temp != NULL); 
		} 
		//print '64KB' queue
                printf("'64 KB' ready queue:\n");
                temp = ready_queue_2 -> head;
                if(temp == NULL){
                        printf("nothing is in '64KB' queue\n");
                }else{
                        do{
			printf("Request %d, left_len: %d bytes\n",temp -> seq_num, temp -> left_len);
                        temp = temp -> next;
                         }while(temp != NULL);
                }
		//print the third level 'RR' queue
		printf("RR queue:\n");
                temp = ready_queue_3 -> head;
                if(temp == NULL){
                        printf("nothing is in the third level 'RR' queue\n");
                }else{
                        do{
			printf("Request %d, left_len: %d bytes\n",temp -> seq_num, temp -> left_len);
                        temp = temp -> next;
                         }while(temp != NULL);
                }      
       
	}
	printf("\n");
}

void Display_buffer()//display buffer for recent requested files
{
	printf("buffer:\n");
	struct path_node *temp;
	temp = path_buffer -> head;
	if(temp == NULL){
		printf("nothing in buffer.\n");
	}else{
		while(temp){
			printf("path:%s\n", temp -> array);
			temp = temp -> next;
		}
	}
}
*/
