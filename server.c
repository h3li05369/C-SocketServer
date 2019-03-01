#include<stdio.h>
#include<stdlib.h>
#include<sys/ioctl.h>
#include<sys/time.h>
#include<sys/types.h>
#include <sys/stat.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<errno.h>
#include<netinet/in.h>
#include<string.h>
#include<unistd.h>
#include<fcntl.h>
#include<poll.h>

#define FALSE 0
#define TRUE 1

#define PORT 8080
#define BYTES 1024

#define PAGENOTEXISTS "error/notfound/index.html"
#define INVALIDREQUEST "error/invalid/index.html"

#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define RESET "\x1B[0m"

void error(const char * errorMsg);

int fileExists(char * filePath);

char* getRequestMethod(char *request);

char* parseFilePathFromRequest(char *request);

int countDigits(int n);

int responseCode(char *request);


int main(int argc, char const *argv[]){
	int sock, cli;
	int rc, on = 1;
	unsigned int len;
	struct sockaddr_in server;
	int bufsize = 1024;
	int statusCode;
	char *buffer = malloc(bufsize);
	int byte_msg;
	char data_to_send[BYTES];
	char* filelocation;
	char* requestHeader = malloc(1024 * sizeof(char));
	char* responseHeaderContent = malloc(1024 * sizeof(char));
	struct pollfd fds[100];
	int fd;
	int timeout;
	int nfds = 1, current_size = 0, i, j;
	int end_server = 0, compress_array = 0;
	int close_conn;


	long fsize;

	if((sock = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		error("socket error");
	}

	if((setsockopt(sock, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on)))==-1){
		close(sock);
	    error("setsockopt");
	}
	//Set Socket to be unblocking
	if(ioctl(sock, FIONBIO, (char *)&on) < 0){
		close(sock);
		error("ioctl error");
	}
	memset(&server, 0, sizeof(server));
	server.sin_family = AF_INET;
	server.sin_port = htons(PORT);
	server.sin_addr.s_addr = INADDR_ANY;
	bzero(&server.sin_zero, 0);

	len = sizeof(struct sockaddr_in);
	
	if((bind(sock, (struct sockaddr *)&server, len)) == -1){
		close(sock);
		error("bind");
	}

	if((listen(sock, 32)) == -1){
		close(sock);
		error("listen");
	}
	else
		printf("Listening on port %d\n", PORT);

	memset(fds, 0, sizeof(fds));

	fds[0].fd = sock;
	fds[0].events = POLLIN;

	//Timeout of 2 minutes
	timeout = (2*60*1000);
	
	do{
		//printf("Waiting for poll()...\n");
		rc = poll(fds, nfds, timeout);
		if(rc < 0){
			perror("poll failed");
			break;
		}
		if(rc == 0){
			printf("poll timed out\n");
			break;
		}
		//Accept incoming connections
		do{	
			cli = accept(sock, NULL, NULL);
			if(cli<0){
				if(errno != EWOULDBLOCK){
					perror("Accept failed");
					end_server = 1;
				}
				break;
			}
			//printf("New incoming connection\n");
			fds[nfds].fd = cli;
			fds[nfds].events = 1;
			nfds++;
		} while(cli != -1);
		current_size = nfds;
		for(i = 1; i < current_size; i++){
			close_conn = 0;
			if(fds[i].revents == 0){
				continue;
			}

			if(fds[i].revents!=POLLIN){
				printf("Error! revents = %d\n", fds[i].revents);
				end_server = 1;
				break;
			}
			else{
				memset(buffer ,0 , bufsize); 
				rc = recv(fds[i].fd, buffer, bufsize, 0);
				if(rc < 0){
					if(errno != EWOULDBLOCK){
						perror("recv() failed");
						close_conn = TRUE;
					}
					goto label_close_connection;
				}
				if(rc == 0){
					close_conn = TRUE;
					goto label_close_connection;
				}

				// Data received
				// Can move ahead
				strcpy(requestHeader, buffer);
				filelocation = parseFilePathFromRequest(requestHeader);
				fd=open(filelocation, O_RDONLY);
				statusCode = responseCode(requestHeader);
				if(statusCode == 200)
					printf(KGRN"Serving %s" RESET"\n\n", filelocation);
				else
					printf(KRED"Error: %d" RESET"\n\n", statusCode);

				if(statusCode == 200) {
					struct stat st;
					stat(filelocation, &st);
					fsize = st.st_size;
					sprintf(responseHeaderContent, "HTTP/1.1 200 OK\nContent-Length: %ld\nContent-Type: */*\n\n", fsize);
					rc = write(fds[i].fd, responseHeaderContent, strlen(responseHeaderContent));
				}
				else if(statusCode == 400){
					sprintf(responseHeaderContent, "HTTP/1.1 400 Bad Request\n");
					rc = write(fds[i].fd, responseHeaderContent, strlen(responseHeaderContent));
				}
				else if(statusCode == 404){
					sprintf(responseHeaderContent, "HTTP/1.1 404 Not Found\nContent-Type: text/html\nConnection: Close\n\n");
					rc = write(fds[i].fd, responseHeaderContent, strlen(responseHeaderContent));
					close_conn = TRUE;
				}
				if(rc < 0){
					perror("Send Response Header failed");
					close_conn = TRUE;
					goto label_close_connection;
				}
				while ((byte_msg=read(fd, data_to_send, BYTES))>0 ){
					if(write(fds[i].fd, data_to_send, byte_msg) < 0){
						perror("send data failed");
						close_conn = TRUE;
						goto label_close_connection;
					}
				}
				label_close_connection:
				if(close_conn){
					//printf("Closed Descriptor:%d\n", fds[i].fd);
					close(fds[i].fd);
					fds[i].fd = -1;
					compress_array = 1;
				}
			}
		}
		if(compress_array){
			compress_array = 0;
			for(i = 0; i < nfds; i++){
				if(fds[i].fd == -1){
					for(j = i; j < nfds - 1; j++){
						fds[j].fd = fds[j+1].fd;
					}
					i--;
					nfds--;
				}
			}
		}

	} while(end_server == FALSE);

	//Clean all open sockets
	for(i = 0; i < nfds; i++){
		if(fds[i].fd >= 0)
			close(fds[i].fd);
	}

	return 0;
}

void error(const char *errorMsg){
	perror(errorMsg);
	exit(-1);
}

int fileExists(char *filePath){
	if(open(filePath, O_RDONLY)==-1)
		return 0;
	return 1;
}

char* getRequestMethod(char *req){
	char *request = malloc(1024 * sizeof(char));
	strcpy(request, req);
	const char delimiter[2] = " ";

	//We will tokenise the request and return the first token
	//For example from "POST /res/index.php", "POST" will be extracted"
	char *requestMethod = (char *)malloc(8 * sizeof(char));
	char *path = (char *)malloc(1024 * sizeof(char));
	requestMethod = strtok(request, delimiter);
	path = strtok(NULL, delimiter);
	if(requestMethod==NULL){
		return "BAD";
	}
	if(path == NULL){
		return "BAD";	
	}
	else{
		if(path[0]!='/'){
			return "BAD";
		}
	}
	return requestMethod;
}

char* parseFilePathFromRequest(char *req){
	char *request = malloc(1024 * sizeof(char));
	strcpy(request, req);
	if(strcmp(getRequestMethod(request),"BAD")==0)
		return INVALIDREQUEST;
	char *temp = malloc(1024 * sizeof(char));
	char *resultPath = malloc(1024 * sizeof(char));
	char *path = malloc(1024 * sizeof(char));
	// delimiter for request tokenisation
	const char delimiter[2] = " ";
	//We will tokenise the request string twice to get the second token
	//For example from "GET /res/page", "/res/page" will be extracted"  
	strtok(request, delimiter);
	path = strtok(NULL, delimiter);
	strcpy(temp, "public\0");
	strcat(temp, path);
	//Checking if request address ends in filename or directory name
	char *name = malloc(256 * sizeof(char));
	name = strrchr(path, '/')+1;
	int lengthOfName = strlen(name);
	char *extension = malloc(64 * sizeof(char));
	extension = strrchr(name, '.');
	if(lengthOfName!=0){
		if(extension!=NULL){
			if(strcmp(extension, ".html")==0||strcmp(extension, ".htm")==0
					||strcmp(extension, ".css")==0||strcmp(extension, ".js")==0
					||strcmp(extension, ".ico")==0||strcmp(extension, ".png")==0
					||strcmp(extension, ".jpeg")==0||strcmp(extension, ".jpg")==0){
				if(fileExists(temp))
					strcpy(resultPath, temp);
				else{
					strcpy(resultPath, PAGENOTEXISTS);
				}
			}

			else
				strcpy(resultPath, PAGENOTEXISTS);
		}
		
		else{
			//Convert address of type "public/foo/bar" to "public/foo/bar/" so that they qualify the next if statement
			strcat(temp, "/");
			lengthOfName = 0;
		}
	}
	if(lengthOfName==0){
		strcat(temp, "index.htm");
		if(fileExists(temp)){
			strcpy(resultPath, temp);
		}
		else{
			//now check for html
			strcat(temp, "l");
			if(fileExists(temp)){
				strcpy(resultPath, temp);
			}
			else{
				strcpy(resultPath, PAGENOTEXISTS);
			}
		}
	}
	return resultPath;
}



int countDigits(int n){
	int i=0;
	for(i=0;n>0;i++){
		n/=10;
	}
	return i;
}

int responseCode(char *req){
	char *request = malloc(1024 * sizeof(char));
	strcpy(request, req);
	char *requestMethod = malloc(8*sizeof(char));
	requestMethod = getRequestMethod(request);
	if(strcmp(requestMethod,"BAD")==0)
		return 400;
	if(strcmp(parseFilePathFromRequest(request), PAGENOTEXISTS) == 0)
		return 404;
	return 200; 
}
