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
 * iumfs_vnode
 *
 * 擬似ファイルシステム IUMFS の VNODE オペレーションの為のコード
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
#include <sys/uio.h>
#include <sys/vm.h>
#include <sys/exec.h>
// OpenSolaris の場合必要
#ifdef OPENSOLARIS
#include <sys/vfs_opreg.h>
#endif

#include "iumfs.h"

/* VNODE 操作プロトタイプ宣言 */
#ifdef SOL10
static int     iumfs_open   (vnode_t **, int , struct cred *);
static int     iumfs_close  (vnode_t *, int , int ,offset_t , struct cred *);
static int     iumfs_read   (vnode_t *, struct uio *, int , struct cred *);
static int     iumfs_getattr(vnode_t *, vattr_t *, int ,  struct cred *);
static int     iumfs_access (vnode_t *, int , int , struct cred *);
static int     iumfs_lookup (vnode_t *, char *, vnode_t **,  struct pathname *,int ,vnode_t *, struct cred *);
static int     iumfs_readdir(vnode_t *, struct uio *,struct cred *, int *);
static int     iumfs_fsync  (vnode_t *, int , struct cred *);
static void    iumfs_inactive(vnode_t *, struct cred *);
static int     iumfs_seek   (vnode_t *, offset_t , offset_t *);
static int     iumfs_cmp    (vnode_t *, vnode_t *);
static int     iumfs_getpage(vnode_t *, offset_t , size_t ,uint_t *, struct page **,
                             size_t ,struct seg *, caddr_t , enum seg_rw ,struct cred *);
static int     iumfs_putpage(vnode_t *, page_t *, u_offset_t *, size_t *, int, struct cred *);
static int     iumfs_map    (vnode_t *, offset_t , struct as *,caddr_t *, size_t ,
                             uchar_t ,uchar_t , uint_t , struct cred *);
#else
static int     iumfs_write  (vnode_t *, struct uio *, int , struct cred *);
static int     iumfs_ioctl  (vnode_t *, int , intptr_t , int ,  struct cred *, int *);
static int     iumfs_setfl  (vnode_t *, int , int , struct cred *);
static int     iumfs_setattr(vnode_t *, vattr_t *, int ,struct cred *);
static int     iumfs_create (vnode_t *, char *, vattr_t *,  vcexcl_t , int ,
                             vnode_t **,struct cred *, int );
static int     iumfs_remove (vnode_t *, char *, struct cred *);
static int     iumfs_link   (vnode_t *, vnode_t *, char *,  struct cred *);
static int     iumfs_rename (vnode_t *, char *,  vnode_t *, char *, struct cred *);
static int     iumfs_mkdir  (vnode_t *, char *,  vattr_t *, vnode_t **, struct cred *);
static int     iumfs_rmdir  (vnode_t *, char *, vnode_t *,struct cred *);
static int     iumfs_symlink(vnode_t *, char *, vattr_t *, char *,struct cred *);
static int     iumfs_readlink(vnode_t *, struct uio *,struct cred *);
static int     iumfs_fid    (vnode_t *, struct fid *);
static void    iumfs_rwlock (vnode_t *, int );
static void    iumfs_rwunlock(vnode_t *, int );
static int     iumfs_frlock (vnode_t *, int , struct flock64 *,   int , offset_t ,
                             struct flk_callback *, struct cred *);
static int     iumfs_space  (vnode_t *, int , struct flock64 *,int , offset_t , struct cred *);
static int     iumfs_realvp (vnode_t *, vnode_t **);
static int     iumfs_addmap (vnode_t *, offset_t , struct as *,caddr_t , size_t ,
                             uchar_t ,  uchar_t , uint_t , struct cred *);
static int     iumfs_delmap (vnode_t *, offset_t , struct as *,caddr_t , size_t ,
                             uint_t ,uint_t , uint_t , struct cred *);
static int     iumfs_poll   (vnode_t *, short , int , short *,struct pollhead **);
static int     iumfs_dump   (vnode_t *, caddr_t , int ,  int );
static int     iumfs_pathconf(vnode_t *, int , ulong_t *,struct cred *);
static int     iumfs_pageio (vnode_t *, struct page *,u_offset_t , size_t , int ,  struct cred *);
static int     iumfs_dumpctl(vnode_t *, int , int *);
static void    iumfs_dispose(vnode_t *, struct page *, int ,int , struct cred *);
static int     iumfs_setsecattr(vnode_t *, vsecattr_t *, int ,struct cred *);
static int     iumfs_getsecattr(vnode_t *, vsecattr_t *, int ,struct cred *);
static int     iumfs_shrlock (vnode_t *, int , struct shrlock *,int );
#endif // idfef SOL10

static int     iumfs_getapage(vnode_t *, u_offset_t , size_t ,uint_t *, struct page *[],
                             size_t ,struct seg *, caddr_t , enum seg_rw ,struct cred *);
int            iumfs_putapage(vnode_t *, page_t *, u_offset_t *, size_t *, int, struct cred *);



/*
 * このファイルシステムでサーポートする vnode オペレーション
 */
#ifdef SOL10
/*
 * Solaris 10 の場合、vnodeops 構造体は vfs_setfsops() にて得る。
 * OpenSolaris の場合さらに fs_operation_def_t の func メンバが union に代わっている
 */
struct vnodeops *iumfs_vnodeops;
#ifdef OPENSOLARIS
fs_operation_def_t iumfs_vnode_ops_def_array[] = {
    { VOPNAME_OPEN,    {&iumfs_open    }},
    { VOPNAME_CLOSE,   {&iumfs_close   }},
    { VOPNAME_READ,    {&iumfs_read    }},
    { VOPNAME_GETATTR, {&iumfs_getattr }},
    { VOPNAME_ACCESS,  {&iumfs_access  }},
    { VOPNAME_LOOKUP,  {&iumfs_lookup  }},
    { VOPNAME_READDIR, {&iumfs_readdir }},
    { VOPNAME_FSYNC,   {&iumfs_fsync   }},
    { VOPNAME_INACTIVE,{(fs_generic_func_p)&iumfs_inactive}},
    { VOPNAME_SEEK,    {&iumfs_seek    }},
    { VOPNAME_CMP,     {&iumfs_cmp     }},
    { VOPNAME_GETPAGE, {&iumfs_getpage }},    
    { VOPNAME_PUTPAGE, {&iumfs_putpage }},
    { VOPNAME_MAP,     {(fs_generic_func_p)&iumfs_map }},        
    { NULL, {NULL}},
};
#else
fs_operation_def_t iumfs_vnode_ops_def_array[] = {
    { VOPNAME_OPEN,    &iumfs_open    },
    { VOPNAME_CLOSE,   &iumfs_close   },
    { VOPNAME_READ,    &iumfs_read    },
    { VOPNAME_GETATTR, &iumfs_getattr },
    { VOPNAME_ACCESS,  &iumfs_access  },
    { VOPNAME_LOOKUP,  &iumfs_lookup  },
    { VOPNAME_READDIR, &iumfs_readdir },
    { VOPNAME_FSYNC,   &iumfs_fsync   },
    { VOPNAME_INACTIVE,(fs_generic_func_p)&iumfs_inactive},
    { VOPNAME_SEEK,    &iumfs_seek    },
    { VOPNAME_CMP,     &iumfs_cmp     },
    { VOPNAME_GETPAGE, &iumfs_getpage },    
    { VOPNAME_PUTPAGE, &iumfs_putpage },
    { VOPNAME_MAP,     (fs_generic_func_p)&iumfs_map },        
    { NULL, NULL},
};
#endif // ifdef OPENSOLARIS

#else
/*
 * Solaris 9 の場合、vnodeops 構造体はファイルシステムが領域を確保し、直接参照できる
 */
struct vnodeops iumfs_vnodeops = {
    &iumfs_open,
    &iumfs_close,
    &iumfs_read,
    &iumfs_write, 
    &iumfs_ioctl,
    &iumfs_setfl,
    &iumfs_getattr,
    &iumfs_setattr,
    &iumfs_access, 
    &iumfs_lookup, 
    &iumfs_create, 
    &iumfs_remove, 
    &iumfs_link,   
    &iumfs_rename, 
    &iumfs_mkdir,  
    &iumfs_rmdir,  
    &iumfs_readdir,
    &iumfs_symlink,
    &iumfs_readlink,
    &iumfs_fsync,  
    &iumfs_inactive,
    &iumfs_fid,    
    &iumfs_rwlock, 
    &iumfs_rwunlock,
    &iumfs_seek,   
    &iumfs_cmp,    
    &iumfs_frlock, 
    &iumfs_space,  
    &iumfs_realvp, 
    &iumfs_getpage,
    &iumfs_putpage,
    &iumfs_map,    
    &iumfs_addmap, 
    &iumfs_delmap, 
    &iumfs_poll,   
    &iumfs_dump,   
    &iumfs_pathconf,
    &iumfs_pageio, 
    &iumfs_dumpctl,
    &iumfs_dispose,
    &iumfs_setsecattr,
    &iumfs_getsecattr,
    &iumfs_shrlock
};
#endif // ifdef SOL10

extern  void *iumfscntl_soft_root;

/************************************************************************
 * iumfs_open()  VNODE オペレーション
 *
 * open(2) システムコールに対応。
 * ここではほとんど何もしない。
 *************************************************************************/
static int
iumfs_open (vnode_t **vpp, int flag, struct cred *cr)
{
    vnode_t      *vp;

    DEBUG_PRINT((CE_CONT,"iumfs_open is called\n"));
    DEBUG_PRINT((CE_CONT,"iumfs_open: vpp = 0x%p, vp = 0x%p\n", vpp, *vpp));

    vp = *vpp;

    if(vp == NULL){
        DEBUG_PRINT((CE_CONT,"iumfs_open: vnode is null\n"));
        return(EINVAL);
    }
    
    return(SUCCESS);
}

/************************************************************************
 * iumfs_close()  VNODE オペレーション
 *
 * 常に 成功
 *************************************************************************/
static int
iumfs_close  (vnode_t *vp, int flag, int count,offset_t offset, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_close is called\n"));
    
    return(SUCCESS);
}

/************************************************************************
 * iumfs_read()  VNODE オペレーション
 *
 * read(2) システムコールに対応する。
 * ファイルのデータを uiomove を使って、ユーザ空間のアドレスにコピーする。
 *************************************************************************/
static int
iumfs_read(vnode_t *vp, struct uio *uiop, int ioflag, struct cred *cr)
{
    iumnode_t    *inp;
    int           err   = 0;
    caddr_t       base  = 0;     // vnode とマップされたカーネル空間のアドレス
    offset_t      mapoff = 0 ;   // block の境界線までのオフセット値
    offset_t      reloff = 0;    // block の境界線からの相対的なオフセット値
    size_t        mapsz = 0;     // マップするサイズ
    size_t        rest  = 0;     // ファイルサイズと要求されているオフセット値との差
    uint_t        flags = 0;    


    DEBUG_PRINT((CE_CONT,"iumfs_read is called\n"));

    // ファイルシステム型依存のノード構造体を得る
    inp = VNODE2IUMNODE(vp);

    mutex_enter(&(inp->i_lock));

    if(!(inp->vattr.va_type | VREG)){
        DEBUG_PRINT((CE_CONT,"iumfs_read: file is not regurar file\n"));
	err = ENOTSUP;
	goto done;
    }


    do {
        /*
         *   uio 構造体の loffset/resid と各ローカル変数の関係
         *   (MAXBSIZE は 8192)
         *   
         *   | MAXBSIZE | MAXBSIZE | MAXBSIZE | MAXBSIZE | MAXBSIZE | MAXBSIZE | MAXBSIZE |-
         *   |----------|----------|----------|----------|----------|----------|----------|-
         *   |--------------------- File size -------------------------------------->|
         *   |<--------- uiop->loffset ----------->|<-- uiop->resid----->|
         *   |<---------- mapoff ------------>|
         *                                    |<-->|<--->|
         *                                    reloff mapsz
         *                                         |<---------- rest --------------->|
         *
         *   一回の segmap_getmapflt でマップ できるのは MAXBSIZE 分だけなので、
         *   uiop->resid 分だけマップするために繰り返し segmap_getmapflt を呼ぶ
         *   必要がある。
         */
        mapoff = uiop->uio_loffset & MAXBMASK;
        reloff = uiop->uio_loffset & MAXBOFFSET;
        mapsz  = MAXBSIZE - reloff;
	rest   = inp->vattr.va_size - uiop->uio_loffset;
	/*
         * もし要求されているオフセット値からの残りのサイズが 0 以下だった場合はリターンする。 
         */
	if (rest <= 0)
            goto done;
	/*
         * もし mapsz がファイルの残りのサイズ(rest) よりも大きかったら rest を mapsz とする。
         */        
        mapsz = MIN(mapsz, rest);
        
        /*
         * もし resid が mapsz より小さければ（つまり最後のマッピング処理の場合）
         * 、resid を mapsz とする。
         */
        mapsz = MIN(mapsz, uiop->uio_resid);
        
        DEBUG_PRINT((CE_CONT,"iumfs_read: uiop->uio_offset = %d\n",uiop->uio_offset));
        DEBUG_PRINT((CE_CONT,"iumfs_read: uiop->uio_resid  = %d\n",uiop->uio_resid));        
        DEBUG_PRINT((CE_CONT,"iumfs_read: mapoff = %d\n",mapoff));
        DEBUG_PRINT((CE_CONT,"iumfs_read: reloff = %d\n",reloff));
        DEBUG_PRINT((CE_CONT,"iumfs_read: mapsz =  %d\n",mapsz));

        /*
         * ファイルの指定領域とカーネルアドレス空間のマップを行う。
         * segmap_getmapflt の第 5 引数の forcefault を 1 にすると、segmap_getmapflt
         * の中でページフォルトで発生し iumfs_getpage が呼ばれる。
         * もし 0 とすると uiomove() が呼ばれてページフォルトが発生した段階で初めて
         * iumfs_getpage 呼ばれることになる。
         */
        base = segmap_getmapflt(segkmap, vp, mapoff + reloff, mapsz, 1, S_READ);
        if(base == NULL){
            DEBUG_PRINT((CE_CONT,"iumfs_read: segmap_getmapflt returned NULL\n"));
        }
        DEBUG_PRINT((CE_CONT,"iumfs_read: segmap_getmapflt succeeded \n"));                

        /*
         * 読み込んだデータをユーザ空間にコピーする。
         * もし、この時点で pagefault が発生したら VOP_GETPAGE ルーチン（iumfs_getpage）
         * が呼ばれ、ユーザモードデーモンにデータの取得を依頼する。
         */
        err = uiomove(base + reloff, mapsz, UIO_READ, uiop);
        if(err != SUCCESS){
            DEBUG_PRINT((CE_CONT,"iumfs_read: uiomove failed (%d)\n",err));
            goto done;
        }
        DEBUG_PRINT((CE_CONT,"iumfs_read: uiomove succeeded \n"));                    

        /*
         * マッピングを解放する。フリーリストに追加される。
         */
        err = segmap_release(segkmap, base, flags);
        if(err != SUCCESS){
            DEBUG_PRINT((CE_CONT,"iumfs_read: segmap_release failed (%d)\n",err));            
            goto done;
        }
        
        DEBUG_PRINT((CE_CONT,"iumfs_read: segmap_release succeeded \n"));        

    } while (uiop->uio_resid > 0);

  done:
    DEBUG_PRINT((CE_CONT,"iumfs_read: returned\n"));        
    inp->vattr.va_atime = iumfs_get_current_time();
    mutex_exit(&(inp->i_lock));
    
    return(err);    
}



/************************************************************************
 * iumfs_getattr()  VNODE オペレーション
 *
 * GETATTR ルーチン
 *************************************************************************/
static int
iumfs_getattr(vnode_t *vp, vattr_t *vap, int flags,  struct cred *cr)
{
    iumnode_t *inp;
    int        err;
    timestruc_t prev_mtime; // キャッシュしていた更新日時
    timestruc_t curr_mtime; // 最新の更新日時
    vnode_t     *parentvp;
    char        *name = NULL; // vnode に対応したファイルの名前
    
    DEBUG_PRINT((CE_CONT,"iumfs_getattr is called\n"));
    DEBUG_PRINT((CE_CONT,"iumfs_getattr: va_mask = 0x%x\n", vap->va_mask));

    inp = VNODE2IUMNODE(vp);
    prev_mtime = inp->vattr.va_mtime;

    /*
     * ユーザモードデーモンに最新の属性情報を問い合わせる。
     */
    if((err = iumfs_request_getattr(vp))){
        DEBUG_PRINT((CE_CONT,"iumfs_getattr: can't update latest attributes"));
        //親ディレクトリを探す
        if((parentvp = iumfs_find_parent_vnode(vp)) == NULL){
            cmn_err(CE_CONT, "iumfs_getattr: failed to find parent vnode of \"%s\"\n", inp->pathname);
            return (err);
        }
        //パス名より名前を得る
        if((name = strrchr(inp->pathname, '/')) == NULL){
            cmn_err(CE_CONT, "iumfs_getattr: failed to get name of \"%s\"\n", inp->pathname);
            return(err);
        }
        // スラッシュから始まっているので、一文字ずらす
        name++;
        
        /*
         * 親ディレクトリからエントリを削除
         * その後、iumfs_find_parent_vnode() で増やされたの親ディレクトリの参照カウント分を減らす 
         */
        iumfs_remove_entry_from_dir(parentvp, name);
        VN_RELE(parentvp);
        
        /*
         * 最後にこの vnode の参照カウントを減らす。
         * この vnode を参照中の人がいるかもしれないので（たとえば shell の
         * カレントディレクトリ）、ここでは free はしない。
         * 参照数が 1 になった段階で iumfs_inactive() が呼ばれ、iumfs_inactive()
         * から free される。
         */
        VN_RELE(vp); // vnode 作成時に増加された参照カウント分を減らす。

        return(err);
    }

    curr_mtime = inp->vattr.va_mtime;    
    
    /*
     * 更新日が変更されていたら vnode に関連したページを無効化する
     */ 
    if((curr_mtime.tv_sec != prev_mtime.tv_sec) || (curr_mtime.tv_nsec != prev_mtime.tv_nsec)){
        DEBUG_PRINT((CE_CONT,"iumfs_getattr: mtime have been changed. invalidating pages.."));        
        err = pvn_vplist_dirty(vp, 0, iumfs_putapage ,B_INVAL, cr);
        // ページを vnode に関連したページを無効化する。
        DEBUG_PRINT((CE_CONT,"iumfs_getattr: pvn_vplist_dirty returned with (%d)\n",err));
    }

    /*
     * ファイルシステム型依存のノード情報(iumnode 構造体)から vnode の属性情報をコピー。
     * 本来は、va_mask にて bit が立っている属性値だけをセットすればよいの
     * だが、めんどくさいので、全ての属性値を vap にコピーしてしまう。
     */
    bcopy(&inp->vattr, vap, sizeof(vattr_t));

    /* 
     * va_mask;      // uint_t           bit-mask of attributes        
     * va_type;      // vtype_t          vnode type (for create)      
     * va_mode;      // mode_t           file access mode             
     * va_uid;       // uid_t            owner user id                
     * va_gid;       // gid_t            owner group id               
     * va_fsid;      // dev_t(ulong_t)   file system id (dev for now) 
     * va_nodeid;    // ino_t          node id                      
     * va_nlink;     // nlink_t          number of references to file 
     * va_size;      // u_offset_t       file size in bytes           
     * va_atime;     // timestruc_t      time of last access          
     * va_mtime;     // timestruc_t      time of last modification    
     * va_ctime;     // timestruc_t      time file ``created''        
     * va_rdev;      // dev_t            device the file represents   
     * va_blksize;   // uint_t           fundamental block size       
     * va_nblocks;   // ino_t          # of blocks allocated        
     * va_vcode;     // uint_t           version code                
     */
    
    
    return(SUCCESS);
}

/************************************************************************
 * iumfs_access()  VNODE オペレーション
 *
 * 常に成功（アクセス可否を判定しない）
 *************************************************************************/
static int
iumfs_access (vnode_t *vp, int mode, int flags, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_access is called\n"));

    // 常に成功
    return(SUCCESS);
}

/************************************************************************
 * iumfs_lookup()  VNODE オペレーション
 *
 *  引数渡されたファイル/ディレクトリ名（name）をディレクトリエントリから
 *  探し、もし存在すれば、そのファイル/ディレクトリの vnode のアドレスを
 *  引数として渡された vnode のポインタにセットする。
 *
 *************************************************************************/
static int
iumfs_lookup (vnode_t *dvp, char *name, vnode_t **vpp,  struct pathname *pnp, int flags,
              vnode_t *rdir, struct cred *cr)
{
    vnode_t      *vp = NULL;
    iumnode_t    *dinp, *inp;
    ino_t         nodeid = 0; // 64 bit のノード番号（ inode 番号）
    iumfs_t      *iumfsp;     // ファイルシステム型依存のプライベートデータ構造体
    vfs_t        *vfsp;
    int           err;
    vattr_t       vap[1];
    char          pathname[MAXPATHLEN]; // マウントポイントからのパス名
    
    DEBUG_PRINT((CE_CONT,"iumfs_lookup is called\n"));

    DEBUG_PRINT((CE_CONT,"iumfs_lookup: pathname->pn_buf  = \"%s\"\n", pnp->pn_buf));
    DEBUG_PRINT((CE_CONT,"iumfs_lookup: name(file name)   = \"%s\"\n", name));

    iumfsp = VNODE2IUMFS(dvp);
    dinp   = VNODE2IUMNODE(dvp);
    vfsp   = VNODE2VFS(dvp);    

    /*
     * ファイルシステムルートからのパス名を得る。
     * もし親ディレクトリがファイルシステムルートだったら、余計な「/」はつけない。
     */            
    if(ISROOT(dinp->pathname))
	snprintf(pathname, MAXPATHLEN, "/%s",name);
    else
        snprintf(pathname, MAXPATHLEN, "%s/%s", dinp->pathname, name);
    DEBUG_PRINT((CE_CONT,"iumfs_lookup: pathname   = \"%s\"\n", pathname));

    nodeid = iumfs_find_nodeid_by_name(dinp, name);
    if(nodeid == 0){
        /*
         * ディレクトリエントリの中に該当するファイルが見つらなかった。
         * 通常ファイルの場合はここにくることになる。(通常ファイルのディレクトリエントリ内
         * でのノード番号はすべて 0 となっているため。
         */
        DEBUG_PRINT((CE_CONT,"iumfs_lookup: can't get node id of \"%s\" in existing dir entry\n", name));        
        vp = iumfs_find_vnode_by_pathname(iumfsp, pathname);
    } else {
        /*
         * ディレクトリエントリの中に該当するファイルが見つかった。
         * ここに来るのは vnode がディレクトリの場合だけ。
         */
        vp = iumfs_find_vnode_by_nodeid(iumfsp, nodeid);
    }
    
    if(vp == NULL){
        if( strcmp(name, "..") == 0){
            /*
             * ここにくるのはマウントポイントでの「..」の検索要求の時だけ。
             * マウントポイントの親ディレクトリの vnode を探してやる
             * （TODO: 現在は決めうちで / の vnode を返している・・）
             */
            DEBUG_PRINT((CE_CONT,"iumfs_lookup: look for a vnode of parent directory\n"));
            err = lookupname("/", UIO_SYSSPACE, FOLLOW, 0, &vp);
            /*
             * lookupname() が正常終了した場合は、親ディレクトリが存在するファイルシステムが
             * vnode の参照カウントを増加させていると期待される。
             * なので、ここでは vnode に対して VN_HOLD() は使わない。
             */
            if(err){
                DEBUG_PRINT((CE_CONT,"iumfs_lookup: cannot find vnode of parent directory\n"));
                return(ENOENT);
            }
        } else {
            if((err = iumfs_request_lookup(dvp, pathname, vap)) != 0){
                DEBUG_PRINT((CE_CONT,"iumfs_lookup: cannot find file \"%s\"\n", name));
                /*
                 * サーバ上にも見つからなかった・・エラーを返す
                 */
                return(err);
            }
            /*
             * リモートサーバ上にファイルが見つかったので新しいノードを作成する。
             * ディレクトリの場合、「.」と「..」の 2 つディレクトリエントリを追加
             * しなければいけないので、iumfs_make_directory() 経由でノードの
             * 追加を行う。
             */
            
            if(vap->va_type & VDIR){
                if ((err = iumfs_make_directory(vfsp, &vp, dvp, cr)) != SUCCESS){
                    cmn_err(CE_CONT, "iumfs_lookup: failed to create directory \"%s\"\n", name);
                    return(err);
                }
            } else {
                if((err = iumfs_alloc_node(vfsp, &vp, 0, vap->va_type)) !=  SUCCESS){
                    cmn_err(CE_CONT, "iumfs_lookup: failed to create new node \"%s\"\n", name);
                    return(err);
                }
            }
            inp = VNODE2IUMNODE(vp);
            
            snprintf(inp->pathname, MAXPATHLEN, "%s", pathname);
            DEBUG_PRINT((CE_CONT,"iumfs_lookup: allocated new node \"%s\"\n",inp->pathname));
            // vnode の参照カウントを増やす            
            VN_HOLD(vp);
        }
    }

    *vpp = vp;
    return(SUCCESS);
}

/************************************************************************
 * iumfs_readdir()  VNODE オペレーション
 *
 * getdent(2) システムコールに対応する。
 * 引数で指定された vnode がさすディレクトリのデータを読み、dirent 構造体
 * を返す。
 *************************************************************************/
static int
iumfs_readdir(vnode_t *vp, struct uio *uiop, struct cred *cr, int *eofp)
{
    offset_t     dent_total;
    iumnode_t   *inp;
    int          err;
    dirent64_t    *dentp;
    offset_t     offset;
    offset_t     readoff = 0; // directory エントリの境界を考えた offset
    size_t       readsize = 0 ;
    int          count_start = 0;
    time_t       prev_mtime = 0;

    DEBUG_PRINT((CE_CONT,"iumfs_readdir is called.\n"));

    // ノードのタイプが VDIR じゃ無かったらエラーを返す
    if(!(vp->v_type & VDIR)){
        DEBUG_PRINT((CE_CONT,"iumfs_readdir: vnode is not a directory.\n"));
        return(ENOTDIR);
    }

    // ファイルシステム型依存のノード構造体を得る
    inp = VNODE2IUMNODE(vp);

    // キャッシュにある更新時間(mtime)をセーブしておく
    // TODO: lock を取得していない
    prev_mtime = inp->vattr.va_mtime.tv_sec;

    // 最新の更新時間(mtime)を得る
    err = iumfs_request_getattr(vp);    
    if(err){
        DEBUG_PRINT((CE_CONT,"iumfs_readdir: can't update latest attributes"));        
        return(err);
    }

    /*
     * 以下の条件にあった場合にディレクトリエントリを読む
     *
     *  o ディレクトリの更新時間が変わっていたら
     *  o ディレクトリの更新時間が変わっていないが、現在ディレクトリは空
     */
    if (inp->vattr.va_mtime.tv_sec != prev_mtime){
        err = iumfs_request_readdir(vp);
    } else if (iumfs_dir_is_empty(vp)){
        err = iumfs_request_readdir(vp);        
    }
    
/*
 * 
 * もし、ディレクトリの更新時間が変わっていたらもう一度ディレクトリエントリを
 * 読み直す。そうでなければ既存のディレクトリエントリを返す。
 * TODO:  最初の読み込み判定に dlen == 64 と、ディレクトリエントリのサイズを
 * 使ってしまっている。あまり良くない
 * 
   if(inp->dlen == 64 || inp->vattr.va_mtime.tv_sec != prev_mtime){
       mutex_exit(&(inp->i_lock));
       err = iumfs_request_readdir(vp);
       mutex_enter(&(inp->i_lock));
    }
*/
/*
    err = iumfs_request_readdir(vp);    
*/


    mutex_enter(&(inp->i_lock));
    
    dent_total = inp->dlen;

    DEBUG_PRINT((CE_CONT,"iumfs_readdir: dent_total = %d\n",dent_total));
    DEBUG_PRINT((CE_CONT,"iumfs_readdir: uiop->uio_offset = %d\n",uiop->uio_offset));
    DEBUG_PRINT((CE_CONT,"iumfs_readdir: uiop->uio_resid  = %d\n",uiop->uio_resid));

    /*
     * 
     *
     */
    for( offset = 0 ; offset < inp->dlen ; offset += dentp->d_reclen){
        dentp = (dirent64_t *)((char *)inp->data + offset);
	if(!count_start){
		if(offset >= uiop->uio_offset){
			readoff = offset;
			count_start = 1;
		}
	}
	if (count_start){
		if( readsize + dentp->d_reclen > uiop->uio_resid)
			break;
		readsize += dentp->d_reclen;
	}
    }

    if(readsize == 0){
        err = uiomove(inp->data, 0, UIO_READ, uiop);
        if(err == SUCCESS)
            DEBUG_PRINT((CE_CONT,"iumfs_readdir: 0 byte copied\n"));            
    } else {
        err = uiomove((caddr_t)inp->data + readoff, readsize, UIO_READ, uiop);
        if(err == SUCCESS)
            DEBUG_PRINT((CE_CONT,"iumfs_readdir: %d byte copied\n", readsize));    
    }
    inp->vattr.va_atime    = iumfs_get_current_time();
    
    mutex_exit(&(inp->i_lock));    
    
    return(err);
}


/************************************************************************
 * iumfs_fsync()  VNODE オペレーション
 *
 * 常に成功
 *************************************************************************/
static int
iumfs_fsync  (vnode_t *vp, int syncflag, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_fsync is called\n"));
    
    return(SUCCESS);
}

/************************************************************************
 * iumfs_inactive()  VNODE オペレーション
 *
 * vnode の 参照数（v_count）が 0 になった場合に VFS サブシステムから
 * 呼ばれる・・と思っていたが、これが呼ばれるときは v_count はかならず 1
 * のようだ。
 *
 * v_count が 0 になるのは、iumfs_rmdir で明示的に参照数を 1 にしたときのみ。
 * 
 *************************************************************************/
static void
iumfs_inactive(vnode_t *vp, struct cred *cr)
{
    vnode_t    *rootvp;
    
    DEBUG_PRINT((CE_CONT,"iumfs_inactive is called\n"));

    /*
     * 実際のファイルシステムでは、ここで、変更されたページのディスクへ
     * の書き込みが行われ、vnode の解放などが行われる。
     */

    rootvp = VNODE2ROOT(vp);

    if(rootvp == NULL)
        return;

    // この関数が呼ばれるときは v_count は常に 1 のようだ。
    DEBUG_PRINT((CE_CONT,"iumfs_inactive: vp->v_count = %d\n",vp->v_count ));
    
    if(VN_CMP(rootvp, vp) != 0){
        DEBUG_PRINT((CE_CONT,"iumfs_inactive: vnode is rootvp\n"));
    } else {
        DEBUG_PRINT((CE_CONT,"iumfs_inactive: vnode is not rootvp\n"));
    }

    // iumfsnode, vnode を free する。
    iumfs_free_node(vp, cr);
    
    return;
}

/************************************************************************
 * iumfs_seek()  VNODE オペレーション
 *
 * 常に成功
 *************************************************************************/
static int
iumfs_seek (vnode_t *vp, offset_t ooff, offset_t *noffp)
{
    DEBUG_PRINT((CE_CONT,"iumfs_seek is called\n"));
    
    DEBUG_PRINT((CE_CONT,"iumfs_seek: ooff = %d, noffp = %d\n", ooff, *noffp));
    
    return(SUCCESS);
}

/************************************************************************
 * iumfs_cmp()  VNODE オペレーション
 *
 * 二つの vnode のアドレスを比較。
 *
 * 戻り値
 *    同じ vnode : 1
 *    違う vnode : 0
 *************************************************************************/
static int
iumfs_cmp (vnode_t *vp1, vnode_t *vp2)
{
    DEBUG_PRINT((CE_CONT,"iumfs_cmp is called\n"));

    // VN_CMP マクロに習い、同じだったら 1 を返す
    if (vp1 == vp2)
        return(1);
    else
        return(0);
}


/************************************************************************
 * iumfs_getpage()  VNODE オペレーション
 *
 * vnode に関連するページを得るための処理だが実際のページの取得処理は
 * iumfs_getapage() で行う。
 *          ^
 * len が PAGESIZE を超えている場合は pvn_getpage() を呼び出し、PAGESIZE
 * 以下であれば iumfs_getapage() を呼び出す。どちらにしても最終的には
 * iumfs_getapage() が呼ばれる。
 *************************************************************************/
static int
iumfs_getpage(vnode_t *vp, offset_t off, size_t len, uint_t *protp, struct page **plarr,
              size_t plsz, struct seg *seg, caddr_t addr, enum seg_rw rw, struct cred *cr)
{
    int		err;

    DEBUG_PRINT((CE_CONT,"iumfs_getpage is called\n"));
    DEBUG_PRINT((CE_CONT,"iumfs_getpage: off  = %d\n", off));    
    DEBUG_PRINT((CE_CONT,"iumfs_getpage: len  = %d\n", len));
    DEBUG_PRINT((CE_CONT,"iumfs_getpage: plsz = %d\n", plsz));            

    if (len <= PAGESIZE) {
	err = iumfs_getapage(vp, off, len, protp, plarr, plsz, seg, addr, rw, cr);
    } else {
	err = pvn_getpages(iumfs_getapage, vp, off, len, protp, plarr, plsz, seg, addr, rw, cr);
    }
   
    return (err);    
}

/************************************************************************
 * iumfs_putpage()  VNODE オペレーション
 *
 * ファイルシステムを READ ONLY としているので PUTPAGE する必要は無い。
 *
 *************************************************************************/
static int
iumfs_putpage(vnode_t *vp, page_t *pp, u_offset_t *offp, size_t *lenp, int flags, struct cred *cr) 
{
    DEBUG_PRINT((CE_CONT,"iumfs_putpage is called\n"));

    if (vp->v_flag & VNOMAP)
        return(ENOSYS);
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_map()  VNODE オペレーション
 *
 * サポートしていない
 * 
 * TODO: 要サポート。実行ファイルの exec(2) にはこの vnode オペレーションの
 *       サポートが必須。なので、現在はこのファイルシステム上にある実行
 *       可能ファイルを exec(2) することはできない。
 *************************************************************************/
static int
iumfs_map (vnode_t *vp, offset_t off, struct as *as, caddr_t *addrp, size_t len,
           uchar_t prot,uchar_t maxprot, uint_t flags, struct cred *cr)
{
    struct segvn_crargs vn_a;
    int error;

    DEBUG_PRINT((CE_CONT,"iumfs_map is called\n"));        

    /*
    if (vp->v_flag & VNOMAP)
        Return (ENOSYS);
 
    if (off > UINT32_MAX || off + len > UINT32_MAX)
        return (ENXIO);
    */
 
    as_rangelock(as);
    if ((flags & MAP_FIXED) == 0) {
        map_addr(addrp, len, off, 1, flags);
        if (*addrp == NULL) {
            as_rangeunlock(as);
            return (ENOMEM);
        }
    } else {
        /*
         * User specified address - blow away any previous mappings
         */
        (void) as_unmap(as, *addrp, len);
    }
 
    vn_a.vp = vp;
    vn_a.offset = off;
    vn_a.type = flags & MAP_TYPE;
    vn_a.prot = prot;
    vn_a.maxprot = maxprot;
    vn_a.flags = flags & ~MAP_TYPE;
    vn_a.cred = cr;
    vn_a.amp = NULL;
    vn_a.szc = 0;
    vn_a.lgrp_mem_policy_flags = 0;
 
    error = as_map(as, *addrp, len, segvn_create, &vn_a);
    as_rangeunlock(as);
    return (error);
//    return(ENOTSUP);
}


/************************************************************************
 * iumfs_getapage()  
 *
 * iumfs_getpage() もしくは vpn_getpages() から呼ばれ、vnode に関連する
 * ページを得る。
 *  
 *************************************************************************/
static int
iumfs_getapage(vnode_t *vp, u_offset_t off, size_t len, uint_t *protp, page_t *plarr[],
               size_t plsz, struct seg *seg, caddr_t addr, enum seg_rw rw, struct cred *cr)
{
    page_t      *pp = NULL;
    size_t	 io_len;
    u_offset_t	 io_off;    
    int          err = 0;
    struct buf  *bp = NULL;
    static       int count = 0;
    
    DEBUG_PRINT((CE_CONT,"iumfs_getapage is called\n"));
    DEBUG_PRINT((CE_CONT,"iumfs_getapage: off  = %d\n", off));
    DEBUG_PRINT((CE_CONT,"iumfs_getapage: len  = %d\n", len));
    DEBUG_PRINT((CE_CONT,"iumfs_getapage: plsz = %d\n", plsz));

    DEBUG_PRINT((CE_CONT,"iumfs_getapage: count = %d(before)\n", count));    
    count++;
    DEBUG_PRINT((CE_CONT,"iumfs_getapage: count = %d(after)\n", count));                    
 
    if (plarr == NULL) {
        DEBUG_PRINT((CE_CONT,"iumfs_getapage: plarr is NULL\n"));        
        return (0);
    }
    plarr[0] = NULL;

    do {
        err = 0;
        bp = NULL;
        pp = NULL;
        
        /*
         * まずページキャッシュの中に該当ページが存在するかを確認する。
         * もしあればそのページを plarr にセットして返す。
         */
        if (page_exists(vp, off)) {        
            pp = page_lookup(vp, off, SE_SHARED);
            if (pp){
                DEBUG_PRINT((CE_CONT,"iumfs_getapage: page found in cache\n"));
                plarr[0] = pp;
                plarr[1] = NULL;
                break;
            }
            /*
             * もう一度最初からやり直し。
             */
            continue;
        }
    
        DEBUG_PRINT((CE_CONT,"iumfs_getapage: page not found in cache\n"));

        /*
         * addr で指定されたアドレス範囲から、指定された vnode のオフセットとサイズに
         * 適合する連続したページブロックを見つける。
         * io_len は PAGESIZE より大きくなる（倍数）可能性もある。
         */        
        pp = pvn_read_kluster(vp, off, seg, addr, &io_off, &io_len, off, len, 0);

        /*
         * pvn_read_kluster が NULL を返してきた場合、他の thread が
         * すでに page を参照している可能性がある。lookup からやり直し。
         */
        if (pp == NULL) {
            DEBUG_PRINT((CE_CONT,"iumfs_getapage: pvn_read_kluster returned NULL, try lookup again.."));
            continue;
        }

        DEBUG_PRINT((CE_CONT,"iumfs_getapage: pvn_read_kluster succeeded io_off = %d, io_len =%d \n", io_off, io_len));

        /*
         * 要求されたサイズをページサイズに丸め込む。
         */
        io_len = ptob(btopr(io_len));

        DEBUG_PRINT((CE_CONT,"iumfs_getapage: ptob(btopr(io_len)) = %d\n", io_len));                

        /*
         * buf 構造体を確保し、初期化する。
         *   ..bp->b_bcount = io_len;
         *   ..bp->b_bufsize = io_len;
         *   ..bp->b_vp = vp など。
         */
        bp = pageio_setup(pp, io_len, vp, B_READ);

        /*
         * block（DEV_BSIZE）数から byte 数へ
         */
        bp->b_lblkno = lbtodb(io_off); // 512 で割った数を計算？
#ifdef SOL10
        /*
         * solaris 9 の buf 構造体には以下のメンバーは含まれない。
         * これらのメンバーには何の意味が・・？ 一応セット
         */
        bp->b_file = vp;              // vnode
        bp->b_offset = (offset_t)off; // vnode offset
#endif

        /*
         * カーネルの仮想アドレス空間にアドレスを確保し、ページの
         * リストにマップする。確保したアドレスはbp->b_un.b_addr
         * にセットする。man bp_mapin(9F)
         */
        bp_mapin(bp);

        /*
         * ユーザモードデーモンへリクエストを投げる。
         */
        err = iumfs_request_read(bp, vp);
    
        /*
         * man bp_mapout(9F)
         */
        bp_mapout(bp);
        pageio_done(bp);

        /*
         *  ページリストの配列(plarr)を初期化する
         */
        pvn_plist_init(pp, plarr, plsz, off, io_len, rw);
    
    } while(0);

    if(err){
        if (pp != NULL)
            pvn_read_done(pp, B_ERROR);
    }
    return (err);
}

/************************************************************************
 * iumfs_putapage()  VNODE オペレーション
 *
 * pvn_vplist_dirty() の引数に使うためだけに存在する関数。
 * ファイルシステムを READ ONLY としているので PUTPAGE する必要は無い。
 *
 *************************************************************************/
int
iumfs_putapage(vnode_t *vp, page_t *pp, u_offset_t *offp, size_t *lenp, int flags, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_putapage is called\n"));

    return(0);
}


#ifndef SOL10
/************************************************************************
 * iumfs_write()  VNODE オペレーション
 *
 * write(2) システムコールに対応する。
 * サポートしていない。
 * 現在 vfs_flag に VFS_RDONLY がたっているので呼ばれることも無い。  
 *************************************************************************/
static int
iumfs_write(vnode_t *vp, struct uio *uiop, int ioflag, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_write is called\n"));    
    return(ENOTSUP);        
}

/************************************************************************
 * iumfs_ioctl()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_ioctl  (vnode_t *vp, int cmd, intptr_t arg, int flag,  struct cred *cr, int *rvalp)
{
    DEBUG_PRINT((CE_CONT,"iumfs_ioctl is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_setfl()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
 static int
iumfs_setfl  (vnode_t *vp, int oflags, int nflags, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_setfl is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_setattr()  VNODE オペレーション
 *
 * SETATTR ルーチン。サポートされていない。
 * 現在 vfs_flag に VFS_RDONLY がたっているので呼ばれることは無い。  
 *
 *************************************************************************/
static int
iumfs_setattr(vnode_t *vp, vattr_t *vap, int flags,struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_setattr is called\n"));

    return(ENOTSUP);
}

/************************************************************************
 * iumfs_create()  VNODE オペレーション
 *
 * create(2) システムコールに対応する。サポートしていない。
 * 現在 vfs_flag に VFS_RDONLY がたっているので呼ばれることは無い。  
 *************************************************************************/
static int
iumfs_create (vnode_t *dirvp, char *name, vattr_t *vap,  vcexcl_t excl, int mode, vnode_t **vpp,
              struct cred *cr, int flag)
{
    DEBUG_PRINT((CE_CONT,"iumfs_create is called\n"));

    return(ENOTSUP);    
}

/************************************************************************
 * iumfs_remove()  VNODE オペレーション
 *
 * サポートしていない
 * 現在 vfs_flag に VFS_RDONLY がたっているので呼ばれることは無い。
 *************************************************************************/
static int
iumfs_remove (vnode_t *pdirvp, char *name, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_remove is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_link()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_link   (vnode_t *tdvp, vnode_t *svp, char *tnm,  struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_link is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_rename()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_rename (vnode_t *sdvp, char *snm,  vnode_t *tdvp, char *tnm, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_rename is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_mkdir()  VNODE オペレーション
 *
 * mkdir(2) システムコールに対応する。
 * 指定された名前の新規ディレクトリを作成する。
 * 現在 vfs_flag に VFS_RDONLY がたっているので呼ばれることは無い。
 * 
 *************************************************************************/
static int
iumfs_mkdir (vnode_t *dvp, char *dirname,  vattr_t *vap, vnode_t **vpp, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_mkdir is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_rmdir()  VNODE オペレーション
 *
 * rmdir(2) システムコールに対応する。サポートされていない。
 * 現在 vfs_flag に VFS_RDONLY がたっているので呼ばれることは無い。
 *************************************************************************/
static int
iumfs_rmdir (vnode_t *pdirvp, char *name, vnode_t *cdirvp, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_rmdir is called\n"));

    return(ENOTSUP);
}

/************************************************************************
 * iumfs_symlink()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_symlink(vnode_t *dvp, char *linkname, vattr_t *vap, char *target,struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_symlink is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_readlink()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_readlink(vnode_t *vp, struct uio *uiop,struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_readlink is called\n"));
    
    return(ENOTSUP);
}


/************************************************************************
 * iumfs_fid()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_fid    (vnode_t *vp, struct fid *fidp)
{
    DEBUG_PRINT((CE_CONT,"iumfs_fid is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_rwlock()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static void
iumfs_rwlock (vnode_t *vp, int write_lock)
{
    DEBUG_PRINT((CE_CONT,"iumfs_rwlock is called\n"));
    
    return;
}

/************************************************************************
 * iumfs_rwunlock()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static void
iumfs_rwunlock(vnode_t *vp, int write_lock)
{
    DEBUG_PRINT((CE_CONT,"iumfs_rwunlock is called\n"));
    
    return;
}


/************************************************************************
 * iumfs_frlock()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_frlock (vnode_t *vp, int cmd, struct flock64 *bfp,   int flag, offset_t offset,
              struct flk_callback *callback, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_frlock is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_space()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_space (vnode_t *vp, int cmd, struct flock64 *bfp,int flag, offset_t offset, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_space is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_realvp()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_realvp (vnode_t *vp, vnode_t **vpp)
{
    DEBUG_PRINT((CE_CONT,"iumfs_realvp is called\n"));
    
    return(ENOTSUP);
}


/************************************************************************
 * iumfs_addmap()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_addmap (vnode_t *vp, offset_t off, struct as *as,caddr_t addr, size_t len,
              uchar_t prot,  uchar_t maxprot, uint_t flags, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_addmap is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_delmap()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_delmap (vnode_t *vp, offset_t off, struct as *as,caddr_t addr, size_t len,
              uint_t prot,uint_t maxprot, uint_t flags, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_delmap is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_poll()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_poll   (vnode_t *vp, short ev, int any, short *revp,struct pollhead **phpp)
{
    DEBUG_PRINT((CE_CONT,"iumfs_poll is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_dump()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_dump (vnode_t *vp, caddr_t addr, int lbdn,  int dblks)
{
    DEBUG_PRINT((CE_CONT,"iumfs_dump is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_pathconf()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_pathconf(vnode_t *vp, int cmd, ulong_t *valp,struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_pathconf is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_pageio()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_pageio (vnode_t *vp, struct page *pp,u_offset_t io_off, size_t io_len, int flags,  struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_pageio is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_dumpctl()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_dumpctl(vnode_t *vp, int action, int *blkp)
{
    DEBUG_PRINT((CE_CONT,"iumfs_dumpctl is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_dispose()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static void
iumfs_dispose(vnode_t *vp, struct page *pp, int flag,int dn, struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_dispose is called\n"));
    
    return;
}

/************************************************************************
 * iumfs_setsecattr()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_setsecattr(vnode_t *vp, vsecattr_t *vsap, int flag,struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_setsecattr is called\n"));
    
    return(ENOTSUP);
}

/************************************************************************
 * iumfs_getsecattr()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_getsecattr(vnode_t *vp, vsecattr_t *vsap, int flag,struct cred *cr)
{
    DEBUG_PRINT((CE_CONT,"iumfs_getsecattr is called\n"));
    
//    return(ENOTSUP);
    return(SUCCESS);
}

/************************************************************************
 * iumfs_shrlock()  VNODE オペレーション
 *
 * サポートしていない
 *************************************************************************/
static int
iumfs_shrlock (vnode_t *vp, int cmd, struct shrlock *shr,int flag)
{
    DEBUG_PRINT((CE_CONT,"iumfs_shrlock is called\n"));
    
    return(ENOTSUP);
}

#endif // #ifndef SOL10
