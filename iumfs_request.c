/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1986, 2010, Oracle and/or its affiliates. All rights reserved.
 */

/*
 * Copright (c) 2005-2010  Kazuyoshi Aizawa <admin2@whiteboard.ne.jp>
 * All rights reserved.
 */
/**************************************************************
 * iumfs_request
 *
 * ユーザモードデーモンにリクエスト（データ要求）するための
 * ルーチンが書かれたモジュール。現在デーモンに依頼できるのは
 * 以下のリクエスト
 *
 *     iumfs_request_read()    ... ファイルのデータを読む
 *     iumfs_request_readdir() ... ディレクトリエントリを読む
 *     iumfs_request_getattr() ... ファイルの属性値を得る 
 *     iumfs_request_lookup()  ... ファイルの有無を確認
 *
 *  各リクエストのルーチンは必ず以下の関数を順番どおりに
 *  呼び、複数のリクエストが同時に実行されないことを保証している。
 *
 *     iumfs_daemon_request_enter() .. リクエストの順番待ちをする 
 *     iumfs_daemon_request_start() .. リクエストを投げる
 *     iumfs_daemon_request_exit()  .. リクエストを終了する
 *
 *
 * 変更履歴：
 *
 **************************************************************/

#include <sys/modctl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <sys/dirent.h>
#include <sys/ksynch.h>
#include <sys/pathname.h>
#include <sys/file.h>

#include <vm/seg.h>
#include <vm/page.h>
#include <vm/pvn.h>
#include <vm/seg_map.h>
#include <vm/seg_vn.h>
#include <vm/hat.h>
#include <vm/as.h>
#include <vm/seg_kmem.h>

#include "iumfs.h"

extern  void *iumfscntl_soft_root;

/******************************************************************
 * iumfs_request_read()
 *
 * iumfs_getapage() から呼ばれ、ユーザモードデーモンに指定した
 * ファイルのオフセットからサイズ分のデータを要求する。
 *
 * 引数:
 *        bp  : buf 構造体
 *        vp  : 読み込むファイルの vnode 
 *
 * 戻り値
 *
 *    正常時   : 0
 *    エラー時 : エラー番号
 *
 *****************************************************************/
int
iumfs_request_read(struct buf *bp, vnode_t *vp)
{
    iumfscntl_soft_t   *cntlsoft;      // iumfscntl デバイスのデバイスステータス構造体
    int                instance = 0 ;  // いまのところ固定値
    caddr_t            mapaddr;
    request_t          *rreq;
    offset_t           offset;
    size_t             size;
    iumfs_t            *iumfsp;       // ファイルシステム型依存のプライベートデータ構造体
    iumnode_t          *inp;
    iumfs_mount_opts_t *mountopts;
    int                 err;
    offset_t           loffset;
    size_t             lsize;
    size_t             leftsize;
    
    DEBUG_PRINT((CE_CONT,"iumfs_request_read called\n"));

    cntlsoft = (iumfscntl_soft_t *)ddi_get_soft_state(iumfscntl_soft_root, instance);

    /*
     * block 数から byte 数へ・・・なんか無意味な操作
     */
    offset = ldbtob(bp->b_lblkno);
    size = bp->b_bcount;

    DEBUG_PRINT((CE_CONT,"iumfs_request_read: offset = %D, size = %d\n", offset, size));

    /*
     * リクエストの順番待ちをする    
     */
    err = iumfs_daemon_request_enter(cntlsoft);
    if(err)
        return(err);


    /*
     *  b_bcount は PAGESIZE より大きい可能性があるが、デーモンにリクエスト
     *  を行う際には PAGESIZE 単位にしないといけない（勝手ルール）ので、
     *  PAGESIZE 毎にリクエストをあげる。
     */
    leftsize = size;    
    loffset = offset;
    lsize = MIN(PAGESIZE, size);
    do {
        mutex_enter(&cntlsoft->d_lock);        
        /*
         * マウントオプション、ファイルシステム依存ノード構造体、ユーザ空間とマッピング
         * しているメモリアドレスを得る。
         */ 
        inp       = VNODE2IUMNODE(vp);
        iumfsp    = VNODE2IUMFS(vp);
        mountopts = iumfsp->mountopts;
        mapaddr   = cntlsoft->mapaddr;
        rreq      = &cntlsoft->req; 
        /*
         * ユーザモードデーモンに渡すリクエストを request 構造体にセット
         */
        bzero(mapaddr, MMAPSIZE);        
        rreq->request_type = READ_REQUEST;
        strncpy(rreq->pathname,inp->pathname, MAXPATHLEN);  // マウントポイントからの相対パス名
        bcopy(mountopts, rreq->mountopts, sizeof(iumfs_mount_opts_t));
        rreq->data.read_request.offset = loffset; // オフセット    
        rreq->data.read_request.size   = lsize;   // サイズ
        mutex_exit(&cntlsoft->d_lock);
    
        /*
         * リクエスト要求を開始する
         */
        err = iumfs_daemon_request_start(cntlsoft);
        if (err){
            /*
             * エラーが発生した模様。リクエストを解除してエラーをリターン
             */
            iumfs_daemon_request_exit(cntlsoft);
            return(err);
        }    

        /*
         * デーモンから受け取ったデータをコピー
         */
        mutex_enter(&cntlsoft->d_lock);
        bcopy(mapaddr, bp->b_un.b_addr + (loffset - offset), lsize);
        mutex_exit(&cntlsoft->d_lock);

        loffset += lsize;
        leftsize -= lsize;
        lsize  = MIN(PAGESIZE, leftsize);
    } while (leftsize > 0);

    /*
     * リクエストを解除。他の待ち thread を起こす
     */
    iumfs_daemon_request_exit(cntlsoft);
    
    DEBUG_PRINT((CE_CONT,"iumfs_request_read: copy data done\n"));            
    
    return(0);
}

/******************************************************************
 * iumfs_request_readdir()
 *
 * iumfs_readdir() から呼ばれ、ユーザモードデーモンに指定した
 * ディレクトリ内のエントリのリストを要求する。
 *
 * 引数:
 *        dirinp : リストを要求するディレクトリの vnode 構造体
 *
 * 戻り値
 *
 *   正常時   : 0
 *   エラー時 : エラー番号
 * 
 *****************************************************************/
int
iumfs_request_readdir(vnode_t *dirvp)
{
    iumfscntl_soft_t   *cntlsoft;      // iumfscntl デバイスのデバイスステータス構造体
    int                 instance = 0 ; // いまのところ固定値
    caddr_t             mapaddr;
    char               *list;          // デーモンから返ってきたエントリのリスト
    request_t          *dreq;          // リクエスト構造体
    size_t              namelen;       // 見つかったディレクトリエントリ名の長さ
    iumnode_t          *dirinp;        // ディレクトリのファイルシステム依存ノード構造体
    int                 err;           
    char               *readp;         // ? 処理用ポインタ
    iumfs_mount_opts_t *mountopts;     // マウントオプション
    iumfs_t            *iumfsp;        // ファイルシステム型依存のプライベートデータ構造体
    offset_t            offset = 0;
    
    DEBUG_PRINT((CE_CONT,"iumfs_request_readdir called\n"));

    cntlsoft = (iumfscntl_soft_t *)ddi_get_soft_state(iumfscntl_soft_root, instance);


    // リクエストの順番待ちをする
    err = iumfs_daemon_request_enter(cntlsoft);
    if(err)
        return(err);

  readagain:    
    mutex_enter(&cntlsoft->d_lock);    
    /*
     * マウントオプション、ファイルシステム依存ノード構造体、ユーザ空間とマッピング
     * しているメモリアドレスを得る。
     */
    dirinp    = VNODE2IUMNODE(dirvp);    
    iumfsp    = VNODE2IUMFS(dirvp);
    mountopts = iumfsp->mountopts;
    mapaddr   = cntlsoft->mapaddr;
    dreq      = &cntlsoft->req;

    /*
     * ユーザモードデーモンに渡すリクエストを request 構造体にセット
     */
    bzero(mapaddr, MMAPSIZE);
    dreq->request_type = READDIR_REQUEST;
    dreq->data.read_request.offset = offset; // オフセット
    dreq->data.read_request.size   = MMAPSIZE; // オフセット        
    strncpy(dreq->pathname,dirinp->pathname, MAXPATHLEN);  // マウントポイントからの相対パス名
    bcopy(mountopts, dreq->mountopts, sizeof(iumfs_mount_opts_t));
    mutex_exit(&cntlsoft->d_lock);

    DEBUG_PRINT((CE_CONT,"iumfs_request_readdir: offset = %D\n", offset));    
    /*
     * リクエスト要求を開始する
     */
    err = iumfs_daemon_request_start(cntlsoft);

    if (err && err != MOREDATA){
        /*
         * エラーが発生した模様。リクエストを解除してエラーリターン
         */
        iumfs_daemon_request_exit(cntlsoft);
        return(err);
    }
    
    /*
     * 正常にデータを取得できた模様
     * データをコピーする。
     */
    mutex_enter(&cntlsoft->d_lock);
    list = (char *)cntlsoft->mapaddr;
    readp = list;
    while (readp[0] != NULL){
        namelen = strlen(readp);
        /*
         * もしディレクトリに既存エントリが無ければ
         * ノード番号 0 で読み込んだエントリを追加
         */
        if(!iumfs_directory_entry_exist(dirvp, readp))
            iumfs_add_entry_to_dir(dirvp, readp, namelen, 0);
        readp += namelen + 2;
        /*
         * わざと、MMAPSIZE から MAXNAMELEN 分だけ残して
         * 再度オフセットを設定しなおして READDIR リクエストを
         * 投げる。こうすることで、MMAPSIZE の境界上にあるエントリ
         * 名を確実に得ようとしている。
         */
        if((MMAPSIZE - (readp - list)) < MAXNAMELEN){
            offset = offset + (readp - list);
            break;
        }
    }
    mutex_exit(&cntlsoft->d_lock);

    if(err == MOREDATA && offset > 0)
        goto readagain;

    /*
     * リクエストを解除。他の待ち thread を起こす
     */
    iumfs_daemon_request_exit(cntlsoft);

    DEBUG_PRINT((CE_CONT,"iumfs_request_readdir: successfully copied data from daemon\n"));            
    
    return(0);
}

/******************************************************************
 * iumfs_daemon_request_enter
 *
 * ユーザモードデーモンへのリクエスト要求を開始するための順番待ちをする。
 * 他の thread がリクエストを要求中であれば、この関数の中で待たされる。
 *
 * 引数:
 *        cntlsoft : iumfscntl デバイスのデバイスステータス構造体
 *
 * 戻り値
 *
 *    正常時: 0
 *    異常時: エラー番号
 * 
 *****************************************************************/
int
iumfs_daemon_request_enter(iumfscntl_soft_t  *cntlsoft)
{
    int                err = 0;
    
    DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_enter called\n"));

    /*
     * すでに他の thread がデーモンへのリクエストを実行中だったら、その処理が終わるまで待つ。
     * もし、誰もほかにリクエストを実行中でなかったら REQUEST_INPROGRESS フラグを立てて
     * 処理を進める。（他の thread が同時に実行されないことを保障する）
     */
    mutex_enter(&cntlsoft->s_lock);    
    while (cntlsoft->state & REQUEST_INPROGRESS){
        if(cv_wait_sig(&cntlsoft->cv, &cntlsoft->s_lock) == 0){
            mutex_exit(&cntlsoft->s_lock);
            err = EINTR;
            DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_enter returned(%d)\n",err));            
            return(err);
        }
    }
    cntlsoft->state |= REQUEST_INPROGRESS;
    mutex_exit(&cntlsoft->s_lock);
    DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_enter returned(0)\n"));

    return(0);
}

/******************************************************************
 * iumfs_daemon_request_start
 *
 * ユーザモードデーモンへリクエストを要求する。
 * この関数は最初に iumfs_daemon_request_enter() 呼び出してリターンし
 * てきてから、つまり排他的に呼ばれなくてはならない。
 *
 * 引数:
 *        cntlsoft : iumfscntl デバイスのデバイスステータス構造体
 *
 * 戻り値
 *     正常時 : 0 
 *     異常時 : エラー番号
 * 
 *****************************************************************/
int
iumfs_daemon_request_start(iumfscntl_soft_t  *cntlsoft)
{
    int                err;
    
    DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_start called\n"));

    /*
     * iumfscntl_read() 内で待っている thread を cv_broadcast で起こす。
     */
    mutex_enter(&cntlsoft->s_lock);
    cntlsoft->state |= REQUEST_IS_SET;    // REQUEST_IS_SET フラグを立てる
    cntlsoft->state |= DAEMON_INPROGRESS; // DAEMON_IN_PROGRESS フラグを立てる    
    cv_broadcast(&cntlsoft->cv);          // thread を起こす
    mutex_exit(&cntlsoft->s_lock);

    /*
     * poll(2) を起こす
     */
    DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_start: wakeup poll\n"));    
    pollwakeup(&cntlsoft->pollhead, POLLIN|POLLRDNORM);
    
    /*
     * ユーザモードデーモンの書き込み終了を待つ
     */
    mutex_enter(&cntlsoft->s_lock);    
    DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_start: waiting for data from daemon\n"));    
    while (cntlsoft->state & DAEMON_INPROGRESS){
        if(cv_wait_sig(&cntlsoft->cv, &cntlsoft->s_lock) == 0){
            /*
             * 割り込みを受けた。 EINTR を返す。
             * daemon に対しても POLLERR | POLLRDBAND で通知する。
             */
            DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_start: interrupt recieved.\n"));
            cntlsoft->state |= REQUEST_IS_CANCELED;
            mutex_exit(&cntlsoft->s_lock);
            pollwakeup(&cntlsoft->pollhead, POLLERR|POLLRDBAND);
            return(EINTR);
        }
    }
    /*
     * DAEMON_INPROGRESS フラグの解除を待っている thread は他にはいないはずなので、
     * cv_broadcase() は呼ばない。（というか意味が無い）
     * 逆に、この thread を起こしてくれるのは iumfscntl デバイスドライバの
     * iumfscntl_close() と iumfscntl_write() だけ。
     */
    DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_start: data has come from daemon\n"));        

    if(cntlsoft->state & MAPDATA_INVALID){
        /*
         * デーモンが死んだ、もしくはエラーを返してきた。
         */
        DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_start: mmap data is invalid. state = 0x%x\n",
                     cntlsoft->state));
    }
    err = cntlsoft->error;    
    mutex_exit(&cntlsoft->s_lock);    

    return(err);
}

/******************************************************************
 * iumfs_daemon_request_exit
 *
 * リクエスト要求の完了処理をする。
 * 他のリクエスト要求待ちをしている thread を起こす。
 *
 * 引数:
 *        cntlsoft : iumfscntl デバイスのデバイスステータス構造体
 *
 * 戻り値
 *        無し
 * 
 *****************************************************************/
void
iumfs_daemon_request_exit(iumfscntl_soft_t  *cntlsoft)
{
    
    DEBUG_PRINT((CE_CONT,"iumfs_daemon_request_exit called\n"));

    /*
     * 無用なフラグをはずし、他の thread を起こす。
     */
    mutex_enter(&cntlsoft->s_lock);
    cntlsoft->state &= ~(REQUEST_INPROGRESS|MAPDATA_INVALID|REQUEST_IS_SET|REQUEST_IS_CANCELED); 
    cv_broadcast(&cntlsoft->cv);   // thread を起こす    
    mutex_exit(&cntlsoft->s_lock);

    return;
}

/******************************************************************
 * iumfs_request_lookup
 *
 * iumfs_lookup() から呼ばれ、ユーザモードデーモンに指定した
 * ディレクトリ内のファイルの存在を確認をし、かつ属性値を得る。
 *
 *
 * 引数:
 *        drivp : 検索するディレクトリの vnode 構造体
 *        name  : 確認するファイルのファイルシステムルートからのパス名
 *        vap   : 正常終了した場合、属性値を格納する vattr 構造体
 *
 * 戻り値
 *     正常時 : 0
 *     異常時 : エラー番号
 * 
 *****************************************************************/
int 
iumfs_request_lookup(vnode_t *dirvp, char *pathname, vattr_t *vap)
{
    iumfscntl_soft_t   *cntlsoft;      // iumfscntl デバイスのデバイスステータス構造体
    int                instance = 0 ;  // いまのところ固定値
    caddr_t            mapaddr;
    request_t          *req;
    iumfs_t            *iumfsp;        // ファイルシステム型依存のプライベートデータ構造体
    iumfs_mount_opts_t *mountopts;
    int                 err;
    
    DEBUG_PRINT((CE_CONT,"iumfs_request_lookup called\n"));

    cntlsoft = (iumfscntl_soft_t *)ddi_get_soft_state(iumfscntl_soft_root, instance);

    /*
     * リクエストの順番待ちをする    
     */
    err = iumfs_daemon_request_enter(cntlsoft);
    if (err)
        return(err);

    mutex_enter(&cntlsoft->d_lock);    
    /*
     * マウントオプション、ファイルシステム依存ノード構造体、ユーザ空間とマッピング
     * しているメモリアドレスを得る。
     */ 
    iumfsp    = VNODE2IUMFS(dirvp);
    mountopts = iumfsp->mountopts;
    mapaddr   = cntlsoft->mapaddr;
    req       = &cntlsoft->req;    
     /*
     * ユーザモードデーモンに渡すリクエストを request 構造体にセット
     */
    bzero(mapaddr, MMAPSIZE);        
    req->request_type = GETATTR_REQUEST; // LOOKUP だが、中身は GETATTR と同じ
    snprintf(req->pathname, MAXPATHLEN, "%s", pathname); //マウントポイントからのパス名
    bcopy(mountopts, req->mountopts, sizeof(iumfs_mount_opts_t));
    mutex_exit(&cntlsoft->d_lock);
    
    /*
     * リクエスト要求を開始する
     */
    err = iumfs_daemon_request_start(cntlsoft);
    if (err){
        /*
         * エラーが発生した模様。リクエストを解除してエラーリターン
         */
        iumfs_daemon_request_exit(cntlsoft);
        return(err);
    }    

    /*
     * デーモンから受け取ったデータをコピー
     */
    mutex_enter(&cntlsoft->d_lock);
    bcopy(mapaddr, vap, sizeof(vattr_t));
    mutex_exit(&cntlsoft->d_lock);

    /*
     * リクエストを解除。他の待ち thread を起こす
     */
    iumfs_daemon_request_exit(cntlsoft);
    
    DEBUG_PRINT((CE_CONT,"iumfs_request_lookup: copy data done\n"));            
    
    return(0);    
    
}

/******************************************************************
 * iumfs_request_getattr
 *
 * iumfs_lookup() から呼ばれ、ユーザモードデーモンに指定した
 * ディレクトリ内のファイルの属性情報を得る。
 *
 *
 * 引数:
 *        vp    : 属性値変更するファイルの vnode 
 *
 * 戻り値
 *     正常時 : 0
 *     異常時 : エラー番号
 * 
 *****************************************************************/
int
iumfs_request_getattr(vnode_t *vp)
{
    iumfscntl_soft_t   *cntlsoft;      // iumfscntl デバイスのデバイスステータス構造体
    int                instance = 0 ;  // いまのところ固定値
    caddr_t            mapaddr;
    request_t          *req;
    iumfs_t            *iumfsp;        // ファイルシステム型依存のプライベートデータ構造体
    iumnode_t          *inp;
    iumfs_mount_opts_t *mountopts;
    int                 err;
    vattr_t            *vap;
    
    DEBUG_PRINT((CE_CONT,"iumfs_request_getattr called\n"));

    cntlsoft = (iumfscntl_soft_t *)ddi_get_soft_state(iumfscntl_soft_root, instance);

    /*
     * リクエストの順番待ちをする    
     */
    err = iumfs_daemon_request_enter(cntlsoft);
    if (err)
        return(err);

    mutex_enter(&cntlsoft->d_lock);    
    /*
     * マウントオプション、ファイルシステム依存ノード構造体、ユーザ空間とマッピング
     * しているメモリアドレスを得る。
     */ 
    inp       = VNODE2IUMNODE(vp);
    iumfsp    = VNODE2IUMFS(vp);
    mountopts = iumfsp->mountopts;
    mapaddr   = cntlsoft->mapaddr;    
    req       = &cntlsoft->req; 
    /*
     * ユーザモードデーモンに渡すリクエストを request 構造体にセット
     */
    bzero(mapaddr, MMAPSIZE);        
    req->request_type = GETATTR_REQUEST; 
    snprintf(req->pathname, MAXPATHLEN, "%s", inp->pathname); //マウントポイントからの相対パス
    bcopy(mountopts, req->mountopts, sizeof(iumfs_mount_opts_t));
    mutex_exit(&cntlsoft->d_lock);
    
    /*
     * リクエスト要求を開始する
     */
    err = iumfs_daemon_request_start(cntlsoft);
    if (err){
        /*
         * エラーが発生した模様。リクエストを解除してエラーリターン
         */
        iumfs_daemon_request_exit(cntlsoft);
        return(err);
    }    

    vap = (vattr_t *) mapaddr;
    
    /*
     * デーモンから受け取ったデータをコピー
     * モード、サイズ、タイプ、更新時間のみ。
     */
    mutex_enter(&cntlsoft->d_lock);
    mutex_enter(&(inp->i_lock));
    inp->vattr.va_mode = vap->va_mode;
    inp->vattr.va_size = vap->va_size;
    inp->vattr.va_type = vap->va_type;
    inp->vattr.va_mtime = vap->va_mtime;
    mutex_exit(&(inp->i_lock));        
    mutex_exit(&cntlsoft->d_lock);

    /*
     * リクエストを解除。他の待ち thread を起こす
     */
    iumfs_daemon_request_exit(cntlsoft);
    
    DEBUG_PRINT((CE_CONT,"iumfs_request_getattr: copy data done\n"));            
    
    return(0);    
}
