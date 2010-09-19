/*
 * Copyright (C) 2005-2010 Kazuyoshi Aizawa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*****************************************************************
 * iumfs_mount.c
 *
 *  gcc iumfs_mount.c -o mount
 *
 * iumfs の為の mount コマンド。
 * /usr/lib/fs/iumfs/ ディレクトリを作り、このディレクトリ内に
 * このプログラムを「mount」として配置すれば、/usr/sbin/mount
 * にファイルシステムタイプとして「iumfs」を指定すると、この
 * プログラムが呼ばれることになる。
 *
 *   Usage: mount -F iumfs [-o options] ftp://host/pathname mount_point
 *     options: [user=username[,pass=password]]
 *
 ******************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/mntent.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/param.h>
#include <strings.h>
#include <string.h>
#include "iumfs.h"

void  print_usage(char *);

int
main(int argc, char *argv[])
{
    char *opts = NULL;
    char *opt  = NULL;
    char *resource = NULL;
    char *mountpoint = NULL;
    char *server_and_path = NULL;
    int  c;
    int   verbose = 0;
    iumfs_mount_opts_t mountopts[1];
    size_t n;

    memset(mountopts, 0x0, sizeof(iumfs_mount_opts_t));
    
    if (argc < 3 || argc > 5)
        print_usage(argv[0]);

    while ((c = getopt(argc, argv, "o:")) != EOF){
        switch (c) {
            case 'o':
                // マウントオプション
                opts = optarg;
                break;
            default:
                print_usage(argv[0]);
        }
    }

    if ((argc - optind) != 2)
        print_usage(argv[0]);    

    resource =  argv[optind++];
    mountpoint = argv[optind++];

    /*
     * -o で指定されたオプションを解釈する。
     * サポートしているのはは以下の2つのオプションだけ。
     *     user=<user name>
     *     pass=<password>
     *
     *     例） -o user=root,pass=hoge
     */
    if(opts){
        char *arg;

        arg = opts;
        while((opt = strtok(arg, ",")) != NULL){

            if(!strncmp(opt, "user=", 5))
                strcpy(mountopts->user,&opt[5]);
            else if (!strncmp(opt, "pass=", 5))
                strcpy(mountopts->pass, &opt[5]);
            else if (!strncmp(opt, "verbose", 7))
                verbose = 1;
            else {
                printf("Unknown option %s\n", opt);
                print_usage(argv[0]);
            }
            
            arg = NULL;
        }
    }

    /*
     * mount コマンドの -o オプションとしてユーザ名とパスワードが指定され
     * なかったら、デフォルトでそれぞれ ftp をセットする。
     */
    if(strlen(mountopts->user) == 0)
        strcpy(mountopts->user,"ftp");
    if(strlen(mountopts->pass) == 0)
        strcpy(mountopts->pass, "ftp");


    /*
     * mount コマンドに渡されたリソース部分から ftp サーバ名と、マウントする
     * ベースディレクトリを解釈する。
     */
    if (strncmp(resource, "ftp://", 6)){
        printf("Invalid URL\n");
        print_usage(argv[0]);
    }
    server_and_path = &resource[6];
    if(strstr(server_and_path, "/") == NULL){
        printf("No pathname specified\n");
        print_usage(argv[0]);
    }
    n = strcspn(server_and_path, "/");
    strncpy(mountopts->server, server_and_path, n);
    strcpy(mountopts->basepath, &server_and_path[n]);

    if(strlen(mountopts->basepath)==0)
        strcpy(mountopts->basepath, "/");
    
    if(verbose){
        printf("user = %s\n", mountopts->user);
        printf("pass = %s\n", mountopts->pass);    
        printf("resoruce = %s\n",resource);
        printf("mountpint = %s\n", mountpoint);
        printf("server = %s\n", mountopts->server);
        printf("basepath = %s\n", mountopts->basepath);        
    }

    if ( mount(resource, mountpoint, MS_DATA|MS_RDONLY, "iumfs", mountopts, sizeof(mountopts)) < 0 ){
	perror("mount");
        exit(0);
    }

    return(0);    
}

/*****************************************************************************
 * print_usage()
 *
 * Usage を表示し、終了する。
 *****************************************************************************/
void
print_usage(char *argv)
{
    printf("Usage: %s -F iumfs [-o options] ftp://host/pathname mount_point\n", argv);
    printf("\toptions: [user=username[,pass=password]]\n");
    exit(0);
}
