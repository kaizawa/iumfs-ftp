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
 * iumfs_cntl_device.c
 *
 * 擬似ファイルシステム IUMFS とユーザモードデーモンとのデータの
 * 送受を行うための擬似デバイスのデバイスドライバ。
 *
 *  更新履歴:
 *    
 **************************************************************/
#include <sys/modctl.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/kmem.h>
#include <sys/ddi.h>
#include <sys/conf.h> 
#include <sys/sunddi.h>
#include <sys/vfs.h>
#include <sys/mount.h>
#include <sys/dirent.h>
#include <sys/ksynch.h>
#include <sys/pathname.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/uio.h>

#include "iumfs.h"

static int iumfscntl_attach(dev_info_t *dip, ddi_attach_cmd_t cmd);
static int iumfscntl_detach(dev_info_t *dip, ddi_detach_cmd_t cmd);
static int iumfscntl_open(dev_t *devp, int flag, int otyp, cred_t *cred);
static int iumfscntl_close(dev_t dev, int flag, int otyp, cred_t *cred);
static int iumfscntl_read(dev_t dev, struct uio *uiop, cred_t *credp);
static int iumfscntl_write(dev_t dev, struct uio *uiop, cred_t *credp);
static int iumfscntl_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp, int *rvalp);
static int iumfscntl_devmap(dev_t dev, devmap_cookie_t handle, offset_t off, size_t len, size_t *maplen, uint_t model);
static int iumfscntl_poll(dev_t dev, short events, int anyyet, short *reventsp, struct pollhead **phpp);

void *iumfscntl_soft_root = NULL;

struct ddi_device_acc_attr iumfscntl_acc_attr = {
	DDI_DEVICE_ATTR_V0,
	DDI_NEVERSWAP_ACC,
	DDI_STRICTORDER_ACC
};

/*
 * キャラクター用エントリーポイント構造体
 */
static struct cb_ops iumfscntl_cb_ops = {
    iumfscntl_open,          /* cb_open     */
    iumfscntl_close,         /* cb_close    */
    nodev,                   /* cb_strategy */
    nodev,                   /* cb_print    */
    nodev,                   /* cb_dump     */
    iumfscntl_read,          /* cb_read     */
    iumfscntl_write,         /* cb_write    */
    iumfscntl_ioctl,         /* cb_ioctl    */
    iumfscntl_devmap,        /* cb_devmap   */
    nodev,                   /* cb_mmap     */
    nodev,                   /* cb_segmap   */
    iumfscntl_poll,          /* cb_chpoll   */
    ddi_prop_op,             /* cb_prop_op  */
    NULL,                    /* cb_stream   */
    D_MP                     /* cb_flag     */
};

/*
 * デバイスオペレーション構造体
 */
static struct dev_ops iumfscntl_ops = {
    (DEVO_REV),               /* devo_rev      */
    (0),                      /* devo_refcnt   */
    (nodev),                  /* devo_getinfo  */
    (nulldev),                /* devo_identify */
    (nulldev),                /* devo_probe    */
    (iumfscntl_attach),       /* devo_attach   */
    (iumfscntl_detach),       /* devo_detach   */
    (nodev),                  /* devo_reset    */
    &(iumfscntl_cb_ops),      /* devo_cb_ops   */
    (struct bus_ops *)(NULL)  /* devo_bus_ops  */        
};

/*
 * ドライバーのリンケージ構造体
 */
struct modldrv iumfs_modldrv = {
    &mod_driverops,          //  mod_driverops
    "IUMFS control device",  // ドライバの説明
    &iumfscntl_ops           // driver ops   
};

/*****************************************************************************
 * iumfscntl_attach
 *
 * iumfscntl の attach(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_attach(dev_info_t *dip, ddi_attach_cmd_t cmd)
{
    int                instance;
    iumfscntl_soft_t  *cntlsoft = NULL;
    caddr_t            mapaddr = NULL;
    int                size = MMAPSIZE;
    
    DEBUG_PRINT((CE_CONT,"iumfscntl_attach called\n"));
    
    if (cmd != DDI_ATTACH){
        return (DDI_FAILURE);
    }
        
    instance = ddi_get_instance(dip);
    
    /*
     *　構造体を割り当てる
     */
    if(ddi_soft_state_zalloc(iumfscntl_soft_root, instance) != DDI_SUCCESS){
        cmn_err(CE_CONT,"iumfscntl_attach: failed to create minor node\n");
        return (DDI_FAILURE);
    }
    cntlsoft = (iumfscntl_soft_t *)ddi_get_soft_state(iumfscntl_soft_root, instance);    

    /*
     *  mmap(2) でユーザ空間とマッピングを行うメモリーを確保する。
     *  このメモリーをつかって iftpfsd とのデータの受け渡しを行う。
     */
    mapaddr = ddi_umem_alloc(size, DDI_UMEM_NOSLEEP, &cntlsoft->umem_cookie);
    if(mapaddr == NULL){
        cmn_err(CE_CONT,"iumfscntl_attach: failed to allocate umem\n");        
        goto err;
    }

    /*
     * iumfscntl デバイスのステータス構造体を設定
     */
    cntlsoft->instance = instance;    // インスタンス番号
    cntlsoft->mapaddr  = mapaddr;     // ユーザ空間とマッピングを行うメモリアドレス
    cntlsoft->dip      = dip;         // dev_info 構造体
    cntlsoft->size     = size;        // マッピングするメモリのサイズ。今はページサイズ
    mutex_init(&(cntlsoft->d_lock), NULL, MUTEX_DRIVER, NULL);
    mutex_init(&(cntlsoft->s_lock), NULL, MUTEX_DRIVER, NULL);        
    cv_init(&cntlsoft->cv, NULL, CV_DRIVER, NULL);
       
    /*
     * /devicese/pseudo 以下にデバイスファイルを作成する
     * マイナーデバイス番号は 0。
     */
    if(ddi_create_minor_node(dip, "iumfscntl", S_IFCHR, 0, DDI_PSEUDO, 0) == DDI_FAILURE) {
        ddi_remove_minor_node(dip, NULL);
        cmn_err(CE_CONT,"iumfscntl_attach: failed to create minor node\n");
        goto err;
    }
    return (DDI_SUCCESS);

  err:
    if(cntlsoft != NULL){
        mutex_destroy(&cntlsoft->d_lock);
        mutex_destroy(&cntlsoft->s_lock);        
        cv_destroy(&cntlsoft->cv);            
        ddi_soft_state_free(iumfscntl_soft_root, instance);
    }
    if(mapaddr != NULL)
        ddi_umem_free(cntlsoft->umem_cookie);
    
    return (DDI_FAILURE);
}

/*****************************************************************************
 * iumfscntl_dettach
 *
 * iumfscntl の dettach(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_detach(dev_info_t *dip, ddi_detach_cmd_t cmd)
{
    int      instance;
    iumfscntl_soft_t *cntlsoft = NULL;

    DEBUG_PRINT((CE_CONT,"iumfscntl_dettach called\n"));
    
    if (cmd != DDI_DETACH){
        return (DDI_FAILURE);
    }

    instance = ddi_get_instance(dip);
    
    cntlsoft = (iumfscntl_soft_t *)ddi_get_soft_state(iumfscntl_soft_root, instance);

    if (cntlsoft == NULL){
        cmn_err(CE_CONT,"iumfscntl_dettach: \n");
        return(DDI_FAILURE);
    }
    mutex_destroy(&cntlsoft->d_lock);
    mutex_destroy(&cntlsoft->s_lock);    
    cv_destroy(&cntlsoft->cv);
    ddi_umem_free(cntlsoft->umem_cookie);        
    ddi_remove_minor_node(dip, NULL);
    ddi_soft_state_free(iumfscntl_soft_root, instance);

    return(DDI_SUCCESS);
}


/*****************************************************************************
 * iumfscntl_open
 *
 * iumfscntl の open(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_open(dev_t *devp, int flag, int otyp, cred_t *cred)
{
    int               instance;
    iumfscntl_soft_t  *cntlsoft;

    DEBUG_PRINT((CE_CONT,"iumfscntl_open called\n"));    
    if (otyp != OTYP_CHR)
        return (EINVAL);

    instance = getminor(*devp);

    cntlsoft = (iumfscntl_soft_t *)ddi_get_soft_state(iumfscntl_soft_root, instance);

    mutex_enter(&cntlsoft->s_lock);
    if ( cntlsoft->state & IUMFSCNTL_OPENED){
        // すでに /dev/cntlsoft はオープンされている
        mutex_exit(&cntlsoft->s_lock);                
        return(EBUSY);
    }
    cntlsoft->state |= IUMFSCNTL_OPENED;
    mutex_exit(&cntlsoft->s_lock);                    
    
    return(0);
}

/*****************************************************************************
 * iumfscntl_close
 *
 * iumfscntl の close(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_close(dev_t dev, int flag, int otyp, cred_t *cred)
{
    int              instance;
    iumfscntl_soft_t  *cntlsoft;

    DEBUG_PRINT((CE_CONT,"iumfscntl_close called\n"));    
    
    if (otyp != OTYP_CHR)
        return (EINVAL);

    instance = getminor(dev);

    cntlsoft = ddi_get_soft_state(iumfscntl_soft_root, instance);

    if (cntlsoft == NULL)
        return(ENXIO);

    mutex_enter(&cntlsoft->s_lock);
    /*
     * state の DAEMON_INPROGRESS フラグが解除されるのを待っている thread が
     * いるかもしれないので、フラグを解除。
     */
    if(cntlsoft->state & DAEMON_INPROGRESS){
        cntlsoft->state &= ~DAEMON_INPROGRESS;// DAEMON_INPROGRESS フラグを解除
        cntlsoft->state |= MAPDATA_INVALID;   // マップされたアドレスのデータが不正であることを知らせる
        cntlsoft->error = EIO;                // エラーをセット
        cv_broadcast(&cntlsoft->cv);          // thread を起こす
    }
    cntlsoft->state &= ~IUMFSCNTL_OPENED;
    mutex_exit(&cntlsoft->s_lock);                    
    
    return(0);
}
/*****************************************************************************
 * iumfscntl_read
 *
 * iumfscntl の read(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_read(dev_t dev, struct uio *uiop, cred_t *credp)
{
    int                instance;
    iumfscntl_soft_t  *cntlsoft;
    int                err = 0;
    
    DEBUG_PRINT((CE_CONT,"iumfscntl_read called\n"));

    instance = getminor(dev);
    cntlsoft = ddi_get_soft_state(iumfscntl_soft_root, instance);
    if (cntlsoft == NULL)
        return(ENXIO);

    // request 構造体より小さな read 要求は無効
    if(uiop->uio_resid < sizeof(request_t))
        return(EINVAL);

    DEBUG_PRINT((CE_CONT,"iumfscntl_read: waiting for request from iumfs_daemon_request_start..\n"));
    DEBUG_PRINT((CE_CONT,"iumfscntl_read: state = 0x%x\n", cntlsoft->state));

    mutex_enter(&cntlsoft->s_lock);    
    while (!(cntlsoft->state & REQUEST_IS_SET)){
        if(cv_wait_sig(&cntlsoft->cv, &cntlsoft->s_lock) == 0){
            mutex_exit(&cntlsoft->s_lock);
            return(EINTR);
        }
    }
    DEBUG_PRINT((CE_CONT,"iumfscntl_read: data has come. copyout data to user space\n"));    
    err = uiomove(&cntlsoft->req, sizeof(request_t), UIO_READ, uiop);    
    cntlsoft->state &= ~REQUEST_IS_SET;
    mutex_exit(&cntlsoft->s_lock);
    
    return(err);
}

/*****************************************************************************
 * iumfscntl_write
 *
 * iumfscntl の write(9E) ルーチン
 *
 * 呼ばれたら、ユーザプロセスからのデータの確認やコピーなどはせず、単に
 * iumfscntl_soft 構造体のステートフラグに DAEMON_INPROGRESS を解除し、iumfs_bio()
 * の中でまっているであろう thread を起こす。
 *
 * 戻り値
 *
 *    常に 0 成功
 *
 *****************************************************************************/
static int
iumfscntl_write(dev_t dev, struct uio *uiop, cred_t *credp)
{
    int                instance;
    iumfscntl_soft_t  *cntlsoft;
    int                err = 0;
    int                result;
    
    DEBUG_PRINT((CE_CONT,"iumfscntl_write called\n"));    
    
    instance = getminor(dev);
    cntlsoft = ddi_get_soft_state(iumfscntl_soft_root, instance);
    if (cntlsoft == NULL)
        return(ENXIO);

    DEBUG_PRINT((CE_CONT,"iumfscntl_write: get response from daemon. wake iumfs_daemon_request_start\n"));
    DEBUG_PRINT((CE_CONT,"iumfscntl_write: state = 0x%x\n", cntlsoft->state));

    err = uiomove(&result, sizeof(int), UIO_WRITE, uiop);
    
    mutex_enter(&cntlsoft->s_lock);
    if(result == 0 || result == MOREDATA){
        /*
         * マップしているデータは有効
         */
        DEBUG_PRINT((CE_CONT,"iumfscntl_write: daemon reported request was succeeded\n"));
        cntlsoft->error = result;        
    } else {
        /*
         * デーモンがエラーを返してきた
         */
        DEBUG_PRINT((CE_CONT,"iumfscntl_write: daemon reported request was fail\n"));                
        cntlsoft->state |= MAPDATA_INVALID;
        cntlsoft->error = result;
    }

    DEBUG_PRINT((CE_CONT,"iumfscntl_write: state = 0x%x\n", cntlsoft->state));    

    cntlsoft->state &= ~DAEMON_INPROGRESS;   // DAEMON_INPROGRESS フラグを解除
    cv_broadcast(&cntlsoft->cv);             // thread を起こす
    mutex_exit(&cntlsoft->s_lock);

    return(0); // iumfscntl デバイスに対する write は常に成功
}

/*****************************************************************************
 * iumfscntl_ioctl
 *
 * iumfscntl の ioctl(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_ioctl(dev_t dev, int cmd, intptr_t arg, int mode, cred_t *credp, int *rvalp)
{
    DEBUG_PRINT((CE_CONT,"iumfscntl_ioctl called\n"));
    return(EINVAL);    
}

/*****************************************************************************
 * iumfscntl_devmap
 *
 * iumfscntl の devmap(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_devmap(dev_t dev, devmap_cookie_t handle, offset_t off, size_t len, size_t *maplen, uint_t model)
{
    int               instance;
    iumfscntl_soft_t *cntlsoft;
    size_t            length;
    int               err;
    
    DEBUG_PRINT((CE_CONT,"iumfscntl_devmap called\n"));

    instance = getminor(dev);

    cntlsoft = ddi_get_soft_state(iumfscntl_soft_root, getminor(dev));
    if (cntlsoft == NULL)
	return (ENXIO);
    
    length = ptob(btopr(len));
    if (off + length > cntlsoft->size)
	return (-1);
    
    err = devmap_umem_setup(handle, cntlsoft->dip, NULL, cntlsoft->umem_cookie,
                            off, length, PROT_ALL, 0, &iumfscntl_acc_attr);

    if(err != 0){
        cmn_err(CE_CONT,"iumfscntl_devmap: devmap_umem_setup failed (%d)\n", err);
        return(err);
    }
        
    *maplen = length;
    return (0);
}

/*****************************************************************************
 * iumfscntl_poll
 *
 * iumfscntl の chpoll(9E) ルーチン
 *
 *****************************************************************************/
static int
iumfscntl_poll(dev_t dev, short events, int anyyet, short *reventsp, struct pollhead **phpp)
{
    int                instance;
    iumfscntl_soft_t  *cntlsoft;
    short              revent;
    
    DEBUG_PRINT((CE_CONT,"iumfscntl_poll called\n"));

    instance = getminor(dev);
    cntlsoft = ddi_get_soft_state(iumfscntl_soft_root, instance);
    if (cntlsoft == NULL)
        return(ENXIO);

    revent = 0;
    /*
     * 有効なイベント
     * POLLIN | POLLOUT | POLLPRI | POLLHUP | POLLERR
     * 現在は POLLIN | POLLRDNORM と POLLERR|POLLRDBAND しかサポートしていない。
     */
    if ((events & (POLLIN|POLLRDNORM)) && (cntlsoft->state & REQUEST_IS_SET)) {
        DEBUG_PRINT((CE_CONT,"iumfscntl_poll: request can be read\n"));        
        revent |= POLLIN|POLLRDNORM;
    }
    if ((events & (POLLERR| POLLRDBAND)) && (cntlsoft->state & REQUEST_IS_CANCELED)) {
        DEBUG_PRINT((CE_CONT,"iumfscntl_poll: request is canceled\n"));        
        revent |= (POLLERR |POLLRDBAND);
        /*
         * 現在は iumfs_daemon_request_exit() でフラグを解除している。
         * cntlsoft->state &= ~REQUEST_IS_CANCELED;
         */
    }
    /*
     * 通知すべきイベントは発生していない
     */ 
    if (revent == 0) {
        DEBUG_PRINT((CE_CONT,"iumfscntl_poll: no event happened.\n"));        
        if (!anyyet) {
            *phpp = &cntlsoft->pollhead;
        }
    }
    
    *reventsp = revent;
    return (0);    
}
