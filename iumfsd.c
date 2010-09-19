/*
 * Copyright (C) 2010 Kazuyoshi Aizawa. All rights reserved.
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
/**********************************************************
 * iumfsd.c
 *
 * FTP を使った擬似ファイルシステムのデーモン
 *
 *  LD_PRELOAD=libumem.so.1 UMEM_LOGGING=transaction UMEM_DEBUG=default /usr/local/bin/iumfsd -d 1
 *
 *********************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/varargs.h>
#include <syslog.h>
#include <libgen.h>
#include <netdb.h>
#include <strings.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/vnode.h>
#include "iumfs.h"

#define FTP       21
#define FTPDATA   20
#define ERR_MSG_MAX    300       // syslog に出力する最長文字数
#define FTP_CMD_MAX    200       // FTP コマンドの最長文字数
#define FTP_RES_MAX    2000       // FTP レスポンスの最長文字数
#define SELECT_CMD_TIMEOUT    10 // FTP コマンド発行時のタイムアウト
#define RETRY_SLEEP_SEC       1  // リトライまでの待ち時間
#define RETRY_MAX             1  // リトライ回数
#define FS_BLOCK_SIZE         512 // このファイルシステムのブロックサイズ


#define CMD_NULL  0
#define CMD_USER  1 
#define CMD_PASS  2 
#define CMD_ACCT  3 
#define CMD_CWD   4 
#define CMD_CDUP  5 
#define CMD_SMNT  6 
#define CMD_QUIT  7 
#define CMD_REIN  8 
#define CMD_PORT  9 
#define CMD_PASV  10
#define CMD_TYPE  11
#define CMD_STRU  12
#define CMD_MODE  13
#define CMD_RETR  14
#define CMD_STOR  15
#define CMD_STOU  16
#define CMD_APPE  17
#define CMD_ALLO  18
#define CMD_REST  19
#define CMD_RNFR  20
#define CMD_RNTO  21
#define CMD_ABOR  22
#define CMD_DELE  23
#define CMD_RMD   24
#define CMD_MKD   25
#define CMD_PWD   26
#define CMD_LIST  27
#define CMD_NLST  28
#define CMD_SITE  29
#define CMD_SYST  30
#define CMD_STAT  31
#define CMD_HELP  32
#define CMD_NOOP  33
#define CMD_SIZE  34

char *cmds[] = {
    "NULL",
    "USER",
    "PASS",
    "ACCT",
    "CWD", 
    "CDUP",
    "SMNT",
    "QUIT",
    "REIN",
    "PORT",
    "PASV",
    "TYPE",
    "STRU",
    "MODE",
    "RETR",
    "STOR",
    "STOU",
    "APPE",
    "ALLO",
    "REST",
    "RNFR",
    "RNTO",
    "ABOR",
    "DELE",
    "RMD",
    "MKD",
    "PWD",
    "LIST",
    "NLST",
    "SITE",
    "SYST",
    "STAT",
    "HELP",
    "NOOP",
    "SIZE",    
};


/*
 * FTP セッションの管理構造体
 */
typedef struct ftpcntl
{
    int cntlfd;       // 制御セッションの socket
    int datafd;       // データセッションの socket
    int devfd;
    int statusflag;   // ステータスフラグ
    char *server;     // FTP サーバ名
    char *loginname;  // ログイン名
    char *loginpass;  // ログインパスワード
    int  dataport;    // データ転送用のポート番号
    char *basepath;   // クライアントが要求しているベースのパス名
} ftpcntl_t;

/*
 * ステータスフラグ
 */
#define     CNTL_OPEN        0x01  // 制御セッションがオープンしている
#define     LOGGED_IN        0x02  // ログイン完了済み
#define     DATA_OPEN        0x04  // データセッションがオープンしている
#define     CNTL_ERR         0x08  // 制御セッションが回復不能なエラー状態
#define     DATA_ERR         0x10  // データセッションが回復不可能なエラー状態

#define DEVPATH "/devices/pseudo/iumfs@0:iumfscntl"

int     become_daemon();
void    print_usage(char *);
void    print_err(int , char *, ...);
int     open_cntl(ftpcntl_t * const);
void    close_cntl(ftpcntl_t * const);
int     send_cmd(ftpcntl_t * const, int, char *);
int     recv_res(ftpcntl_t * const, int, char * , size_t);
int     open_socket(char *, int);
int     read_socket(int , char *, size_t );
int     write_socket(int , void *, size_t, int);
void    close_data(ftpcntl_t * const);
int     open_data(ftpcntl_t * const);
int     read_file_block(ftpcntl_t * const, char *, caddr_t, off_t, size_t);
int     read_socket_bytes(int , caddr_t , size_t );
int     enter_passive(ftpcntl_t * const);
int     check_offset(ftpcntl_t * const, off_t);
int     read_directory_entries(ftpcntl_t * const, char *, caddr_t, off_t, size_t);
int     process_readdir_request(ftpcntl_t * const, char *, caddr_t, off_t , size_t );
int     process_read_request(ftpcntl_t * const, char *, caddr_t, off_t , size_t );
int     process_getattr_request(ftpcntl_t * const, char *, caddr_t);
int     get_file_attributes(ftpcntl_t * const, char *, caddr_t, size_t );
int     month_to_int(char *);
int     parse_attributes(vattr_t *, char *);
void    hoge(ftpcntl_t * const);

int debuglevel = 0; // とりあえず デフォルトのデバッグレベルを 1 にする
int use_syslog = 0; // メッセージを STDERR でなく、syslog に出力する

#define DEBUG

#ifdef DEBUG
#define PRINT_ERR(args) \
             if (debuglevel > 0){\
                 print_err args;\
             }    
#else
#define PRINT_ERR
#endif


ftpcntl_t     *gftpp; 

int
main(int argc, char *argv[])
{
    ftpcntl_t     *ftpp; 
    int           c;
    char          pathname[MAXPATHLEN]; // ファイルパス
    size_t        size;       // 読み込みサイズ
    off_t      offset;     // ファイルのオフセット
    caddr_t       mapaddr;
    request_t     req[1];
    int           inprogress = 0;    // iumfscntl からのリクエストを処理中か？
    int           result;
    static fd_set fds, err_fds;
    struct timeval timeout;
    char response[FTP_RES_MAX] = {0}; // サーバからのレスポンスを書き込むバッファ    

    ftpp = gftpp = (ftpcntl_t *) malloc(sizeof(ftpcntl_t));

    memset(req, 0x0, sizeof(request_t));
    memset(ftpp, 0x0, sizeof(ftpcntl_t));

    while ((c = getopt(argc, argv, "d:")) != EOF){
        switch (c) {
            case 'd':
                //デバッグレベル
                debuglevel = atoi(optarg);
                break;
            default:
                print_usage(argv[0]);
                break;
        }
    }

    ftpp->devfd = open(DEVPATH, O_RDWR, 0666);
    if ( ftpp->devfd < 0){
        perror("open");
        goto error;
    }
    
    PRINT_ERR((LOG_INFO, "main: successfully opened iumfscntl device\n"));    

    mapaddr = (caddr_t)mmap(0, MMAPSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, ftpp->devfd, 0);
    if (mapaddr == MAP_FAILED) {
        perror("mmap:");
        goto error;
    }
    PRINT_ERR((LOG_INFO, "main: mmap succeeded\n"));    

    /* syslog のための設定。Facility は　LOG_USER とする */
    openlog(basename(argv[0]),LOG_PID,LOG_USER);

    sigignore(SIGPIPE);

    /*
     * ここまではとりあえず、フォアグラウンドで実行。
     * ここからは、デバッグレベル 0 （デフォルト）なら、バックグラウンド
     * で実行し、そうでなければフォアグラウンド続行。
     */
    if(debuglevel == 0){
        PRINT_ERR((LOG_INFO,"Going to background mode\n"));
        if(become_daemon() != 0){
            print_err(LOG_ERR,"can't become daemon\n");
            goto error;
        }
    }
    
    FD_ZERO(&fds);
    FD_ZERO(&err_fds);    

    do {
        size_t ret;


        if (inprogress){
            /*
             * 以前のリクエストの継続処理中
             */
            FD_ZERO(&err_fds);            
            FD_SET(ftpp->devfd, &err_fds);
            timeout.tv_sec = 1;
            timeout.tv_usec = 0;
            /*
             * iumfscntl デバイスからキャンセルがきていないかどうかを
             * 確認し、もしきていなければ新規リクエストを読まずに、
             * 処理を進める。
             */
            ret = select(FD_SETSIZE, NULL, NULL, &err_fds, &timeout);
            if (ret){
                PRINT_ERR((LOG_INFO, "main: request canceled\n"));                
                inprogress = 0;
                continue;
            }
            PRINT_ERR((LOG_INFO, "main: request not canceled, continue to process request\n"));
        } else {
            /*
             * iumfscntl デバイスの FD を監視し、新規リクエストを待つ。
             * もしコントロールセッションの socket が開いているなら、
             * サーバからのコントロールセッションの切断を識別するために、
             * 同時に select 待ちをする。
             */
            FD_ZERO(&fds);            
            if(ftpp->statusflag & CNTL_OPEN)
                FD_SET(ftpp->cntlfd, &fds);
            FD_SET(ftpp->devfd, &fds);

            ret = select(FD_SETSIZE, &fds, NULL, &err_fds, NULL);
            if( ret < 0){
                print_err(LOG_ERR,"main: select: %s\n", strerror(errno));
                goto error;
            }

            /*
             * FTP サーバからデータの受信があった。
             * ここで FTP サーバからデータを受信するのは以下の場合
             * 
             *  1. サーバがコントロールセッションをタイムアウトクローズした
             *  2. サーバから予想外のレスポンスが返ってきた
             *    
             * 本プログラムはコマンドに対する全てのレスポンスを正しくハンドル
             * できていないため、ここにきてしまう可能性がある。
             */
            if((ftpp->statusflag & CNTL_OPEN) && FD_ISSET(ftpp->cntlfd, &fds)){
                if(recv_res(ftpp, CMD_NULL, response, sizeof(response)) < 0){
                    close_cntl(ftpp);
                }
                continue;
            }

            /*
             * ここにくるのは iumfscntl デバイスが READ 可能な状態の時だけ。
             */
            ret = read(ftpp->devfd, req, sizeof(request_t));
            if (ret != sizeof(request_t)){
                print_err(LOG_ERR,"main: read size invalid ret(%d) != sizeof(request_t)(%d)\n",
                          ret, sizeof(request_t));
                sleep(1);
                continue;
            }
            inprogress = 1;

            PRINT_ERR((LOG_INFO, "==============================================\n"));
            PRINT_ERR((LOG_INFO, "main: read(%d) returned (%d)\n",ftpp->devfd, ret));

            if(req->mountopts->basepath == NULL)
                PRINT_ERR((LOG_ERR, "main: req->mountopts->basepath is NULL"));

            if(req->pathname == NULL)
                PRINT_ERR((LOG_ERR, "main: req->pathname is NULL"));                    

            /*
             * サーバ上の実際のパス名を得る。
             * もしベースパスがルートだったら、余計な「/」はつけない。
             */
            if(ISROOT(req->mountopts->basepath))
                snprintf(pathname, MAXPATHLEN, "%s", req->pathname);
            else
                snprintf(pathname, MAXPATHLEN, "%s%s", req->mountopts->basepath, req->pathname);
            
            PRINT_ERR((LOG_INFO, "main: user=%s, pass=%s\n",
                       req->mountopts->user,req->mountopts->pass));
            PRINT_ERR((LOG_INFO, "main: server=%s, basepath=%s\n",
                       req->mountopts->server, req->mountopts->basepath));
            PRINT_ERR((LOG_INFO, "main: pathname=%s\n",req->pathname));

	    /*
	     * もしコントロールセッションがオープンされていて、今回の要求が別のサーバへ
	     * のものだったら一度コントロールセッションをクローズする。
             */
	    if((ftpp->statusflag & CNTL_OPEN) && ftpp->server != NULL && ftpp->basepath != NULL){
		if(strcmp(ftpp->server, req->mountopts->server)
                   && strcmp(ftpp->basepath, req->mountopts->basepath)){
                    PRINT_ERR((LOG_INFO, "main: Server changed. close existing control session.\n"));
                    close_cntl(ftpp);
                }
	    }
        }

        /*
         * ftp のコントロールセッションがオープンしていなければ今オープン
         */
        if(!(ftpp->statusflag & CNTL_OPEN)){
            ftpp->loginname = req->mountopts->user;
            ftpp->loginpass = req->mountopts->pass;
            ftpp->server    = req->mountopts->server;
            ftpp->basepath  = req->mountopts->basepath;
            if(open_cntl(ftpp) < 0){
                print_err(LOG_ERR,"main: can't open ftp session\n");
                continue;
            }
            PRINT_ERR((LOG_INFO, "main: ftp session to \"%s\" established.\n", ftpp->server));            
        }
        
        PRINT_ERR((LOG_INFO, "main: server=%s, basepath=%s\n",
                       req->mountopts->server, req->mountopts->basepath));

        switch(req->request_type){
            case READ_REQUEST:
                PRINT_ERR((LOG_INFO, "------> READ_REQUEST\n"));                
                offset = req->data.read_request.offset;
                size = req->data.read_request.size;
                PRINT_ERR((LOG_INFO, "main: pathname = %s\n",pathname));                                
                PRINT_ERR((LOG_INFO, "main: offset = %d, size = %d \n",offset, size));
                if(process_read_request(ftpp, pathname, mapaddr, offset, size) == 0)
                    inprogress = 0;                    
                PRINT_ERR((LOG_INFO, "<------ READ_REQUEST\n"));                                
                break;
            case READDIR_REQUEST:
                PRINT_ERR((LOG_INFO, "------> READDIR_REQUEST\n"));
                offset = req->data.readdir_request.offset;
                size = req->data.readdir_request.size;                
                PRINT_ERR((LOG_INFO, "main: pathname = %s\n",pathname));
                PRINT_ERR((LOG_INFO, "main: offset = %d, size = %d \n",offset, size));                
                if(process_readdir_request(ftpp, pathname, mapaddr, offset, size) == 0)
                    inprogress = 0;
                PRINT_ERR((LOG_INFO, "<------ READDIR_REQUEST\n"));                
                break;
            case GETATTR_REQUEST:
                PRINT_ERR((LOG_INFO, "------> GETATTR_REQUEST\n"));                
                PRINT_ERR((LOG_INFO, "main: pathname = %s\n",pathname));
                if(process_getattr_request(ftpp, pathname, mapaddr) == 0)
                    inprogress = 0;
                PRINT_ERR((LOG_INFO, "<------ GETATTR_REQUEST\n")); 
                break;                
            default:
                result = ENOSYS;
                PRINT_ERR((LOG_ERR, "main: Unknown request type 0x%x\n", req->request_type));
                write(ftpp->devfd, &result, sizeof(int));
                inprogress = 0;                
                break;
        }
        /*
         * inprogress がまだ立っていた場合、リクエストが何らかの問題で
         * 失敗したことを示している。再トライ。
         */
        if(inprogress){
            PRINT_ERR((LOG_INFO, "main: request failed. try again...\n"));
        } else {
            PRINT_ERR((LOG_INFO, "main: request completed\n"));
        }
    } while (1);
    
  error:
    exit(0);
}

/*****************************************************************************
 * print_usage()
 *
 * Usage を表示し、終了する。
 *****************************************************************************/
void
print_usage(char *argv)
{
    printf ("Usage: %s [-d level]\n",argv);
    printf ("\t-d level    : Debug level[0-1]\n");
    exit(0);
}

/*****************************************************************************
 * become_daemon()
 *
 * 標準入出力、標準エラー出力をクローズし、バックグラウンドに移行する。
 *****************************************************************************/
int
become_daemon()
{
    chdir("/");
    umask(0);
    signal(SIGHUP,SIG_IGN);

    if( fork() == 0){
        use_syslog = 1;
        close (0);
        close (1);
        close (2);
        /* 新セッションの開始 */
        if (setsid() < 0)
            return(-1);
    } else {
        exit(0);
    }
    return(0);
}

/***********************************************************
 * print_err
 *
 * エラーメッセージを表示するルーチン。
 *
 * debuglevel 0 で LOG_WARRNING 以上のメッセージを出力（デフォルト）
 * debuglevel 1 で LOG_NOTICE 以上のメッセージを出力 
 * debuglevel 2 で LOG_INFO 以上のメッセージを出力 
 * debuglevel 3 で LOG_DEBUG 以上のメッセージを出力
 *
 * この print_err() のラッパーになっている PRINT_ERR マクロは
 * DEBUG フラグが define されている場合にだけ有効になる。
 *
 ***********************************************************/
void
print_err(int level, char *format, ...)
{
    va_list ap;
    char buf[ERR_MSG_MAX];

    if( level > debuglevel + 4  )
        return;
    
    va_start(ap, format);
    vsnprintf(buf,ERR_MSG_MAX, format, ap);
    va_end(ap);

    if(use_syslog)
        syslog(level, buf);
    else
        fprintf(stderr, buf);
}

/*****************************************************************************
 * open_socket()
 *
 * FTP サーバの指定されたポートに対して TCP connection を確立し、socket 番号
 * を返す。
 *
 *  引数：
 *           server : 接続に行くサーバ名
 *           port   : 接続に行くポート番号

 * 戻り値：
 *         成功時 :  ソケット番号
 *         失敗時 :  -1
 *****************************************************************************/
int
open_socket(char *server, int port)
{
    static struct  sockaddr_in sin;
    static struct  hostent     *hp;
    int     sock;

    PRINT_ERR((LOG_DEBUG, "open_socket: called\n"));
    
    if( server == NULL){
        print_err(LOG_ERR,"open_socket: server name not specified.\n");
        return(-1);
    }

    if(( hp = gethostbyname(server)) == NULL) {
        print_err(LOG_ERR,"hostname %s not found.\n",server);
        goto error;
    }
    /*
     * sockaddr_in の sin_port にポート番号をセット
     */
    sin.sin_port = htons((short)port);

    memcpy((char *)&sin.sin_addr,hp->h_addr,hp->h_length);
    sin.sin_family = AF_INET;

    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        print_err(LOG_ERR, "open_socket: socket:%s\n", strerror(errno));
        goto error;
    }

    if(connect(sock,(struct sockaddr *)&sin, sizeof sin) < 0) {
        print_err(LOG_ERR, "open_socket: connect: %s\n", strerror(errno));
        goto error;
    }

    /*
     * recv() でブロックされるのを防ぐため、non-blocking mode に設定
     */
    if( fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
        print_err(LOG_ERR, "open_socket: Failed to set nonblock: %s\n",sock, strerror(errno));
        goto error;
    }

    PRINT_ERR((LOG_DEBUG, "open_socket: Successfully connected with %s\n", server));

    PRINT_ERR((LOG_DEBUG, "open_socket: returned (%d)\n", sock));        
    return(sock);


  error:
    PRINT_ERR((LOG_DEBUG, "open_socket: returned (-1)\n"));    
    return(-1);
}

/*****************************************************************************
 * open_cntl()
 *
 * FTP コントロールセッションをオープンし、ログインする。
 * 実際の socket のオープン処理はは open_socket() が行う。
 *
 *  引数：
 *           ftpp : FTP セッションの管理構造体
 *
 * 戻り値：
 *         成功時 :  ソケット番号
 *         失敗時 :  -1
 *****************************************************************************/
int
open_cntl(ftpcntl_t * const ftpp)
{
    int retry = RETRY_MAX; // 制御セッションの接続、およびログイン試行回数
    char response[FTP_RES_MAX] = {0}; // サーバからのレスポンスを書き込むバッファ

    PRINT_ERR((LOG_DEBUG, "open_cntl: called\n"));

    do {
        if ((ftpp->cntlfd = open_socket(ftpp->server, FTP)) < 0)
            continue;

        // 制御セッション接続完了。フラグをセット
        ftpp->statusflag |= CNTL_OPEN;

        if(recv_res(ftpp, CMD_NULL, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            continue;
        }

        // USER コマンド発行
        if(send_cmd(ftpp, CMD_USER, ftpp->loginname) < 0){
            close_cntl(ftpp);
            continue;
        }
    
        if(recv_res(ftpp, CMD_USER, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            continue;
        }

        // PASS コマンド発行        
        if(send_cmd(ftpp, CMD_PASS, ftpp->loginpass) < 0){
            close_cntl(ftpp);
            continue;
        }

        if(recv_res(ftpp, CMD_PASS, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            continue;
        }

        //  BINARY モードに移行
        if(send_cmd(ftpp, CMD_TYPE, "I") < 0){
            close_cntl(ftpp);
            continue;
        }

        if(recv_res(ftpp, CMD_TYPE, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            continue;
        }

        // ログイン接続完了。フラグをセット
        ftpp->statusflag |= LOGGED_IN;

        break;
    } while (retry--);

    if (retry <= 0){
        PRINT_ERR((LOG_DEBUG, "open_cntl: returned (-1)\n"));        
        return(-1);
    }else{
        PRINT_ERR((LOG_DEBUG, "open_cntl: returned (%d)\n", ftpp->cntlfd));        
        return(ftpp->cntlfd);
    }
}

/*****************************************************************************
 * close_socket()
 *
 * 引数で渡された socket をクローズする
 *
 *  引数：
 *           fd : クローズする socket 
 *
 * 戻り値：
 *           無し
 *****************************************************************************/
void
close_socket(int fd)
{
    PRINT_ERR((LOG_DEBUG, "close_socket: called for fd# %d\n",fd));        
    shutdown(fd, 2);
    close(fd);
    PRINT_ERR((LOG_DEBUG, "close_socket: returned\n"));        
}

/*****************************************************************************
 * close_cntl()
 *
 * FTP コントロールセッションをクローズする。
 * 実際の socket のクローズ処理は close_socket() が行う。
 *
 *  引数：
 *           ftpp : FTP セッションの管理構造体
 *
 * 戻り値：
 *           無し
 *****************************************************************************/
void
close_cntl(ftpcntl_t * const ftpp)
{
    char response[FTP_RES_MAX] = {0};

    PRINT_ERR((LOG_DEBUG, "close_cntl: called\n"));
    
    if(ftpp->statusflag & CNTL_ERR){
        /*
         * 制御セッションが異常
         */ 
	ftpp->statusflag ^= CNTL_ERR;
    } else if(ftpp->statusflag & CNTL_OPEN ){
        /*
         * 制御セッションは正常。QUIT コマンドを送る
         */ 
        send_cmd(ftpp, CMD_QUIT, NULL);
        recv_res(ftpp, CMD_QUIT, response, sizeof(response));
    } 

    if(ftpp->statusflag & CNTL_OPEN)
	ftpp->statusflag ^= CNTL_OPEN;
    if(ftpp->statusflag & LOGGED_IN)
	ftpp->statusflag ^= LOGGED_IN;
    close_socket(ftpp->cntlfd);
    ftpp->cntlfd = -1;

    /*
     * データセッションもクローズする。
     */
    if(ftpp->statusflag & DATA_OPEN)
       close_data(ftpp);
    
    PRINT_ERR((LOG_DEBUG, "close_cntl: returned\n"));
}

/*****************************************************************************
 * close_data()
 *
 * FTP データセッションをクローズする。
 * 実際の socket のクローズ処理は close_socket() が行う。
 *
 *  引数：
 *           ftpp : FTP セッションの管理構造体
 *
 * 戻り値：
 *           無し
 *****************************************************************************/
void
close_data(ftpcntl_t * const ftpp)
{
    PRINT_ERR((LOG_DEBUG, "close_data: called\n"));
    /*
     * もしデータセッションがオープン中であればクローズする。
     */
    if(ftpp->datafd != -1){
        close_socket(ftpp->datafd);
        ftpp->datafd = -1;
        ftpp->dataport = 0;
    }
    ftpp->statusflag ^= DATA_OPEN;
    PRINT_ERR((LOG_DEBUG, "close_data: returned.\n"));
}

/*****************************************************************************
 * send_cmd()
 *
 * FTP サーバにコマンドを送信する
 *
 *  引数：
 *           ftpp : FTP セッションの管理構造体
 *           cmd  : サーバに送るコマンド
 *           args : コマンドの引数（引数の必要が無ければ NULL)
 *
 * 戻り値：
 *         成功時 :  0
 *         失敗時 :  -1
 *****************************************************************************/
int
send_cmd(ftpcntl_t * const ftpp, int cmd, char *args)
{
    char     command[FTP_CMD_MAX] ={0};
    uchar_t  telnet_ip[2]    = { 0xff, 0xf4 };
    uchar_t  telnet_synch[1] = { 0xf2 };
    uchar_t  telnet_iac[1]   = { 0xff };
            
    PRINT_ERR((LOG_DEBUG, "send_cmd: called\n"));

    if (args)
        // FTP_CMD_MAX 以上の長さのコマンドは切り詰められる。長いパス名のときに問題になる
        snprintf(command, FTP_CMD_MAX, "%s %s\r\n", cmds[cmd], args);
    else
        snprintf(command, FTP_CMD_MAX, "%s\r\n", cmds[cmd]);

    PRINT_ERR((LOG_INFO, "send_cmd: cmd = %s", command));
    
    /*
     * ABOR(Abort) コマンドを送る場合は、コマンドを送る前に Telnet プロトコルで
     * IP(Interrupt Process), SYNCH(Data Mark) を送らなければならない。
     *
     * IP は 通常データとして 0xFF 0xEE を送る
     * SHNCH は 大域外データとして 0xFF 0xF2 を送る
     * 
     */
    if(cmd == CMD_ABOR){
        // TELNET の IP シーケンスを送信
        if (write_socket(ftpp->cntlfd, telnet_ip, sizeof(telnet_ip), 0) < 0){
            // 回復不能な送信エラーが発生した
            goto error;
        }
        // TELNET の SYNCH シーケンスを大域外データとして送信        
        if (write_socket(ftpp->cntlfd, telnet_iac, sizeof(telnet_iac), MSG_OOB) < 0){
            // 回復不能な送信エラーが発生した
            goto error;
        }
        if (write_socket(ftpp->cntlfd, telnet_synch, sizeof(telnet_synch), 0) < 0){
            // 回復不能な送信エラーが発生した
            goto error;
        }                
    }
    
    // コマンド文字列を socket に送信
    if (write_socket(ftpp->cntlfd, command, strlen(command), 0) < 0){
        // 回復不能な送信エラーが発生した
        goto error;
    }
    

    PRINT_ERR((LOG_DEBUG, "send_cmd: returned (0)\n"));
    return(0);

  error:
    ftpp->statusflag |= CNTL_ERR;
    PRINT_ERR((LOG_DEBUG, "send_cmd: returned (-1)\n"));
    return(-1);
}
/*****************************************************************************
 * write_socket()
 *
 * socket にデータを送信する
 *
 *  引数：
 *           fd   : socket descriptor
 *           buf  : 送信するデータ
 *           len  : データ長
 *           flags: フラグ
 *
 * 戻り値：
 *         成功時 :  0
 *         失敗時 :  -1
 *****************************************************************************/
int
write_socket(int fd, void *buf, size_t len, int flags)
{

    PRINT_ERR((LOG_DEBUG, "write_socket: called\n"));

    do {
        if ( send(fd, buf, len, flags) < 0){
            if(errno == EINTR || errno == EWOULDBLOCK || errno == 0){
                // 無視するエラー
                PRINT_ERR((LOG_NOTICE, "write_socket: send: %s\n", strerror(errno)));
            } else {
                /*
                 * 致命的なエラーが発生した
                 */
                print_err(LOG_ERR,"write_socket: send %s (%d)\n", strerror(errno), errno);
                PRINT_ERR((LOG_DEBUG,"write_socket returned\n"));
                goto error;
            }
            // EINTR, EWOULDBLOCK だった場合、RETRY_SLEEP_SEC だけ待って再度トライ
            sleep(RETRY_SLEEP_SEC);
        }
        break;
    } while (1);
    
    PRINT_ERR((LOG_DEBUG, "write_socket: returned (0)\n"));
    return(0);

  error:
    PRINT_ERR((LOG_DEBUG, "write_socket: returned (-1)\n"));
    return(-1);
}

/*****************************************************************************
 * recv_res()
 *
 * FTP サーバからの応答を受け取る
 *
 *  引数：
 *           ftpp     : FTP セッションの管理構造体
 *           cmd      : FTP のコマンド（番号で表現）
 *           response : 呼び出し元から渡されたレスポンスを格納するバッファ
 *           len      : バッファのサイズ
 *
 * 戻り値：
 *         成功時 :  リプライコード番号
 *         失敗時 :  -1
 *****************************************************************************/
int
recv_res(ftpcntl_t * const ftpp, int cmd, char *response, size_t len)
{
    char *writep;            // バッファ response の書き込み開始位置
    char *line_head;         // <CR><LF>で区切ったレスポンス行の始まり位置    
    int  recvsize;           // socket から読み取ったデータサイズ
    int  leftsize;           // バッファ中の未使用サイズ
    int  reply_code = 0;     // リプライコード番号
    int  lines = 0;          // 受信したレスポンスの行数（デバッグ用）
    int  i;
    int  response_complete  = 0;

    line_head = writep = response;
    leftsize = len;

    PRINT_ERR((LOG_DEBUG, "recv_res: called\n"));
    PRINT_ERR((LOG_DEBUG, "recv_res: cmd = %s\n", cmds[cmd]));    

    /*
     * バッファをゼロクリア
     */
    memset(response, 0x0, len);    

    do {
        if( (recvsize = read_socket(ftpp->cntlfd, writep, leftsize)) < 0) {
            // コントロールセッションに回復不可能なエラーが発生した。
            goto error;
        }

        if(recvsize == 0){
            //コントロールセッションがクローズされてしまった。
            PRINT_ERR((LOG_DEBUG, "recv_res: control session unexpectedly closed\n"));            
            goto error;
        }
        
        PRINT_ERR((LOG_DEBUG, "recv_res: writep = %s", writep));

        for ( i = 0 ; i < recvsize ; i++){
            if ( writep[i] == 0x0a && writep[i-1] == 0x0d){
                PRINT_ERR((LOG_DEBUG, "recv_res: <CR><LF> found\n"));
                /*
                 * <CR><LF>を受けとった。
                 * ここまでで、少なくとも１行のレスポンスを受け取ったことになる。
                 * 3 桁のリプライコードに続く文字から、次のレスポンス行があるかどうかを判断する。
                 *
                 * スペースだったら・・・ レスポンス終わり。for ループを抜ける
                 * ハイフンだったら・・・次のレスポンスがある。for ループを続ける。
                 */
                lines++;                
                if (line_head[3] == ' '){
                    response_complete++;
                    break;
                }
                
                /*
                 * レスポンス行の先頭をセットし直す
                 */
                line_head = &writep[i+1];
            }
        }

        if(response_complete)
            break;

        writep += recvsize;
        leftsize -= recvsize;
    } while (leftsize > 0);

    /*
     * バッファーオーバーフローを避けるため、バッファの最後に 0x0 をセット
     */
    response[len-1] = 0x0;

    sscanf(response, "%3d %*s", &reply_code);

    PRINT_ERR((LOG_INFO, "recv_res: Reply Code: %d\n", reply_code));
    PRINT_ERR((LOG_DEBUG, "recv_res: Reply lines: %d line\n", lines));

    /*
     * TODO: reply code の妥当性チェック
     */
        
    PRINT_ERR((LOG_DEBUG, "recv_res: returned (0)\n"));
    return(reply_code);

  error:
    ftpp->statusflag |= CNTL_ERR;    
    PRINT_ERR((LOG_DEBUG, "recv_res: returned (-1)\n"));
    return(-1);
}

/*****************************************************************************
 * read_socket
 *
 * Socket からデータを読み込む
 *
 *  引数：
 *
 *           fd   : Socket descriptor
 *           buf  : 受信データを格納するバッファ
 *           len  : バッファのサイズ
 *                   ToDo here !! バッファのサイズ・・・って書いてるけど、実際には device から要求
 *                                されたサイズがそのまま載ってくる。これじゃオーバーフローしちゃうよ！
 *
 * 戻り値：
 *         成功時 :  読み込んだサイズ（コネクション切断時は 0）
 *         失敗時 :  -1
 *****************************************************************************/
int
read_socket(int fd, char *buf, size_t len)
{
    static fd_set fds;
    int   ret;
    struct timeval timeout;

    PRINT_ERR((LOG_DEBUG, "read_socket: called\n"));

    FD_ZERO(&fds);

    do {
        FD_SET(fd, &fds);
        timeout.tv_sec = SELECT_CMD_TIMEOUT;
        timeout.tv_usec = 0;

        ret = select(FD_SETSIZE, &fds, NULL, NULL, &timeout);
        if( ret < 0){
            print_err(LOG_ERR,"read_socket: select: %s\n", strerror(errno));
            goto error;
        } else if ( ret == 0 ){
            // SELECT_CMD_TIMEOUT 間に応答の受信がなければ切断する
            PRINT_ERR((LOG_DEBUG, "read_socket: select timeout\n"));
            goto error;
        }
        if((ret = recv(fd, buf, len,0)) < 0)  {
            if(errno == EINTR || errno == EWOULDBLOCK || errno == 0){
                // 回復可能なエラーの場合ループを廻る
                PRINT_ERR((LOG_NOTICE, "read_socket: recv %s\n", strerror(errno)));
                continue;
            }
            // 致命的なエラー
            print_err(LOG_ERR,"read_socket: recv %s (%d)\n", strerror(errno),errno);
            goto error;
        }
        if(ret == 0)
            // 接続が切断された
            PRINT_ERR((LOG_DEBUG, "read_socket: connection closed\n"));
        break;
    } while (1);

    PRINT_ERR((LOG_DEBUG, "read_socket: returned (%d)\n", ret));
    return(ret);

  error:
    PRINT_ERR((LOG_DEBUG, "read_socket: returned (-1)\n"));
    return(-1);
}

/*****************************************************************************
 * enter_passive
 *
 * パッシブモードに移行し、サーバからポート番号を得る
 *
 *  引数：
 *
 *           ftpp : ftpcntl 構造体
 *
 * 戻り値：
 *         成功時 :  0
 *         失敗時 :  -1
 *****************************************************************************/
int
enter_passive(ftpcntl_t * const ftpp)
{
    char response[FTP_RES_MAX] = {0};
    int reply_code;
    int ip[4];
    int port1, port2;

    PRINT_ERR((LOG_DEBUG, "enter_passive: called\n"));

    // PASV コマンド発行
    if(send_cmd(ftpp, CMD_PASV, NULL) < 0){
        close_cntl(ftpp);
        goto error;
    }
    
    if(recv_res(ftpp, CMD_PASV, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;
    }
    
    /*
     * サーバからのレスポンス構文
     * 
     * 227 Entering Passive Mode (172.16.1.3,37,84)
     * ^^^ ^^^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^ ^^^^
     * (1) (2)                    (3)        (4)
     * 
     * (1)リプライコード (2)テキスト (3)アドレス  (4)ポート
     */
    sscanf(response, "%d %*s %*s %*s (%d,%d,%d,%d,%d,%d)",
           &reply_code, &ip[0], &ip[1], &ip[2], &ip[3], &port1, &port2);
    /*
     * データ転送用のポート番号をセットする
     */ 
    ftpp->dataport = port1 * 256 + port2;
    PRINT_ERR((LOG_INFO, "enter_passive: %d.%d.%d.%d:%d \n", ip[0],ip[1],ip[2],ip[3], ftpp->dataport));
    PRINT_ERR((LOG_DEBUG, "enter_passive: returned (0)\n"));
    return(0);
    
  error:
    ftpp->dataport = 0;
    PRINT_ERR((LOG_DEBUG, "enter_passive: returned (-1)\n"));
    return(-1);
}

/*****************************************************************************
 * read_file_block()
 *
 * ファイルの指定されたオフセットから、指定されたバイト数だけ読み込む
 *
 *  引数：
 *
 *           ftpp      : ftpcntl 構造体
 *           pathname  : データを読み込むファイルのパス
 *           buffer    : データを書き込むバッファ
 *           offset    : ファイルのデータ読み込み開始位置
 *           size      : 要求されたデータサイズ
 *
 * 戻り値：
 *         成功時 :  最終的に読み込んだデータサイズ
 *                   指定されたオフセット値が、ファイルサイズを超える場合は０が返る。
 *         失敗時 :  -1
 *****************************************************************************/
int
read_file_block(ftpcntl_t * const ftpp, char *pathname, caddr_t buffer, off_t offset, size_t size)
{
    char response[FTP_RES_MAX] = {0}; // コントロールセッションのレスポンスを書き込むバッファ
    char off[20];
    size_t readsize;
    int    reply_code;

    PRINT_ERR((LOG_DEBUG, "read_file_block: called\n"));

    /*
     * データセッションを PASV モードでオープン
     */ 
    if (enter_passive(ftpp) < 0)
        goto error;

    if (open_data(ftpp) < 0)
        goto error;


    snprintf(off, 20, "%ld", offset);
    PRINT_ERR((LOG_DEBUG, "read_file_block: off = %s size = %d\n",off, size));

    /*
     *offset 値を検証
     */
    /*
    if( check_offset(ftpp, offset) < 0){
        //
        //  オフセット値が、ファイルサイズより大きかったようだ。
        //  データコネクションだけクローズして、０を返す
        //
        close_data(ftpp);
        PRINT_ERR((LOG_DEBUG, "read_file_block: returned (0)\n"));            
        return(0);
    }
    */
    
    // REST(Restart) コマンドを発行
    if( send_cmd(ftpp, CMD_REST, off) < 0){
        close_cntl(ftpp);
    }
    if(recv_res(ftpp, CMD_REST, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;
    }

    
    // RETR(Retrieve) コマンドを発行
    if(send_cmd(ftpp, CMD_RETR, pathname) < 0){
        close_cntl(ftpp);
        goto error;        
    }
    if((reply_code = recv_res(ftpp, CMD_RETR, response, sizeof(response))) < 0){
        close_cntl(ftpp);
        goto error;        
    }

    /*
     * もしサーバが 550 を返してきたら、ファイルが無い可能性がある。
     */
    if(reply_code == 550){
        PRINT_ERR((LOG_DEBUG, "read_file_block: server returned 550.\n"));
	close_data(ftpp);
        return(0);
    }

    /*
     * データコネクションから指定バイト読み込む
     */ 
    if( (readsize = read_socket_bytes(ftpp->datafd, buffer, size)) < 0){
        close_data(ftpp);
        goto error;
    }

    // ABOR(Abort) コマンドを発行
    if(send_cmd(ftpp, CMD_ABOR, NULL) < 0){
        close_cntl(ftpp);
        goto error;        
    }

    /* TODO: abort コマンドの結果の reply の pase がうまくいかない。
     * どうやら
     * 426 Transfer aborted. Data connection closed.
     * 226 Abort successful
     * という２つの応答があるようだ。とりあえず recv_res を２回呼ぶようにする・・
     */
    if(recv_res(ftpp, CMD_ABOR, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;        
    }
    if(recv_res(ftpp, CMD_ABOR, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;        
    }    
    close_data(ftpp);
    PRINT_ERR((LOG_DEBUG, "read_file_block: returned (%d)\n", readsize));    
    return(readsize);

  error:
    PRINT_ERR((LOG_DEBUG, "read_file_block: returned (-1)\n"));    
    return(-1);
}

/*****************************************************************************
 * open_data()
 *
 * FTP データセッションをオープンする。    

 * 実際の socket のオープン処理はは open_socket() が行う。
 *
 *  引数：
 *           ftpp : FTP セッションの管理構造体
 *
 * 戻り値：
 *         成功時 :  ソケット番号
 *         失敗時 :  -1
 *****************************************************************************/
int
open_data(ftpcntl_t * const ftpp)
{
    PRINT_ERR((LOG_DEBUG, "open_data: called\n"));

    if(ftpp->dataport == 0){
        print_err(LOG_ERR, "open_data: Data port is not set\n");
        goto error;
    }

    if ((ftpp->datafd = open_socket(ftpp->server, ftpp->dataport)) < 0)
        goto error;
    /*
     * データセッションの接続に成功した。
     */
    ftpp->statusflag |= DATA_OPEN;
    PRINT_ERR((LOG_DEBUG, "open_data: returned(%d)\n", ftpp->datafd));
    return(ftpp->datafd);

  error:
    PRINT_ERR((LOG_DEBUG, "open_data: returned (-1)\n"));
    return(-1);
    
}

/*****************************************************************************
 * read_socket_bytes()
 *
 * socket から指定バイト分だけ読み込む。
 * もし、指定バイト到達前にコネクションが切断された場合には読み込めた
 * バイト分だけを返す。
 *
 *  引数：
 *           fd     : socket 
 *           buffer : データを書き込むバッファ
 *           size   : 指定されたバイト数
 *           
 * 戻り値：
 *         成功時 :  読み込んだバイト数
 *         失敗時 :  -1
 *****************************************************************************/
int
read_socket_bytes(int fd, caddr_t buffer, size_t size)
{
    caddr_t  writep;         // バッファへの書き込み位置のポインタ
    size_t   totalbytes = 0; //
    size_t   leftsize = 0;
    int      ret;

    writep = buffer;
    leftsize = size;
    
    PRINT_ERR((LOG_DEBUG, "read_socket_bytes: called\n"));

    while((ret = read_socket(fd,  writep, leftsize)) > 0){
        totalbytes += ret;
        if(totalbytes >= size)
            break;
        writep += ret;
        leftsize -= ret;
    }

    /*
     * socket の読み込みでエラーが発生した模様
     */
    if(ret < 0)
        goto error;
    
    PRINT_ERR((LOG_DEBUG, "read_socket_bytes: total read %d bytes.\n", totalbytes));

    if(totalbytes > size)
        totalbytes = size;

    PRINT_ERR((LOG_DEBUG, "read_socket_bytes: returned (%d)\n", totalbytes));
    return(totalbytes);

  error:
    PRINT_ERR((LOG_DEBUG, "read_socket_bytes: returned (-1)\n"));
    return(-1);
}

/*****************************************************************************
 * check_offset
 *
 * SIZE コマンドを発行し、ファイルサイズと要求された offset 値を比較する。
 * もし offset がファイルサイズ内であれば 0　を、そうでなければ -1 を返す。
 *
 *  引数：
 *
 *           ftpp : ftpcntl 構造体
 *
 * 戻り値：
 *         成功時 :  0
 *         失敗時 :  -1
 *****************************************************************************/
int
check_offset(ftpcntl_t * const ftpp, off_t offset)
{
    char response[FTP_RES_MAX] = {0};
    int reply_code;
    int filesize;

    PRINT_ERR((LOG_DEBUG, "check_offset: called\n"));
    
    // SIZE コマンド発行
    if(send_cmd(ftpp, CMD_SIZE, NULL) < 0){
        close_cntl(ftpp);
        goto error;
    }
    
    if(recv_res(ftpp, CMD_SIZE, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;
    }
    
    /*
     * サーバからのレスポンス
     * 
     * 213 22114
     * ^^^ ^^^^^
     * (1) (2)  
     * 
     * (1)リプライコード (2)ファイルサイズ
     */
    sscanf(response, "%d %d", &reply_code, &filesize);
    
    PRINT_ERR((LOG_DEBUG, "check_offset: filesize = %d, offset = %d", filesize, offset));
    if(filesize < offset){
        /*
         * 指示されたオフセット値がファイルサイズ値よりも大きい！！
         */
        PRINT_ERR((LOG_DEBUG, "check_offset: offset too large\n"));
        goto error;
    }
    PRINT_ERR((LOG_DEBUG, "check_offset: returned (0)\n"));
    return(0);
    
  error:
    PRINT_ERR((LOG_DEBUG, "check_offset: returned (-1)\n"));
    return(-1);
}

/*****************************************************************************
 * read_directory_entries
 *
 * 指定されたディレクトリのエントリを取ってくる
 *
 *  引数：
 *
 *           ftpp      : ftpcntl 構造体
 *           pathname  : 読み込むディレクトリのパス *           
 *           buffer    : データを書き込むバッファ
 *           offset    : ディレクトリエントリの読み込み開始位置
 *           size      : 要求されたデータサイズ
 *
 * 戻り値：
 *         成功時 :  最終的に読み込んだデータサイズ
 *                   指定されたオフセット値が、ファイルサイズを超える場合は０が返る。
 *         失敗時 :  -1
 *****************************************************************************/
int
read_directory_entries(ftpcntl_t * const ftpp, char *pathname, caddr_t buffer, off_t offset, size_t size)
{
    char    response[FTP_RES_MAX]  = {0}; // コントロールセッションのレスポンスを書き込むバッファ
    size_t  readsize;
    int     reply_code;
    char   *tempbuf;     // オフセットまで読み込むための仮のデータ置き場

    PRINT_ERR((LOG_DEBUG, "read_directory_entries: called\n"));

    //  ASCII モードに移行
    if(send_cmd(ftpp, CMD_TYPE, "A") < 0){
        close_cntl(ftpp);
        goto error;
    }

    if(recv_res(ftpp, CMD_TYPE, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;
    }

    /*
     * データセッションを PASV モードでオープン
     */ 
    if (enter_passive(ftpp) < 0)
        goto error;

    if (open_data(ftpp) < 0)
        goto error;

    // CWD(change working directory?) コマンドを発行
    if(send_cmd(ftpp, CMD_CWD, pathname) < 0){
        close_cntl(ftpp);
        goto error;        
    }
    if(recv_res(ftpp, CMD_CWD, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;        
    }    

    // NLST(list) コマンドを発行
    if(send_cmd(ftpp, CMD_NLST, "-a") < 0){
        close_cntl(ftpp);
        goto error;        
    }
    if((reply_code = recv_res(ftpp, CMD_NLST, response, sizeof(response))) < 0){
        close_cntl(ftpp);
        goto error;        
    }
    /*
     * もしサーバが 550 を返してきたら、ディレクトリが何もファイルを持っていないということ。
     */
    if(reply_code == 550){
        PRINT_ERR((LOG_DEBUG, "read_directory_entries: server returned 550.\n"));        
        //  BINARY モードに移行
        if(send_cmd(ftpp, CMD_TYPE, "I") < 0){
            close_cntl(ftpp);
            goto error;
        }
        if(recv_res(ftpp, CMD_TYPE, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            goto error;
        }
        close_data(ftpp);
        return(0);
    }

    /*
     * データコネクションからオフセット分だけ先に読む。受信データは破棄する。
     */
    if(offset != 0){
        tempbuf = malloc(offset);
        if( (readsize = read_socket_bytes(ftpp->datafd, tempbuf, offset)) < 0){
            close_data(ftpp);
            goto error;
        }
        free(tempbuf);        
        if(readsize < offset){
            PRINT_ERR((LOG_DEBUG, "read_directory_entries: offset too large\n"));
            close_data(ftpp);
            readsize = 0;
            goto done;
        }
    }

    /*
     * データコネクションから指定バイト読み込む
     */    
    if( (readsize = read_socket_bytes(ftpp->datafd, buffer, size)) < 0){
        close_data(ftpp);
        goto error;
    }

    /*
     * もし指定サイズいっぱいまで reply がきたら、まだデータ転送中の
     * 可能性があるので abort する
     */
    if(readsize == size){
        // ABOR(Abort) コマンドを発行
        if(send_cmd(ftpp, CMD_ABOR, NULL) < 0){
            close_cntl(ftpp);
            goto error;        
        }

        /*
         * TODO: abort コマンドの結果の reply の pase がうまくいかない。
         * どうやら
         * 426 Transfer aborted. Data connection closed.
         * 226 Abort successful
         * という２つの応答があるようだ。とりあえず recv_res を２回呼ぶようにする・・
         */
        if(recv_res(ftpp, CMD_ABOR, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            goto error;        
        }
        if(recv_res(ftpp, CMD_ABOR, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            goto error;        
        }
    }
    
    close_data(ftpp);

    /*
     * もし、abort せずにデータ転送を終了していたら。
     * 226 Transfer complete. を受け取る
     */
    if(readsize > 0 && readsize < size){    
        if(recv_res(ftpp, CMD_NLST, response, sizeof(response)) < 0){
            close_cntl(ftpp);
            goto error;
        }
    }

  done:
    //  BINARY モードに移行
    if(send_cmd(ftpp, CMD_TYPE, "I") < 0){
        close_cntl(ftpp);
        goto error;
    }
    if(recv_res(ftpp, CMD_TYPE, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;
    }

    PRINT_ERR((LOG_DEBUG, "read_directory_entries: returned (%d)\n", readsize));    
    return(readsize);

  error:
    PRINT_ERR((LOG_DEBUG, "read_directory_entries: returned (-1)\n"));    
    return(-1);
}


/*****************************************************************************
 * process_readdir_request
 *
 * main() から呼ばれ、READDIR_REQUEST を処理する
 *
 *  引数：
 *
 *           ftpp      : ftpcntl 構造体
 *           pathname  : 読み込むディレクトリのパス
 *           mapaddr   : ディレクトリエントリを書き込むバッファ
 *           offset    : ディレクトリエントリの読み込み開始位置
 *           size      : 要求されたデータサイズ
 *
 * 戻り値：
 *         継続処理が必要無い場合 : 0
 *         継続処理が必要な場合   : -1
 *         
 *****************************************************************************/
int
process_readdir_request(ftpcntl_t * const ftpp, char *pathname, caddr_t mapaddr, off_t offset, size_t size)
{
    int     i ;
    int     readsize;
    int     result;

    PRINT_ERR((LOG_DEBUG, "process_readdir_request called\n"));    

    readsize = read_directory_entries(ftpp, pathname, mapaddr, offset, size );

    PRINT_ERR((LOG_INFO, "process_readdir_request: read_directory_entries returned (%d)\n",readsize));    

    if (readsize < 0){
        PRINT_ERR((LOG_DEBUG, "process_readdir_request: Error happened, close control sessioin\n"));
        close_cntl(ftpp);
        return(-1);
    } else if (readsize == 0){
        PRINT_ERR((LOG_DEBUG, "directory has no more entry.\n"));            
        result = ENOENT;
        write(ftpp->devfd, &result, sizeof(int));
        return(0);
    }

    /*
     * <CR> <LF> を NULL に変換。
     */
    for (i = 0 ; i < readsize ; i++){
        if(mapaddr[i] == 0x0a || mapaddr[i] == 0x0d)
            mapaddr[i] = 0x0;
    }

    if(readsize == size)
        result = MOREDATA;
    else
        result = 0;
    write(ftpp->devfd, &result, sizeof(int));
    return(0);
    
}

/*****************************************************************************
 * process_read_request
 *
 * main() から呼ばれ、READ_REQUEST を処理する
 *
 *  引数：
 *
 *           ftpp      : ftpcntl 構造体
 *           pathname  : データを読み込むファイルのパス
 *           mapaddr   : データを書き込むマッピングされたバッファ
 *           offset    : ファイルのデータ読み込み開始位置
 *           size      : 要求されたデータサイズ
 *
 * 戻り値：
 *         継続処理が必要無い場合 : 0
 *         継続処理が必要な場合   : -1
 *         
 *****************************************************************************/
int
process_read_request(ftpcntl_t * const ftpp, char *pathname, caddr_t mapaddr, off_t offset, size_t size)
{
    int     readsize;
    int     result;    

    PRINT_ERR((LOG_DEBUG, "process_read_request called\n"));

    readsize = read_file_block(ftpp, pathname, mapaddr, offset, size);

    PRINT_ERR((LOG_INFO, "process_read_request: read_file_block returned (%d)\n",readsize));

    if (readsize < 0){
        // TODO: エラー iumfscntl デバイスに通知する方法が無い・・                    
        PRINT_ERR((LOG_DEBUG, "process_read_request: Error happened, close control sessioin\n"));
        close_cntl(ftpp);
        return(-1);
    } else if (readsize == 0){
        PRINT_ERR((LOG_DEBUG, "Requested offset too large.\n"));
        result = ENOENT;
        write(ftpp->devfd, &result, sizeof(int));
        return(0);
    }
    
    result = 0;
    write(ftpp->devfd, &result, sizeof(int));
    return(0);
}

/*****************************************************************************
 * process_getattr_request
 *
 * main() から呼ばれ、GETATTR_REQUEST を処理する
 *
 *  引数：
 *
 *           ftpp      : ftpcntl 構造体
 *           pathname  : データを読み込むファイルのパス
 *           offset    : ファイルのデータ読み込み開始位置
 *           mapaddr   : データを書き込むバッファ
 *
 * 戻り値：
 *         継続処理が必要無い場合 : 0
 *         継続処理が必要な場合   : -1
 *         
 *****************************************************************************/
int
process_getattr_request(ftpcntl_t * const ftpp, char *pathname, caddr_t mapaddr)
{
    char    buf[MMAPSIZE] ; // LIST(ls) の結果を入れる。こんな大きなサイズはいらないが、
    int     readsize;
    int     result;
    vattr_t *vap;
    int     err = 0;

    PRINT_ERR((LOG_DEBUG, "process_getattr_request called\n"));    

    memset(buf, 0x0, MMAPSIZE);

    readsize = get_file_attributes(ftpp, pathname, buf, MMAPSIZE);

    if (readsize < 0){
        PRINT_ERR((LOG_DEBUG, "process_getattr_request: Error happened, close control sessioin\n"));
        close_cntl(ftpp);
        return(-1);
    } else if (readsize == 0){
        PRINT_ERR((LOG_DEBUG, "process_getattr_request: readsize = 0\n"));
        result = ENOENT;
        write(ftpp->devfd, &result, sizeof(int));
        return(0);
    }

    vap = (vattr_t *)mapaddr;

    /*
     * NLST の結果を解析して vattr 構造体に必要なデータをセットする
     */
    if (parse_attributes(vap, buf) < 0){        
        err = ENOENT;
        goto done;
    }

    PRINT_ERR((LOG_DEBUG, "process_getattr_request: filesize = %d\n", vap->va_size));    

  done:
    result = err;
    write(ftpp->devfd, &result, sizeof(int));
    if (err)
        return(-1);
    else
        return(0);
}


/*****************************************************************************
 * get_file_attributes
 *
 * ファイル属性値を得る
 *
 *  引数：
 *
 *           ftpp      : ftpcntl 構造体
 *           pathname  : データを読み込むファイルのパス
 *           buffer    : データを書き込むバッファ
 *           size      : 要求されたデータサイズ
 *
 * 戻り値：
 *         成功時 :  最終的に読み込んだデータサイズ
 *                   指定されたオフセット値が、ファイルサイズを超える場合は０が返る。
 *         失敗時 :  -1
 *****************************************************************************/
int
get_file_attributes(ftpcntl_t * const ftpp, char *pathname, caddr_t buffer, size_t size)
{
    char response[FTP_RES_MAX] = {0}; // コントロールセッションのレスポンスを書き込むバッファ
    size_t readsize;
    char args[MAXPATHLEN] = {0};

    PRINT_ERR((LOG_DEBUG, "get_file_attributes: called\n"));

    /*
     * NLST コマンドの引数を「-dlAL ファイル名」オプションをつける。
     */ 
    snprintf(args, MAXPATHLEN, "-dlAL %s", pathname);    

    //  ASCII モードに移行
    if(send_cmd(ftpp, CMD_TYPE, "A") < 0){
        close_cntl(ftpp);
        goto error;
    }

    if(recv_res(ftpp, CMD_TYPE, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;
    }

    //データセッションを PASV モードでオープン
    if (enter_passive(ftpp) < 0){
        goto error;
    }

    if (open_data(ftpp) < 0)
        goto error;

    // NLST コマンドを発行
    if( send_cmd(ftpp, CMD_NLST, args) < 0){        
        close_cntl(ftpp);
    }
    if(recv_res(ftpp, CMD_NLST, response, sizeof(response)) < 0){        
        close_cntl(ftpp);
        goto error;
    }
    
    /*
     * データコネクションから指定バイト読み込む
     */ 
    if( (readsize = read_socket_bytes(ftpp->datafd, buffer, size)) < 0){
        close_data(ftpp);
        goto error;
    }

    close_data(ftpp);

    /*
     * 226 Transfer complete. を受け取る
     */
    if(recv_res(ftpp, CMD_NLST, response, sizeof(response)) < 0){        
        close_cntl(ftpp);
        goto error;
    }

    //BINARY モードに移行
    if(send_cmd(ftpp, CMD_TYPE, "I") < 0){
        close_cntl(ftpp);
        goto error;
    }

    if(recv_res(ftpp, CMD_TYPE, response, sizeof(response)) < 0){
        close_cntl(ftpp);
        goto error;
    }
    
    PRINT_ERR((LOG_DEBUG, "get_file_attributes: returned (%d)\n", readsize));    
    return(readsize);

  error:
    PRINT_ERR((LOG_DEBUG, "get_file_attributes: returned (-1)\n"));    
    return(-1);
}

/**************************************************************
 * parse_attributes()
 *
 * NLST -dlAL の結果を解析し、vattr 構造体を埋める。
 * ファイルサイズ以外は参考程度なので、もし解読不能だとしても
 * デフォルト値をセットしてエラーとはしない。
 *
 * -rwxr-xr-x   1 root  545    203 Dec 10 00:13 clean.sh
 *
 * 引数
 *
 *  vap : 解析した属性値をセットする vattr 構造体
 *  buf : サーバから受け取ったファイルの NLST -dlAL の結果。
 *
 * 戻り値
 *
 *    正常時   : 0
 *    エラー時 : -1
 *
 **************************************************************/
int
parse_attributes(vattr_t *vap, char *buf)
{
    char   str_month[10];  // NLST -dlAL の出力からえられた月
    int    month = 0, day = 0, year = 0, hour = 0, minute = 0;
    time_t timenow;        // 現在時刻がセットされる time 構造体        
    struct tm *tmnow;      // 現在時間がセットされる tm 構造体
    struct tm tmfile[1];   // 解析したファイルの修正時刻がセットされる tm 構造体
    int    filesize;

    int    use_current_time = 0;    

    PRINT_ERR((LOG_DEBUG, "parse_attributes called\n"));

    PRINT_ERR((LOG_DEBUG, "parse_attributes: file type = \"%c\"\n", buf[0]));        

    PRINT_ERR((LOG_DEBUG, "parse_attributes: buf = \"%s\"\n", buf));            
    /*
     * ファイルタイプのチェック
     * 出力結果の最初の一文字目で判断する。
     */
    switch(buf[0]){
        case 'd':
            vap->va_type = VDIR; // ディレクトリ
            vap->va_mode |= S_IFDIR;            
            break;
        case 'D':
            vap->va_type = VDOOR; // DOOR ファイル
            vap->va_mode |= S_IFDOOR;            
            break;
        case 'l':
            vap->va_type = VLNK; // シンボリックリンク
            vap->va_mode |= S_IFLNK;            
            break;            
        case 'b':
            vap->va_type = VBLK; // ブロックデバイス
            vap->va_mode |= S_IFBLK;            
            break;            
        case 'c':
            vap->va_type = VCHR; // キャラクタデバイス
            vap->va_mode |= S_IFCHR;            
            break;            
        case 'p':
            vap->va_type = VFIFO; // FIFO
            vap->va_mode |= S_IFIFO;            
            break;
#ifdef SOL10            
        case 'P':
            vap->va_type = VPORT; // ...?
            vap->va_mode |= S_IFPORT;            
            break;
#endif
        case 's':
            vap->va_type = VSOCK; // ソケット
            vap->va_mode |= S_IFSOCK;            
            break;            
        default:
            vap->va_type = VREG; // 通常ファイル。
            vap->va_mode |= S_IFREG;            
            break;            
    }

    /*
     * オーナーのアクセス権をチェック
     */
    if(buf[1] == 'r')
        vap->va_mode |= S_IRUSR;

    if(buf[2] == 'w')
        vap->va_mode |= S_IWUSR;

    if(buf[3] == 'x' || buf[3] == 's')
        vap->va_mode |= S_IXUSR;
    /*
     * グループのアクセス権をチェック
     */
    if(buf[4] == 'r')
        vap->va_mode |= S_IRGRP;

    if(buf[5] == 'w')
        vap->va_mode |= S_IWGRP;

    if(buf[6] == 'x' || buf[6] == 's')
        vap->va_mode |= S_IXGRP;
    /*
     * その他のユーザのアクセス権をチェック
     */
    if(buf[7] == 'r')
        vap->va_mode |= S_IROTH;

    if(buf[8] == 'w')
        vap->va_mode |= S_IWOTH;

    if(buf[9] == 'x' || buf[9] == 's')
        vap->va_mode |= S_IXOTH;

    /*
     * ファイルサイズを得る
     * ファイルサイズは重要な情報なので、これが正常に得られない場合
     * には -1 が返り、結果としてファイルが見つからないとして扱う。
     * デバイスファイルの場合はサイズを 0 とする。
     * 
     * -rwxr-xr-x   1 root  bin  203 Dec 10 00:13 clean.sh
     *                           ^^^
     */
    if( vap->va_type == VCHR || vap->va_type == VBLK){
        PRINT_ERR((LOG_DEBUG, "parse_attributes: file is device file\n"));        
        filesize = 0;
    } else {
        if (sscanf(buf,"%*s %*d %*s %*s %d %*s", &filesize) == 0){
            PRINT_ERR((LOG_DEBUG, "parse_attributes: can't get file size\n"));
            goto error;
        }
    }
    vap->va_size = filesize;    
    
    /*
     * ファイルの更新日を得る
     * 解析できるフォーマットは以下の 8 通り。
     *
     * デバイスファイルの場合
     *  crw-rw-rw-   1 root  sys   146, 3  Feb  11   00:13 tcp6@0:tcp6
     *  crw-rw-rw-   1 root  sys   146, 3  Feb  11   2005 tcp6@0:tcp6
     *  crw-rw-rw-   1 root  sys   146, 3  2月  11日 00:13 tcp6@0:tcp6
     *  crw-rw-rw-   1 root  sys   146, 3  2月  11日 2005年 tcp6@0:tcp6
     *
     * その他のファイルの場合
     *  -rwxr-xr-x   1 root  bin      203  Dec  10   00:13  clean.sh
     *  -rwxr-xr-x   1 root  bin      203  Dec  10   2005   clean.sh
     *  -rwxr-xr-x   1 root  bin      203  12月 10日 00:13  clean.sh
     *  -rwxr-xr-x   1 root  bin      203  12月 10日 2005年 clean.sh
     */
    if( vap->va_type == VCHR || vap->va_type == VBLK){        
        if (sscanf(buf,"%*s %*d %*s %*s %*d,%*d %s %d %d:%d %*s", str_month, &day, &hour, &minute) == 4){
            // crw-rw-rw-   1 root     sys      146,  3 Feb 11  00:13 tcp6@0:tcp6
            if((month = month_to_int(str_month)) < 0){
                use_current_time = 1;
                PRINT_ERR((LOG_DEBUG, "parse_attributes: cannot parse month\n"));            
            }
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Time is %d:%d\n", hour, minute));        
        } else if (sscanf(buf,"%*s %*d %*s %*s %*d,%*d %d%*s %d%*s %d:%d %*s",
                          &month, &day, &hour, &minute) == 4){
            // crw-rw-rw-   1 root  sys   146, 3  2月  11日 00:13 tcp6@0:tcp6
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Time is %d:%d\n", hour, minute));
        } else if (sscanf(buf,"%*s %*d %*s %*s %*d,%*d %d%*s %d%*s %d%*s %*s",
                          &month, &day, &year ) == 3){
            // crw-rw-rw-   1 root  sys   146, 3  2月  11日 2005年 tcp6@0:tcp6
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Year is %d\n", year));        
        } else if (sscanf(buf,"%*s %*d %*s %*s %*d,%*d %s %d %d %*s", str_month, &day, &year) == 3){
            // crw-rw-rw-   1 root  sys   146, 3  Feb  11   2005 tcp6@0:tcp6
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Year is %d\n", year));        
        } else {
            use_current_time = 1;        
        }
    } else {
        if (sscanf(buf,"%*s %*d %*s %*s %*d %s %d %d:%d %*s", str_month, &day, &hour, &minute) == 4){
            // -rwxr-xr-x   1 root  bin   203 Dec 10 00:13 clean.sh
            if((month = month_to_int(str_month)) < 0){
                use_current_time = 1;
                PRINT_ERR((LOG_DEBUG, "parse_attributes: cannot parse month\n"));            
            }
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Time is %d:%d\n", hour, minute));        
        } else if (sscanf(buf,"%*s %*d %*s %*s %*d %d%*s %d%*s %d:%d %*s",
                          &month, &day, &hour, &minute) == 4){
            // -rwxr-xr-x   1 root  bin   203 12月 10日 00:13 clean.sh
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Time is %d:%d\n", hour, minute));
        } else if (sscanf(buf,"%*s %*d %*s %*s %*d %d%*s %d%*s %d%*s %*s",
                          &month, &day, &year ) == 3){
            // -rwxr-xr-x   1 root  bin   203 12月 10日 2005年 clean.sh
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Year is %d\n", year));        
        } else if (sscanf(buf,"%*s %*d %*s %*s %*d %s %d %d %*s", str_month, &day, &year) == 3){
            // -rwxr-xr-x   1 root  bin   203 Dec 10 2005 clean.sh
            PRINT_ERR((LOG_DEBUG, "parse_attributes: Year is %d\n", year));        
        } else {
            use_current_time = 1;        
        }
    }

    /*
     * 上で得られた情報から vattr 構造体の va_mtime をセットする
     */
    time(&timenow);
    if(use_current_time){
        /*
         * もし解析に失敗したら、現在の時間をセットする。
         */
        vap->va_mtime.tv_sec = vap->va_atime.tv_sec = vap->va_ctime.tv_sec = timenow;
        PRINT_ERR((LOG_DEBUG, "parse_attributes: set current time\n"));
    } else {
        tmnow = localtime(&timenow);
        
        tmfile->tm_sec  = 0;
        tmfile->tm_mday = day;
        tmfile->tm_mon  = month - 1;
        
        if (year == 0){
            /*
             * 西暦の表示が無い場合（６ヶ月以内にファイルが更新されている場合）
             */
            if(tmnow->tm_mon + 1 < month)
                /*
                 * 去年更新されたファイルと思われる 例）現在が1月でファイルの更新月が12月
                 */
                tmfile->tm_year = tmnow->tm_year -1;
            else 
                tmfile->tm_year = tmnow->tm_year;
            tmfile->tm_min   = minute;
            tmfile->tm_hour  = hour;  
        } else {
            /*
             * 西暦の表示がある場合（最後のファイルの更新が６ヶ月以上前）
             */
            tmfile->tm_year = year - 1900;
            /*
             * 更新時刻の情報は無いので、0 時 00 分 にセット
             */
            tmfile->tm_min   = 0;
            tmfile->tm_hour  = 0;
        }
        vap->va_mtime.tv_sec = vap->va_atime.tv_sec = vap->va_ctime.tv_sec = mktime(tmfile);
    }
    
    return(0);
    
  error:
    PRINT_ERR((LOG_DEBUG, "parse_attributes: failed\n"));    
    return(-1);
}

/*************************************************************
 * month_to_int()
 *
 * 3文字略語の月を数字に変換する。
 *
 * 引数
 *     month : Jan, Feb 等の月をあらわす３文字の文字列
 *
 * 戻り値
 *    正常時 : 月をあらわす数
 *    異常時 : -1
 *
 *************************************************************/
int
month_to_int(char *month){

        if(strncasecmp(month, "Jan", 3) == 0)
                return(1);
        else if(strncasecmp(month, "Feb", 3) == 0)
                return(2);
        else if(strncasecmp(month, "Mar", 3) == 0)
                return(3);
        else if(strncasecmp(month, "Apr", 3) == 0)
                return(4);
        else if(strncasecmp(month, "May", 3) == 0)
                return(5);
        else if(strncasecmp(month, "Jun", 3) == 0)
                return(6);
        else if(strncasecmp(month, "Jul", 3) == 0)
                return(7);
        else if(strncasecmp(month, "Aug", 3) == 0)
                return(8);
        else if(strncasecmp(month, "Sep", 3) == 0)
                return(9);
        else if(strncasecmp(month, "Oct", 3) == 0)
                return(10);
        else if(strncasecmp(month, "Nov", 3) == 0)
                return(11);
        else if(strncasecmp(month, "Dec", 3) == 0)
                return(12);
        else
                return(-1);
}
