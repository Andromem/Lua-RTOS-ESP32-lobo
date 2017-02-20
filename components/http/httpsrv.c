/*******************************************************************************
 * Copyright (c) 2015, http://www.jbox.dk/
 * All rights reserved. Released under the BSD license.
 * httpsrv.c 1.0 01/01/2016 (Simple Http Server)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/
#include "luartos.h"

#if LUA_USE_HTTP

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

#include <pthread/pthread.h>

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <sys/dirent.h>
#include <sys/syslog.h>
#include <sys/panic.h>
#include <unistd.h>
#include <sys/mount.h>

#define PORT           80
#define SERVER         "lua-rtos-http-server/1.0"
#define PROTOCOL       "HTTP/1.1"
#define RFC1123FMT     "%a, %d %b %Y %H:%M:%S GMT"
#define HTPP_BUFF_SIZE 1024

char *get_mime_type(char *name) {

    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".htm")  == 0) return "text/html";
    if (strcmp(ext, ".txt")  == 0) return "text/html";
    if (strcmp(ext, ".jpg")  == 0) return "image/jpeg";
    if (strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif")  == 0) return "image/gif";
    if (strcmp(ext, ".png")  == 0) return "image/png";
    if (strcmp(ext, ".css")  == 0) return "text/css";
    if (strcmp(ext, ".au")   == 0) return "audio/basic";
    if (strcmp(ext, ".wav")  == 0) return "audio/wav";
    if (strcmp(ext, ".avi")  == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mpg")  == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3")  == 0) return "audio/mpeg";
    if (strcmp(ext, ".svg")  == 0) return "image/svg+xml";
    if (strcmp(ext, ".pdf")  == 0) return "application/pdf";
    return NULL;
}

void send_headers(FILE *f, int status, char *title, char *extra, char *mime, int length) {
    fprintf(f, "%s %d %s\r\n", PROTOCOL, status, title);
    fprintf(f, "Server: %s\r\n", SERVER);
    if (extra) fprintf(f, "%s\r\n", extra);
    if (mime) fprintf(f, "Content-Type: %s\r\n", mime);

    if (length >= 0) {
    	fprintf(f, "Content-Length: %d\r\n", length);
    } else {
    	fprintf(f, "Transfer-Encoding: chunked\r\n");
    }

    fprintf(f, "Connection: close\r\n");

    fprintf(f, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
    fprintf(f, "no-cache\r\n");
    fprintf(f, "0\r\n");

	fprintf(f, "\r\n");
}

#define HTTP_STATUS_LEN     3
#define HTTP_ERROR_LINE_1   "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n"
#define HTTP_ERROR_LINE_2   "<BODY><H4>%d %s</H4>\r\n"
#define HTTP_ERROR_LINE_3   "%s\r\n"
#define HTTP_ERROR_LINE_4   "</BODY></HTML>\r\n"
#define HTTP_ERROR_VARS_LEN (2 * 5)

void send_error(FILE *f, int status, char *title, char *extra, char *text) {
	int len = strlen(title) * 2 +
			  HTTP_STATUS_LEN * 2 +
			  strlen(text) +
			  strlen(HTTP_ERROR_LINE_1) +
			  strlen(HTTP_ERROR_LINE_2) +
			  strlen(HTTP_ERROR_LINE_3) +
			  strlen(HTTP_ERROR_LINE_4) -
			  HTTP_ERROR_VARS_LEN;

    send_headers(f, status, title, extra, "text/html", len);
    fprintf(f, HTTP_ERROR_LINE_1, status, title);
    fprintf(f, HTTP_ERROR_LINE_2, status, title);
    fprintf(f, HTTP_ERROR_LINE_3, text);
    fprintf(f, HTTP_ERROR_LINE_4);
}

void send_file(FILE *f, char *path, struct stat *statbuf) {
	//printf("HTTP: send file [%s]\r\n", path);
    int n, buflen;
    char *data; //[HTPP_BUFF_SIZE];

	FILE *file = fopen(path, "r");

    if (!file) {
        send_error(f, 403, "Forbidden", NULL, "Access denied.");
    } else {
        int length = S_ISREG(statbuf->st_mode) ? statbuf->st_size : -1;
        send_headers(f, 200, "OK", NULL, get_mime_type(path), length);

        buflen = 8192;
        if (length < 8192) buflen = length;
        data = (char *)malloc(8192);
        if (data) {
        	while ((n = fread(data, 1, buflen, file)) > 0) fwrite(data, 1, n, f);
    		free(data);
        }
        else {
            send_error(f, 403, "Error", NULL, "File read.");
        }
        fclose(file);
    }
}


static void chunk(FILE *f, const char *fmt, ...) {
	char *buffer;
	va_list args;

	buffer = (char *)malloc(2048);
	if (buffer) {
		*buffer = '\0';

		va_start(args, fmt);

		vsnprintf(buffer, 2048, fmt, args);

		//printf("CHUNK: [%s]\r\n", buffer);
		fprintf(f, "%x\r\n", strlen(buffer));
		fprintf(f, "%s\r\n", buffer);

		va_end(args);

		free(buffer);
	}
}


int process(FILE *f) {
    char buf[HTPP_BUFF_SIZE];
    char *method;
    char *httppath;
    char *path;
    char *protocol;
    struct stat statbuf;
    char pathbuf[HTPP_BUFF_SIZE];
    //int len;

    if (!fgets(buf, sizeof (buf), f)) return -1;

    method = strtok(buf, " ");
    httppath = strtok(NULL, " ");
    protocol = strtok(NULL, "\r");

    if (!method || !httppath || !protocol) {
    	syslog(LOG_DEBUG, "HTTP: Error\r");
    	return -1;
    }

    path = mount_resolve_to_logical(httppath);

    syslog(LOG_DEBUG, "http: %s %s %s\r", method, path, protocol);

    fseek(f, 0, SEEK_CUR); // Force change of stream direction

    if (strcasecmp(method, "GET") != 0) {
        send_error(f, 501, "Not supported", NULL, "Method is not supported.");
        return -1;
    }

    DIR *dir;
	struct dirent *de;
	dir = opendir(path);
	if (!dir) {
		if (stat(path, &statbuf) == 0) {
			send_file(f, path, &statbuf);
		}
		send_error(f, 404, "Not Found", NULL, "File not found.");
		syslog(LOG_DEBUG, "http: %s Not found\r", path);
		return -1;
	}
	snprintf(pathbuf, sizeof (pathbuf), "%s/index.html", path);
	if (stat(pathbuf, &statbuf) >= 0) {
		closedir(dir);
		send_file(f, pathbuf, &statbuf);
		return 0;
	}

	send_headers(f, 200, "OK", NULL, "text/html", -1);
	chunk(f, "<!DOCTYPE html><HTML><HEAD><TITLE>Index of %s</TITLE></HEAD><BODY background=\"/sd/www/bg1.png\">", path);
	chunk(f, "<H4>Index of %s<hr></H4>", path);

	//chunk(f, "<TABLE>");
	chunk(f, "<TABLE style=\"border-collapse:collapse;\" cellpadding=\"4\" bordercolor=\"888888\" background=\"/sd/www/bg3.jpg\" border=\"1\">");
	chunk(f, "<TR>");
	chunk(f, "<TH style=\"width: 250;text-align: left;\" background=\"/sd/www/bg4.jpg\">Name</TH>");
	chunk(f, "<TH style=\"width: 100px;text-align: right;\" background=\"/sd/www/bg4.jpg\">Size</TH>");
	chunk(f, "<TH style=\"width: 200px;text-align: right;\" background=\"/sd/www/bg4.jpg\">Time</TH>");
	chunk(f, "</TR>");

	chunk(f, "<TR>");
	chunk(f, "<TD><A HREF=\"..\">..</A></TD><TD></TD> <TD> </TD>");
	chunk(f, "</TR>");

	char tbuffer[80];
	struct tm *tm_info;
	int statok = -1;

	while ((de = readdir(dir)) != NULL) {
		strcpy(pathbuf, path);
		if (strcmp(path, "/") != 0) strcat(pathbuf,"/");
		strcat(pathbuf, de->d_name);
		tbuffer[0] = '\0';
		//printf("HTTP: dir [%s] [%s]\r\n", path, pathbuf);

		statok = stat(pathbuf, &statbuf);
		if ( statok == 0) {
			tm_info = localtime(&statbuf.st_mtime);
			strftime(tbuffer, 80, "%d/%m/%Y %R", tm_info);
		}
		else sprintf(tbuffer, "  ");

		chunk(f, "<TR>");

		// File name
		chunk(f, "<TD>");
		chunk(f, "<A HREF=\"%s%s\">", de->d_name, (de->d_type != DT_REG) ? "/" : "");
		chunk(f, "%s%s", de->d_name, (de->d_type != DT_REG) ? "/</A>" : "</A> ");
		chunk(f, "</TD>");
		// File size
		chunk(f, "<TD style=\"text-align: right;\">");
		if (de->d_type == DT_REG) {
			if ( statok == 0) chunk(f, "%d", (int)statbuf.st_size);
			else chunk(f, "?");
		}
		else {
			chunk(f, "DIR");
		}
		chunk(f, "</TD>");
		// File time
		chunk(f, "<TD style=\"text-align: right;\">");
		chunk(f, "%s", tbuffer);
		chunk(f, "</TD>");

		chunk(f, "</TR>");
	}
	closedir(dir);


	chunk(f, "</TABLE>");

	chunk(f, "</BODY></HTML>", SERVER);

	fprintf(f, "0\r\n\r\n");

    return 0;
}

static void *http_thread(void *arg) {
    struct sockaddr_in sin;
	int server;

	server = socket(AF_INET, SOCK_STREAM, 0);


    sin.sin_family      = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port        = htons(PORT);
    bind(server, (struct sockaddr *) &sin, sizeof (sin));

    listen(server, 5);
    syslog(LOG_INFO, "http: server listening on port %d\n", PORT);

    while (1) {
    	int client;

    	// Wait for a request ...
        if ((client = accept(server, NULL, NULL)) != -1) {
        	// We waiting for send all data before close socket's stream
        	struct linger so_linger;

        	so_linger.l_onoff  = 1;
        	so_linger.l_linger = 10;

            setsockopt(client, SOL_SOCKET, SO_LINGER, &so_linger, sizeof(so_linger));

            // Set a timeout for send / receive
            struct timeval tout;

            tout.tv_sec = 10;
            tout.tv_usec = 0;

            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tout, sizeof(tout));
            setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tout, sizeof(tout));

            // Create the socket stream
            FILE *stream = fdopen(client, "a+");
            process(stream);
            fclose(stream);
        }
    }

    close(server);

    pthread_exit(NULL);
}

void http_start() {
	pthread_attr_t attr;
	struct sched_param sched;
	pthread_t thread;
	int res;

	// Init thread attributes
	pthread_attr_init(&attr);

	// Set stack size
    pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN); //LUA_TASK_STACK);

    // Set priority
    sched.sched_priority = LUA_TASK_PRIORITY;
    pthread_attr_setschedparam(&attr, &sched);

    // Set CPU
    cpu_set_t cpu_set = LUA_TASK_CPU;
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpu_set);

    // Create thread
    res = pthread_create(&thread, &attr, http_thread, NULL);
    if (res) {
		panic("Cannot start http_thread");
	}
}

void http_stop() {

}

#endif
