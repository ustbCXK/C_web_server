/*
 * @Descripttion: file content
 * @version: 
 * @Author: congsir
 * @Date: 2021-05-24 16:09:12
 * @LastEditors: Do not edit
 * @LastEditTime: 2021-05-25 21:47:40
 */
#ifndef _EPOLL_SERVER_H
#define _EPOLL_SERVER_H


void  epoll_run(int port);
int init_listen_fd( int port, int epfd );
void do_accept(int lfd, int epfd);
void do_read(int cfd, int epfd );
int get_line(int sock, char *buf, int size);
void disconnect( int cfd, int epfd );
void http_request(const char* request, int cfd);
void send_dir( int cfd, const char* dirname);
void send_respond_head( int cfd, int no, const char* desp, const char* type, long len );
void send_file( int cfd, const char* filename );

#endif
