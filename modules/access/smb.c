/*****************************************************************************
 * smb.c: SMB input module
 *****************************************************************************
 * Copyright (C) 2001-2004 VideoLAN
 * $Id$
 *
 * Authors: Gildas Bazin <gbazin@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

/*****************************************************************************
 * Preamble
 *****************************************************************************/
#include <stdlib.h>

#include <vlc/vlc.h>
#include <vlc/input.h>

#include <libsmbclient.h>
#define USE_CTX 1

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

#define CACHING_TEXT N_("Caching value in ms")
#define CACHING_LONGTEXT N_( \
    "Allows you to modify the default caching value for SMB streams. This " \
    "value should be set in millisecond units." )
#define USER_TEXT N_("SMB user name")
#define USER_LONGTEXT N_("Allows you to modify the user name that will " \
    "be used for the connection.")
#define PASS_TEXT N_("SMB password")
#define PASS_LONGTEXT N_("Allows you to modify the password that will be " \
    "used for the connection.")
#define DOMAIN_TEXT N_("SMB domain")
#define DOMAIN_LONGTEXT N_("Allows you to modify the domain/workgroup that " \
    "will be used for the connection.")

vlc_module_begin();
    set_shortname( "SMB" );
    set_description( _("SMB input") );
    set_capability( "access2", 0 );
    set_category( CAT_INPUT );
    set_subcategory( SUBCAT_INPUT_ACCESS );
    add_integer( "smb-caching", 2 * DEFAULT_PTS_DELAY / 1000, NULL,
                 CACHING_TEXT, CACHING_LONGTEXT, VLC_TRUE );
    add_string( "smb-user", NULL, NULL, USER_TEXT, USER_LONGTEXT,
                VLC_FALSE );
    add_string( "smb-pwd", NULL, NULL, PASS_TEXT,
                PASS_LONGTEXT, VLC_FALSE );
    add_string( "smb-domain", NULL, NULL, DOMAIN_TEXT,
                DOMAIN_LONGTEXT, VLC_FALSE );
    add_shortcut( "smb" );
    set_callbacks( Open, Close );
vlc_module_end();

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int Read( access_t *, uint8_t *, int );
static int Seek( access_t *, int64_t );
static int Control( access_t *, int, va_list );

struct access_sys_t
{
#ifdef USE_CTX
    SMBCCTX *p_smb;
    SMBCFILE *p_file;
#else
    int i_smb;
#endif
};

void smb_auth( const char *srv, const char *shr, char *wg, int wglen,
               char *un, int unlen, char *pw, int pwlen )
{
    //wglen = unlen = pwlen = 0;
}

/****************************************************************************
 * Open: connect to smb server and ask for file
 ****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    access_t     *p_access = (access_t*)p_this;
    access_sys_t *p_sys;
    struct stat  filestat;
    char         *psz_uri, *psz_user, *psz_pwd, *psz_domain;
    int          i_ret;

#ifdef USE_CTX
    SMBCCTX      *p_smb;
    SMBCFILE     *p_file;
#else
    int          i_smb;
#endif

    /* Build an SMB URI
     * smb://[[[domain;]user[:password@]]server[/share[/path[/file]]]] */

    psz_user = var_CreateGetString( p_access, "smb-user" );
    if( psz_user && !*psz_user ) { free( psz_user ); psz_user = 0; }
    psz_pwd = var_CreateGetString( p_access, "smb-pwd" );
    if( psz_pwd && !*psz_pwd ) { free( psz_pwd ); psz_pwd = 0; }
    psz_domain = var_CreateGetString( p_access, "smb-domain" );
    if( psz_domain && !*psz_domain ) { free( psz_domain ); psz_domain = 0; }

    /* FIXME: will need to parse the URI so we don't override credentials
     * if there are already present. */
    if( psz_user )
        asprintf( &psz_uri, "smb://%s%s%s%s%s@%s",
                  psz_domain ? psz_domain : "", psz_domain ? ";" : "",
                  psz_user, psz_pwd ? ":" : "",
                  psz_pwd ? psz_pwd : "", p_access->psz_path );
    else
        asprintf( &psz_uri, "smb://%s", p_access->psz_path );

    if( psz_user ) free( psz_user );
    if( psz_pwd ) free( psz_pwd );
    if( psz_domain ) free( psz_domain );

#ifdef USE_CTX
    if( !(p_smb = smbc_new_context()) )
    {
        msg_Err( p_access, "out of memory" );
        free( psz_uri );
        return VLC_ENOMEM;
    }
    p_smb->debug = 1;
    p_smb->callbacks.auth_fn = smb_auth;

    if( !smbc_init_context( p_smb ) )
    {
        msg_Err( p_access, "cannot initialize context (%s)", strerror(errno) );
        smbc_free_context( p_smb, 1 );
        free( psz_uri );
        return VLC_EGENERIC;
    }

    if( !(p_file = p_smb->open( p_smb, psz_uri, O_RDONLY, 0 )) )
    {
        msg_Err( p_access, "open failed for '%s' (%s)",
                 p_access->psz_path, strerror(errno) );
        smbc_free_context( p_smb, 1 );
        free( psz_uri );
        return VLC_EGENERIC;
    }

    p_access->info.i_size = 0;
    i_ret = p_smb->fstat( p_smb, p_file, &filestat );
    if( i_ret ) msg_Err( p_access, "stat failed (%s)", strerror(errno) );
    else p_access->info.i_size = filestat.st_size;

#else
    if( smbc_init( smb_auth, 1 ) )
    {
        free( psz_uri );
        return VLC_EGENERIC;
    }

    if( (i_smb = smbc_open( psz_uri, O_RDONLY, 0 )) < 0 )
    {
        msg_Err( p_access, "open failed for '%s' (%s)",
                 p_access->psz_path, strerror(errno) );
        free( psz_uri );
        return VLC_EGENERIC;
    }

    p_access->info.i_size = 0;
    i_ret = smbc_fstat( i_smb, &filestat );
    if( i_ret ) msg_Err( p_access, "stat failed (%s)", strerror(i_ret) );
    else p_access->info.i_size = filestat.st_size;
#endif

    free( psz_uri );

    /* Init p_access */
    p_access->pf_read = Read;
    p_access->pf_block = NULL;
    p_access->pf_seek = Seek;
    p_access->pf_control = Control;
    p_access->info.i_update = 0;
    p_access->info.i_pos = 0;
    p_access->info.b_eof = VLC_FALSE;
    p_access->info.i_title = 0;
    p_access->info.i_seekpoint = 0;
    p_access->p_sys = p_sys = malloc( sizeof( access_sys_t ) );
    memset( p_sys, 0, sizeof( access_sys_t ) );

#ifdef USE_CTX
    p_sys->p_smb = p_smb;
    p_sys->p_file = p_file;
#else
    p_sys->i_smb = i_smb;
#endif

    /* Update default_pts to a suitable value for smb access */
    var_Create( p_access, "smb-caching", VLC_VAR_INTEGER | VLC_VAR_DOINHERIT );

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close: free unused data structures
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    access_t     *p_access = (access_t*)p_this;
    access_sys_t *p_sys = p_access->p_sys;

#ifdef USE_CTX
    p_sys->p_smb->close( p_sys->p_smb, p_sys->p_file );
    smbc_free_context( p_sys->p_smb, 1 );
#else
    smbc_close( p_sys->i_smb );
#endif

    free( p_sys );
}

/*****************************************************************************
 * Seek: try to go at the right place
 *****************************************************************************/
static int Seek( access_t *p_access, int64_t i_pos )
{
    access_sys_t *p_sys = p_access->p_sys;
    int64_t      i_ret;

    if( i_pos < 0 ) return VLC_EGENERIC;

    msg_Dbg( p_access, "seeking to "I64Fd, i_pos );

#ifdef USE_CTX
    i_ret = p_sys->p_smb->lseek(p_sys->p_smb, p_sys->p_file, i_pos, SEEK_SET);
#else
    i_ret = smbc_lseek( p_sys->i_smb, i_pos, SEEK_SET );
#endif
    if( i_ret == -1 )
    {
        msg_Err( p_access, "seek failed (%s)", strerror(errno) );
        return VLC_EGENERIC;
    }

    p_access->info.b_eof = VLC_FALSE;
    p_access->info.i_pos = i_ret;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Read:
 *****************************************************************************/
static int Read( access_t *p_access, uint8_t *p_buffer, int i_len )
{
    access_sys_t *p_sys = p_access->p_sys;
    int i_read;

    if( p_access->info.b_eof ) return 0;

#ifdef USE_CTX
    i_read = p_sys->p_smb->read(p_sys->p_smb, p_sys->p_file, p_buffer, i_len);
#else
    i_read = smbc_read( p_sys->i_smb, p_buffer, i_len );
#endif
    if( i_read < 0 )
    {
        msg_Err( p_access, "read failed (%s)", strerror(errno) );
        return -1;
    }

    if( i_read == 0 ) p_access->info.b_eof = VLC_TRUE;
    else if( i_read > 0 ) p_access->info.i_pos += i_read;

    return i_read;
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control( access_t *p_access, int i_query, va_list args )
{
    vlc_bool_t   *pb_bool;
    int          *pi_int;
    int64_t      *pi_64;

    switch( i_query )
    {
    case ACCESS_CAN_SEEK:
        pb_bool = (vlc_bool_t*)va_arg( args, vlc_bool_t* );
        *pb_bool = VLC_TRUE;
        break;
    case ACCESS_CAN_FASTSEEK:
        pb_bool = (vlc_bool_t*)va_arg( args, vlc_bool_t* );
        *pb_bool = VLC_TRUE;
        break;
    case ACCESS_CAN_PAUSE:
        pb_bool = (vlc_bool_t*)va_arg( args, vlc_bool_t* );
        *pb_bool = VLC_TRUE;
        break;
    case ACCESS_CAN_CONTROL_PACE:
        pb_bool = (vlc_bool_t*)va_arg( args, vlc_bool_t* );
        *pb_bool = VLC_TRUE;
        break;

    case ACCESS_GET_MTU:
        pi_int = (int*)va_arg( args, int * );
        *pi_int = 0;
        break;

    case ACCESS_GET_PTS_DELAY:
        pi_64 = (int64_t*)va_arg( args, int64_t * );
        *pi_64 = (int64_t)var_GetInteger( p_access, "smb-caching" ) * 1000;
        break;

    case ACCESS_SET_PAUSE_STATE:
        /* Nothing to do */
        break;

    case ACCESS_GET_TITLE_INFO:
    case ACCESS_SET_TITLE:
    case ACCESS_SET_SEEKPOINT:
    case ACCESS_SET_PRIVATE_ID_STATE:
        return VLC_EGENERIC;

    default:
        msg_Warn( p_access, "unimplemented query in control" );
        return VLC_EGENERIC;

    }

    return VLC_SUCCESS;
}
