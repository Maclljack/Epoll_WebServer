#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include<dirent.h>
#include<sys/stat.h>
#include<fcntl.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include<sys/sendfile.h>

//http server epoll
//
//
//
char *get_mime_type(char *name)
{
     char* dot;
     dot = strrchr(name, '.'); 
     if (dot == (char*)0)
         return "text/plain; charset=utf-8";
     if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
         return "text/html; charset=utf-8";
     if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
         return "image/jpeg";
     if (strcmp(dot, ".gif") == 0)
         return "image/gif";
     if (strcmp(dot, ".png") == 0)
         return "image/png";
     if (strcmp(dot, ".css") == 0)
         return "text/css";
     if (strcmp(dot, ".au") == 0)
         return "audio/basic";
     if (strcmp( dot, ".wav") == 0)
         return "audio/wav";
     if (strcmp(dot, ".avi") == 0)
         return "video/x-msvideo";
     if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
         return "video/quicktime";
     if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
         return "video/mpeg";
     if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
         return "model/vrml";
     if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
         return "audio/midi";
     if (strcmp(dot, ".mp3") == 0)
         return "audio/mpeg";
     if (strcmp(dot, ".ogg") == 0)
         return "application/ogg";
     if (strcmp(dot, ".pac") == 0)
         return "application/x-ns-proxy-autoconfig";
 
     return "text/plain; charset=utf-8";
  }



//设置非阻塞
int setnonblocking(int fd) 
{
	int old_option=fcntl(fd,F_GETFL); 
	int new_option=old_option|O_NONBLOCK; 
	fcntl(fd,F_SETFL,new_option);
	return old_option;
}

//发送http 头部包
void send_http_head(int cfd, int code, char *info,  char *conenttype){
	
	char buf[256];

	int len_ = sprintf(buf,"HTTP/1.1 %d %s\r\n", code, info);
	send(cfd, buf, len_, 0);

	len_ = sprintf(buf,"Content-Type:%s\r\n", conenttype);
	send(cfd, buf, len_, 0);

	// len_ = sprintf(buf,"Content-Length:%d\r\n", length);
	// send(cfd, buf, len_, 0);

	send(cfd, "\r\n", 2, 0);
}

//发送文件数据
void send_file(int cfd, char *path, int *efd, int isclose){
	//0 拷贝技术
	struct stat statbuf;
	int ret = stat(path, &statbuf);
	if(-1 == ret){
		perror("stat");
		return;
	}
	int fd = open(path, O_RDONLY);
	if(-1 == fd){
		perror("open file");
		return;
	}
	ret = sendfile(cfd, fd, NULL, statbuf.st_size);
	if(-1 == ret){
		perror("sendfile");
	}

	close(fd);
	if(isclose){
		char ip[16];
		memset(ip, 0, 16);
		struct sockaddr_in addr;
		socklen_t addrlen;
		ret = getsockname(cfd, (struct sockaddr*)&addr, &addrlen);
		if(-1 == ret){
			perror("getsockname");
		}
		printf("client [%s:%u] exit\n", inet_ntop(AF_INET, &(addr.sin_addr.s_addr), ip, 16), ntohs(addr.sin_port));
	
		//下树关闭连接
		epoll_ctl(*efd, EPOLL_CTL_DEL,cfd, NULL);
		close(cfd);

	}

}

//回复客户端的请求
void send_http_data(int cfd, char *path, int* efd){
	
	//发送数据
	struct stat st;
	int ret = stat(path, &st);

	if(ret<0){
		//文件或目录不存在
		//先发送http头部
		send_http_head(cfd, 404, "Not Found", get_mime_type("error.html"));
		//再发送数据部分
		send_file(cfd, "error.html", efd, 1);

	}else{
		//文件或目录存在

		if(strcmp("./", path)==0||strcmp("./index.html", path)==0){
			//index.html
			stat("index.html", &st);
			//先发送http头部
			send_http_head(cfd, 200, "OK", get_mime_type("./index.html"));
			//再发送数据部分
			send_file(cfd, "index.html", efd, 1);

		}else if(S_ISDIR(st.st_mode)){
			//目录
			//读目录
			DIR *dir = NULL;
			struct dirent *ptr = NULL;
			dir = opendir(path);

			char tmp[1024];
			int siz;
			//发送头部
			send_http_head(cfd, 200, "OK", get_mime_type("*.html"));
			//发送前部分数据
			send_file(cfd, "./test/dir_header.html", efd, 0);
			while(ptr = readdir(dir)){
				if(ptr==NULL)
					break;
				if(ptr->d_type == DT_DIR){
					siz = sprintf(tmp, "<li><a href=%s/>%s</a></li>",ptr->d_name,ptr->d_name);
					
				}else{
					siz = sprintf(tmp, "<li><a href=%s>%s</a></li>",ptr->d_name,ptr->d_name);
				}
				send(cfd, tmp, siz, 0);

			}
			//发送后半部分数据
			send_file(cfd, "./test/dir_tail.html", efd, 1);

			closedir(dir);
				
		}else if(S_ISREG(st.st_mode)){
			//文件
			//先发送http头部
			send_http_head(cfd, 200, "OK", get_mime_type(path));
			//再发送数据部分
			send_file(cfd, path, efd, 1);

		}else{
			printf("request error\n");
		}
		
	}

}

//读取客户端请求
void read_client_request(struct epoll_event *ev, int *efd)
{
	char buf[2048];
	memset(buf, 0, 2048);
	int ret = recv(ev->data.fd, buf, 2048, 0);
	if (-1 == ret){
		//出现错误
		perror("recv");
		return;
	}
	else if (0 == ret)
	{
		//客户端关闭
		char ip[16];
		memset(ip, 0, 16);
		struct sockaddr_in addr;
		socklen_t addrlen;
		ret = getsockname(ev->data.fd, (struct sockaddr*)&addr, &addrlen);
		if(-1 == ret){
			perror("getsockname");
		}
		printf("client [%s:%u] exit\n", inet_ntop(AF_INET, &(addr.sin_addr.s_addr), ip, 16), ntohs(addr.sin_port));

		// 下树
		epoll_ctl(*efd, EPOLL_CTL_DEL, ev->data.fd, NULL);
		close(ev->data.fd);
	}
	else
	{
		//客户端向服务器发送请求
		char method[256]="";
	 	char content[256]="";
	 	char protocol[256]="";
	 	sscanf(buf,"%[^ ] %[^ ] %[^ \r\n]", method, content, protocol);
		
		int i = strlen(content);
		for(i;i>=0;i--){
			content[i+1] = content[i];
		}
		content[0] = '.';
	 	printf("======[%s]  [%s]  [%s]======\n",method,content,protocol );

		if(0 == strcmp(method,"GET")){
			//GET请求
			send_http_data(ev->data.fd, content, efd);
			
		}else{
			//其他请求
		
		}

	}
}

// 使用epoll实现http服务器
int main(int argc, char *args[])
{
	if (argc < 3)
	{
		printf("plase set ip and port!\neg: %s 192.168.23.131 8888\n",args[0]);
		return 1;
	}

	//获取当前路径并设置为工作目录
	
	char* workdir =  getenv("PWD");
	workdir =  strcat(workdir, "/data");

	chdir(workdir);


	// 创建套接字
	int lfd = socket(AF_INET, SOCK_STREAM, 0);
	if (-1 == lfd)
	{
		perror("socket");
		return 1;
	}
	// 绑定 bind
	struct sockaddr_in addr;
	socklen_t t1 = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(args[2]));
	inet_pton(AF_INET, args[1], &addr.sin_addr.s_addr);
	int ret = bind(lfd, (struct sockaddr *)&addr, t1);
	if (-1 == ret)
	{
		printf("bind failed\n");
		close(lfd);
		return 1;
	}
	// 监听
	ret = listen(lfd, 128);
	if(-1 == ret){
		perror("listen");
		close(lfd);
		return 1;
	}
	// 创建树
	int efd = epoll_create(1);
	// 将lfd上树
	struct epoll_event ev;
	ev.events = EPOLLIN;
	ev.data.fd = lfd;
	ret = epoll_ctl(efd, EPOLL_CTL_ADD, lfd, &ev);
	if (-1 == ret){
		perror("epoll add failed\n");
		close(lfd);
		return 1;
	}

	// 循环监听
	struct epoll_event active_events[4096];
	int nready;

	struct sockaddr_in ad;
	socklen_t t2;
	int cfd;
	char ip[16];

	while (1){
		nready = epoll_wait(efd, active_events, 4096, -1);
		if (nready < 0){
			perror("epoll wait failed\n");
			continue;
		}
		else{
			for (int i = 0; i < nready; i++){
				
				if (active_events[i].data.fd == lfd && active_events[i].events & EPOLLIN){
					// 表示有新的连接建立
					// accept
					cfd = accept(lfd, (struct sockaddr *)&ad, &t2);
					if (-1 == cfd)
					{
						perror("accept");
						break;
					}
					
					//设置非阻塞
					setnonblocking(cfd); //!!

					memset(ip, 0, 16);
					printf("new client [%s:%u] login\n", inet_ntop(AF_INET, &ad.sin_addr.s_addr, ip, 16), ntohs(ad.sin_port));
					// 将cfd上树
					struct epoll_event cev;
					cev.events = EPOLLIN | EPOLLET; // 使用边缘触发（cfd应该设置为非阻塞）
					cev.data.fd = cfd;
					epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &cev);
				}
				else if (active_events[i].events & EPOLLIN){
					// 客户端发送数据来了
					read_client_request(&active_events[i], &efd);
				}
			}
		}
	}
	// 收尾

	close(lfd);

	return 0;
}

