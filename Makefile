http_epoll:
	gcc epoll_http_server.c -o http_epoll
clean:
	rm -rf http_epoll