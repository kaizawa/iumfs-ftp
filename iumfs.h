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
/************************************************************
 * iumfs.h
 * 
 * iumfs 用ヘッダーファイル
 *
 *************************************************************/

#ifndef __IUMFS_H
#define __IUMFS_H

#define MAXUSERLEN    100
#define MAXPASSLEN    100
#define MAXSERVERNAME 100

#define MMAPSIZE      PAGESIZE

typedef struct iumfs_mount_opts 
{
    char user[MAXUSERLEN];
    char pass[MAXPASSLEN];
    char server[MAXSERVERNAME];
    char basepath[MAXPATHLEN];
} iumfs_mount_opts_t;

/*
 * iumfs から iumfsd デーモンに渡されるリクエストの為の構造体
 */
typedef struct request
{
    int                request_type; // リクエストのタイプ
    iumfs_mount_opts_t mountopts[1]; // mount コマンドからの引数
    char               pathname[MAXPATHLEN]; // 操作対象のファイルのパス名
    union {
        struct {
            offset_t offset;
            size_t   size;
        } read_request;
        struct {
            offset_t offset;
            size_t   size;            
        } readdir_request;
    } data;
} request_t;

/*
 * 現在定義されているリクエストタイプ
 */
#define READ_REQUEST      0x01
#define READDIR_REQUEST   0x02
#define GETATTR_REQUEST   0x03

/*
 * デーモンが iumfscntl デバイスに報告する要求の実行結果
 * 通常は
 *   0        -> 成功
 *   それ以外 -> system error 番号
 * だが、readdir リクエスト用に以下の特別なエラー番号もつかう
 */
#define MOREDATA          240 // iumfscntl で使う特別なエラー番号

/*
 * 渡された文字列が「/」一文字であるかをチェック
 */
#define ISROOT(path) (strlen(path) == 1 && !strcmp(path, "/"))

#define SUCCESS         0       // 成功
#define TRUE            1       // 真
#define FALSE           0       // 偽

#ifdef _KERNEL

#define MAX_MSG         256     // SYSLOG に出力するメッセージの最大文字数 
#define MAXNAMLEN       255     // 最大ファイル名長
#define BLOCKSIZE       512     // iumfs ファイルシステムのブロックサイズ

#ifdef DEBUG
#define  DEBUG_PRINT(args)  debug_print args
#else
#define DEBUG_PRINT(args)
#endif

#define VFS2IUMFS(vfsp)     ((iumfs_t *)(vfsp)->vfs_data)
#define VFS2ROOT(vfsp)      ((VFS2IUMFS(vfsp))->rootvnode)
#define IUMFS2ROOT(iumfsp)  ((iumfsp)->rootvnode)
#define VNODE2VFS(vp)       ((vp)->v_vfsp)
#define VNODE2IUMNODE(vp)   ((iumnode_t *)(vp)->v_data)
#define IUMNODE2VNODE(inp)  ((inp)->vnode)
#define VNODE2IUMFS(vp)     (VFS2IUMFS(VNODE2VFS((vp))))
#define VNODE2ROOT(vp)      (VNODE2IUMFS((vp))->rootvnode)
#define IN_INIT(inp) {\
              mutex_init(&(inp)->i_lock, NULL, MUTEX_DEFAULT, NULL);\
              inp->vattr.va_uid      = 0;\
              inp->vattr.va_gid      = 0;\
              inp->vattr.va_blksize  = BLOCKSIZE;\
              inp->vattr.va_nlink    = 1;\
              inp->vattr.va_rdev     = 0;\
              inp->vattr.va_vcode    = 1;\
              inp->vattr.va_mode     = 00644;\
         }

#ifdef SOL10
// Solaris 10 には VN_INIT マクロが無いので追加。
#define	VN_INIT(vp, vfsp, type, dev)	{ \
	mutex_init(&(vp)->v_lock, NULL, MUTEX_DEFAULT, NULL); \
	(vp)->v_flag = 0; \
	(vp)->v_count = 1; \
	(vp)->v_vfsp = (vfsp); \
	(vp)->v_type = (type); \
	(vp)->v_rdev = (dev); \
	(vp)->v_stream = NULL; \
}
#else
#endif

// 引数で渡された数値をポインタ長の倍数に繰り上げるマクロ
#define ROUNDUP(num)        ((num)%(sizeof(char *)) ? ((num) + ((sizeof(char *))-((num)%(sizeof(char *))))) : (num))


#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) < (b) ? (b) : (a))

/*
 * ファイルシステム型依存のノード情報構造体。（iノード）
 * vnode 毎（open/create 毎）に作成される。
 * next, vattr, data, dlen については初期化以降も変更される
 * 可能性があるため、参照時にはロック(i_lock)をとらなければ
 * ならない。
 * pathname はこのノードに対応するファイルのファイルシステムルート  
 * からの相対パス名をあらわす。本来ファイル名はディレクトリにのみ  
 * 存在し、ノード情報にはファイル名は含まれないが、参照するリモート
 * サービス（FTP等）がファイル毎のユニーな ID を持っているとは限ら
 * ないので、ファイルのパス名をノード情報の検索キーとして使うことにする。            
 * 別途ノードID(vattr.va_nodeid) も内部処理用に保持する。
 */
typedef struct iumnode
{
    struct iumnode    *next;      // iumnode 構造体のリンクリストの次の iumnode 構造体
    kmutex_t           i_lock;    // 構造体のデータの保護用のロック    
    vnode_t           *vnode;     // 対応する vnode 構造体へのポインタ
    vattr_t            vattr;     // getattr, setattr で使われる vnode の属性情報
#define fsize vattr.va_size
#define iumnodeid vattr.va_nodeid    
    void              *data;      // vnode がディレクトリの場合、ディレクトリエントリへのポインタが入る
    offset_t           dlen;      // ディレクトリエントリのサイズ
    char               pathname[MAXPATHLEN]; // ファイルシステムルートからの相対パス
} iumnode_t;

/*
 * ファイルシステム型依存のファイルシステムプライベートデータ構造体。
 * ファイルシステム毎（mount 毎）に作成される。
 */
typedef struct iumfs
{
    kmutex_t      iumfs_lock;        // 構造体のデータの保護用のロック
    vnode_t      *rootvnode;         // ファイルシステムのルートの vnode
    ino_t         iumfs_last_nodeid; // 割り当てた最後のノード番号    
    iumnode_t     node_list_head;    // 作成された iumnode 構造体のリンクリストのヘッド。
                                     // 構造体の中身は、ロック以外は参照されない。また、
                                     // ファイルシステムが存在する限りフリーされることもない。
    iumfs_mount_opts_t mountopts[1]; // mount(2) から渡されたオプション
    dev_t         dev;               // このファイルシステムのデバイス番号
} iumfs_t;

/*
 * iumfscntl デバイスのステータス構造体
 */
typedef struct iumfscntl_soft 
{
    kmutex_t          d_lock;         // この構造体のデータを保護するロック
    kmutex_t          s_lock;         // ステータスを保護するロック
    kcondvar_t	      cv;             // condition variable
    dev_info_t        *dip;           // device infor 構造体
    caddr_t           mapaddr;        // mmap(2) でユーザ空間にマッピングするメモリアドレス
    int               instance;       // インスタンス番号
    ddi_umem_cookie_t umem_cookie;
    int               state;          // ステータスフラグ
    size_t            size;           // マッピングするメモリのサイズ
    request_t         req;            // ユーザモードデーモンに対するリクエストを格納する
    int               error;          // デーモンから返ってきたエラー番号
    struct pollhead   pollhead;
} iumfscntl_soft_t;

/*
 * iumfscntl デバイスのステータスフラグ
 */
#define IUMFSCNTL_OPENED        0x01  // /dev/iumfscntl はすでにオープンされている
#define REQUEST_INPROGRESS      0x02  // READ 処理が実行中
#define REQUEST_IS_SET          0x04  // 要求内容がセット済み
#define DAEMON_INPROGRESS       0x08  // デーモンが処理中
#define MAPDATA_INVALID         0x10  // マップされているアドレスのデータは不正なもの（読み込んではダメ）
                                      // デーモンが死んだか、もしくはデーモンがエラーを返してきた
#define REQUEST_IS_CANCELED     0x20  // リクエストがキャンセルされた

extern timestruc_t time; // システムの現在時

/* 関数のプロトタイプ宣言 */
void          debug_print(int , char *, ...);
int           iumfs_alloc_node(vfs_t *, vnode_t **, uint_t , enum vtype);
void          iumfs_free_node(vnode_t *, struct cred *);
int           iumfs_add_node_to_list(vfs_t *, vnode_t *);
int           iumfs_remove_node_from_list(vfs_t *, vnode_t *);
void          iumfs_free_all_node(vfs_t *, struct cred*);
int           iumfs_make_directory(vfs_t *, vnode_t **, vnode_t *, struct cred *);
int           iumfs_add_entry_to_dir(vnode_t *, char *, int, ino_t );
int           iumfs_remove_entry_from_dir(vnode_t *, char *);
ino_t         iumfs_find_nodeid_by_name(iumnode_t *, char *);
int           iumfs_dir_is_empty(vnode_t *);
vnode_t      *iumfs_find_vnode_by_nodeid(iumfs_t *, ino_t);
timestruc_t   iumfs_get_current_time();
int           iumfs_make_directory_with_name(vfs_t *, vnode_t **, vnode_t *, struct cred *, char *);
vnode_t      *iumfs_find_vnode_by_pathname(iumfs_t *, char *);
int           iumfs_directory_entry_exist(vnode_t *, char *);
int           iumfs_request_read(struct buf *, vnode_t *);        
int           iumfs_request_readdir(vnode_t *);                   
int           iumfs_request_lookup(vnode_t *, char *, vattr_t *); 
int           iumfs_request_getattr(vnode_t *);                   
int           iumfs_daemon_request_enter(iumfscntl_soft_t  *);    
int           iumfs_daemon_request_start(iumfscntl_soft_t  *);    
void          iumfs_daemon_request_exit(iumfscntl_soft_t  *);
vnode_t      *iumfs_find_parent_vnode(vnode_t *);


/* Solaris 10 以外の場合の gcc 対策用ラッパー関数 */
#ifndef SOL10
static void  *memcpy(void *,  const  void  *, size_t );
static int    memcmp(const void *, const void *, size_t );
static void  *memset(void *, int , size_t );
#endif
    
#ifndef SOL10
/**************************************************
 * memcpy()
 *
 * gcc 対策。bcopy(9f) のラッパー
 * Solaris 10 では memcpy(9f) が追加されているので不要
 **************************************************/
static void *
memcpy(void *s1,  const  void  *s2, size_t n)
{
    bcopy(s2, s1, n);
    return(s1);
}
/**************************************************
 * memcmp()
 *
 * gcc 対策。bcmp(9f) のラッパー
 * Solaris 10 では memcmp(9f) が追加されているので不要
 **************************************************/
static int
memcmp(const void *s1, const void *s2, size_t n)
{
    return(bcmp(s1, s1, n));
}
/**************************************************
 * memset()
 *
 * gcc 対策。
 * Solaris 10 では memset(9f) が追加されているので不要
 **************************************************/
static void *
memset(void *s, int c, size_t n)
{
    int i;
    uchar_t *p;

    p = (uchar_t *)s;
    
    for (i = 0 ; i < n ; i++){
        p[i] = c;
    }
    return(s);
}
#endif // #ifndef SOL10

#endif // #ifdef _KERNEL

#endif // #ifndef __IUMFS_H
