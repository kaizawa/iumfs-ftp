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
/**********************************************************
 * fstestd.c
 *
 * iumfsファイルシステムの機能テスト用デーモン
 *
 * iumfsd を模しているが、実際にはリモートのFTP サーバには
 * つなぎにいかず、ローカルのディレクトリやファイルの情報を返す。
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
#include <dirent.h>
#include "iumfs.h"

#define ERR_MSG_MAX    300       // syslog に出力する最長文字数
#define SELECT_CMD_TIMEOUT    10 // TEST コマンド発行時のタイムアウト
#define RETRY_SLEEP_SEC       1  // リトライまでの待ち時間
#define RETRY_MAX             1  // リトライ回数
#define FS_BLOCK_SIZE         512 // このファイルシステムのブロックサイズ

#define DIRENTS_MAX 1024 * 1024

/*
 * TEST セッションの管理構造体
 */
typedef struct testcntl
{
    int filefd;       // ローカルファイルのFD
    int devfd;        // iumfsctl デバイスファイルのFD
    int statusflag;   // ステータスフラグ
    char *server;     // TEST サーバ名
    char *loginname;  // ログイン名
    char *loginpass;  // ログインパスワード
    int  dataport;    // データ転送用のポート番号
    char *basepath;   // クライアントが要求しているベースのパス名
    char *pathname;
} testcntl_t;

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
void    close_filefd(testcntl_t * const);
int     process_readdir_request(testcntl_t * const, char *, caddr_t, off_t , size_t );
int     process_read_request(testcntl_t * const, char *, caddr_t, off_t , size_t );
int     process_getattr_request(testcntl_t * const, char *, caddr_t);
int     get_file_attributes(testcntl_t * const, char *, caddr_t, size_t );
int     parse_attributes(vattr_t *, struct stat *);

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


testcntl_t     *gtestp; 

int
main(int argc, char *argv[])
{
    testcntl_t     *testp; 
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

    testp = gtestp = (testcntl_t *) malloc(sizeof(testcntl_t));

    memset(req, 0x0, sizeof(request_t));
    memset(testp, 0x0, sizeof(testcntl_t));

    testp->filefd = -1;

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

    testp->devfd = open(DEVPATH, O_RDWR, 0666);
    if ( testp->devfd < 0){
        perror("open");
        goto error;
    }
    
    PRINT_ERR((LOG_INFO, "main: successfully opened iumfscntl device\n"));    

    mapaddr = (caddr_t)mmap(0, MMAPSIZE, PROT_READ|PROT_WRITE, MAP_SHARED, testp->devfd, 0);
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
            FD_SET(testp->devfd, &err_fds);
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
             */
            FD_ZERO(&fds);            

            FD_SET(testp->devfd, &fds);

            ret = select(FD_SETSIZE, &fds, NULL, &err_fds, NULL);
            if( ret < 0){
                print_err(LOG_ERR,"main: select: %s\n", strerror(errno));
                goto error;
            }

            /*
             * ここにくるのは iumfscntl デバイスが READ 可能な状態の時だけ。
             */
            ret = read(testp->devfd, req, sizeof(request_t));
            if (ret != sizeof(request_t)){
                print_err(LOG_ERR,"main: read size invalid ret(%d) != sizeof(request_t)(%d)\n",
                          ret, sizeof(request_t));
                sleep(1);
                continue;
            }
            inprogress = TRUE;

            PRINT_ERR((LOG_INFO, "==============================================\n"));

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
        }

        switch(req->request_type){
            case READ_REQUEST:
                PRINT_ERR((LOG_INFO, "READ_REQUEST\n"));                
                offset = req->data.read_request.offset;
                size = req->data.read_request.size;
                PRINT_ERR((LOG_INFO, "main: pathname=%s, offset=%d, size=%d\n",pathname,offset,size));
                if(process_read_request(testp, pathname, mapaddr, offset, size) == 0)
                    inprogress = 0;                    
                break;
            case READDIR_REQUEST:
                PRINT_ERR((LOG_INFO, "READDIR_REQUEST\n"));
                offset = req->data.readdir_request.offset;
                size = req->data.readdir_request.size;
                PRINT_ERR((LOG_INFO, "main: pathname=%s, offset=%d, size=%d\n",pathname,offset,size));                
                if(process_readdir_request(testp, pathname, mapaddr, offset, size) == 0)
                    inprogress = 0;
                break;
            case GETATTR_REQUEST:
                PRINT_ERR((LOG_INFO, "GETATTR_REQUEST\n"));                
                PRINT_ERR((LOG_INFO, "main: pathname = %s\n",pathname));
                if(process_getattr_request(testp, pathname, mapaddr) == 0)
                    inprogress = 0;
                break;                
            default:
                result = ENOSYS;
                PRINT_ERR((LOG_ERR, "main: Unknown request type 0x%x\n", req->request_type));
                write(testp->devfd, &result, sizeof(int));
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
 * close_filefd()
 *
 * TEST コントロールセッションをクローズする。
 * 実際の socket のクローズ処理は close_socket() が行う。
 *
 *  引数：
 *           testp : TEST セッションの管理構造体
 *
 * 戻り値：
 *           無し
 *****************************************************************************/
void
close_filefd(testcntl_t * const testp)
{
    close(testp->filefd);

    testp->pathname = NULL;
    testp->filefd = -1;

    PRINT_ERR((LOG_DEBUG, "close_filefd: returned\n"));
}

/*****************************************************************************
 * process_readdir_request
 *
 * main() から呼ばれ、READDIR_REQUEST を処理する
 *
 *  引数：
 *
 *           testp      : testcntl 構造体
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
process_readdir_request(testcntl_t * const testp, char *pathname, caddr_t mapaddr, off_t offset, size_t size)
{
    size_t readsize = 0;
    int result;
    DIR *dirp;
    struct dirent *dp;
    size_t namelen = 0;
    void *read_dirents;
    size_t left_dirents_size = DIRENTS_MAX; // ディレクトリに含まれるエントリの名前の最大合計サイズ 1MB （適当）
    int count = 0;

    PRINT_ERR((LOG_DEBUG, "process_readdir_request: called\n"));    

    if(offset + size > DIRENTS_MAX){
        print_err(LOG_ERR,"process_readdir_request: dirent offset and size exceed max entry size\n");
        exit(1);
    }

    if((read_dirents = malloc(DIRENTS_MAX)) == NULL){
        perror("malloc");
        exit(1);
    }
    
    memset(read_dirents, 0x0, DIRENTS_MAX);

    if ((dirp = opendir(pathname)) == NULL) {
        print_err(LOG_ERR,"process_readdir_request: opendir couldn't open %s\n", pathname);
        exit(1);
    }

    /*
     * iumfs ファイルシステムは mapaddr に <ファイル名> + NULL + NULL という並びで
     * ディレクトリエントリが並んでいることを期待する。なので、ファイル名の後に
     * 明示的に NULL を二つ並べる。
     * See iumfs_request.c#245
     */

    while ((dp = readdir(dirp)) != NULL) {
        namelen = strlen(dp->d_name);
        /*
         * 確保した一時バッファより大きくなってしまう場合はそれ以降の
         * ディレクトリエントリを無視する。
         */ 
        if (left_dirents_size <= namelen + 2){
            memcpy((char *)read_dirents + readsize, dp->d_name, left_dirents_size);
            print_err(LOG_ERR,"process_readdir_request: too many entries... drop rest of etnries\n");
            break;
        }
        memcpy((char *)read_dirents + readsize, dp->d_name, namelen);
        memset((char *)read_dirents + readsize + namelen, 0x0, 2);
        readsize += namelen + 2;
        PRINT_ERR((LOG_DEBUG,"process_readdir_request: entry#%d \"%s\"\n",++count, dp->d_name)); 
        left_dirents_size -= namelen + 2;
    }
    PRINT_ERR((LOG_DEBUG,"process_readdir_request: readsize=%d\n",readsize));

    /*
     * 読み込んだDIRENTが要求されていたサイズより大きかったら driver に
     * MOREDATA を返す。そうでなければ、SUCCESS を返す。
     */ 
    if(offset + size >= readsize){
        memcpy(mapaddr, (char *)read_dirents + offset, readsize - offset);
        result = SUCCESS;
        PRINT_ERR((LOG_DEBUG, "process_readdir_request: directory has no more entry.\n"));        
    } else {
        memcpy(mapaddr, (char *)read_dirents + offset, size);
        result = MOREDATA;
        PRINT_ERR((LOG_DEBUG, "process_readdir_request: directory has more entries.\n"));                
    }
    
    closedir(dirp);    
    write(testp->devfd, &result, sizeof(int));
    return(0);
    
}

/*****************************************************************************
 * process_read_request
 *
 * main() から呼ばれ、READ_REQUEST を処理する
 *
 *  引数：
 *
 *           testp      : testcntl 構造体
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
process_read_request(testcntl_t * const testp, char *pathname, caddr_t mapaddr, off_t offset, size_t size)
{
    int     readsize;
    int     result;
    int     fd = testp->filefd;

    PRINT_ERR((LOG_DEBUG, "process_read_request: called\n"));
    PRINT_ERR((LOG_DEBUG, "process_read_request: pathhame=%s, offset=%ld, size=%ld\n",pathname, offset,size));    

    /*
     * 既存のfilefdのパス名を確認し、もしこれから読もうとしているファイル
     * と別のパス名だったら新規のFDをオープンし、既存のFDをクローズする。
     */ 
    if(testp->filefd < 0 || ( testp->pathname != NULL && strcmp(testp->pathname, pathname) != 0)){
        if((fd = open(pathname, O_RDONLY)) < 0 ){
            perror("open");
            exit(0);
        }
        PRINT_ERR((LOG_DEBUG, "process_read_request: opened new fd=%d, closing old fd=%d \n",fd, testp->filefd));            
        close(testp->filefd);
        testp->filefd = fd;
        testp->pathname = pathname;
    }
    
    readsize = pread(fd, mapaddr, size, offset);

    PRINT_ERR((LOG_INFO, "process_read_request: pread (%d)\n",readsize));

    if (readsize < 0){
        // TODO: エラー iumfscntl デバイスに通知する方法が無い・・                    
        PRINT_ERR((LOG_DEBUG, "process_read_request: Error happened, close control sessioin\n"));
        close_filefd(testp);
        return(-1);
    } else if (readsize == 0){
        PRINT_ERR((LOG_DEBUG, "Requested offset too large.\n"));
        result = ENOENT;
        write(testp->devfd, &result, sizeof(int));
        return(0);
    }
    
    result = 0;
    write(testp->devfd, &result, sizeof(int));
    return(0);
}

/*****************************************************************************
 * process_getattr_request
 *
 * main() から呼ばれ、GETATTR_REQUEST を処理する
 *
 *  引数：
 *
 *           testp      : testcntl 構造体
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
process_getattr_request(testcntl_t * const testp, char *pathname, caddr_t mapaddr)
{
    struct stat st[1];
    int     result;
    vattr_t *vap;
    int     err = 0;

    PRINT_ERR((LOG_DEBUG, "process_getattr_request: called\n"));

    if ((stat(pathname, st)) < 0){
        if(errno == ENOENT){
            PRINT_ERR((LOG_DEBUG, "process_getattr_request: %s not found\n", pathname));
            result = ENOENT;
            write(testp->devfd, &result, sizeof(int));
            return(0);
        } 
        perror("stat");
        exit(1);
    }
    
    vap = (vattr_t *)mapaddr;

    /*
     * stat 構造体を vatt 構造体にマップ
     */
    if (parse_attributes(vap, st) < 0){        
        err = ENOENT;
        goto done;
    }

    PRINT_ERR((LOG_DEBUG, "process_getattr_request: filesize = %d\n", vap->va_size));    

  done:
    result = err;
    write(testp->devfd, &result, sizeof(int));
    if (err)
        return(-1);
    else
        return(0);
}

/**************************************************************
 * parse_attributes()
 *
 * stat 構造体を vattr 構造体にマップする
 *
 * 引数
 *
 *  vap  : 解析した属性値をセットする vattr 構造体
 *  stat : 対象のローカルファイルの stat 構造体
 *
 * 戻り値
 *
 *    正常時   : 0
 *    エラー時 : -1
 *
 **************************************************************/
int
parse_attributes(vattr_t *vap, struct stat *stat)
{
    time_t timenow;        // 現在時刻がセットされる time 構造体        

    PRINT_ERR((LOG_DEBUG, "parse_attributes: called\n"));

    /*
     * ファイルタイプのチェック
     * 出力結果の最初の一文字目で判断する。
     */
    if(S_ISDIR(stat->st_mode)){
        vap->va_type = VDIR; // ディレクトリ
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VDIR\n"));        
    } else if(S_ISDOOR(stat->st_mode)){
        vap->va_type = VDOOR; // DOOR ファイル
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VDOOR\n"));        
    } else if(S_ISLNK(stat->st_mode)){
        vap->va_type = VLNK; // シンボリックリンク
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VLNK\n"));        
    } else if(S_ISBLK(stat->st_mode)){
        vap->va_type = VBLK; // ブロックデバイス
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VBLK\n"));                
    } else if(S_ISCHR(stat->st_mode)){
        vap->va_type = VCHR; // キャラクタデバイス
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VCHR\n"));        
    } else if(S_ISFIFO(stat->st_mode)){
        vap->va_type = VFIFO; // FIFO
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VFIFO\n"));                
    } else if(S_ISPORT(stat->st_mode)){
        vap->va_type = VPORT; // ...?
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VPORT\n"));                        
    } else if(S_ISSOCK(stat->st_mode)){
        vap->va_type = VSOCK; // ソケット
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VSOCKET\n"));        
    } else {
        vap->va_type = VREG; // 通常ファイル。
        PRINT_ERR((LOG_DEBUG, "parse_attributes: is VREG\n"));                
    }

    /*
     * モードを設定
     */
    vap->va_mode = stat->st_mode;

    /*
     * ファイルサイズを設定
     */
    vap->va_size = stat->st_size;
    
    
    /*
     * mtime, atime, ctime ともに現在の時間をセット
     * (timestruc_t と time_t のマッピングがめんどくさかったので・・・)
     */
    time(&timenow);
    vap->va_mtime.tv_sec = vap->va_atime.tv_sec = vap->va_ctime.tv_sec = timenow;
    
    return(0);
}

