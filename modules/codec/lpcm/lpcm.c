/*****************************************************************************
 * lpcm.c: lpcm decoder module
 *****************************************************************************
 * Copyright (C) 1999-2001 VideoLAN
 * $Id: lpcm.c,v 1.3 2002/08/30 22:22:24 massiot Exp $
 *
 * Authors: Samuel Hocevar <sam@zoy.org>
 *          Henri Fallon <henri@videolan.org>
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
#include <string.h>                                    /* memcpy(), memset() */
#include <stdlib.h>                                      /* malloc(), free() */

#include <vlc/vlc.h>
#include <vlc/aout.h>
#include <vlc/decoder.h>

#ifdef HAVE_UNISTD_H
#   include <unistd.h>                                           /* getpid() */
#endif

#include "lpcm.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  OpenDecoder    ( vlc_object_t * );
static int  RunDecoder     ( decoder_fifo_t * );

       void DecodeFrame    ( lpcmdec_thread_t * );
static int  InitThread     ( lpcmdec_thread_t * );
static void EndThread      ( lpcmdec_thread_t * );

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin();
    set_description( _("linear PCM audio decoder") );
    set_capability( "decoder", 100 );
    set_callbacks( OpenDecoder, NULL );
vlc_module_end();

/*****************************************************************************
 * OpenDecoder: probe the decoder and return score
 *****************************************************************************/
static int OpenDecoder( vlc_object_t *p_this )
{
    decoder_fifo_t *p_fifo = (decoder_fifo_t*) p_this;

    if( p_fifo->i_fourcc != VLC_FOURCC('l','p','c','m')
         && p_fifo->i_fourcc != VLC_FOURCC('l','p','c','b') )
    {   
        return VLC_EGENERIC;
    }
    
    p_fifo->pf_run = RunDecoder;
    return VLC_SUCCESS;
}

/*****************************************************************************
 * RunDecoder: the lpcm decoder
 *****************************************************************************/
static int RunDecoder( decoder_fifo_t * p_fifo )
{
    lpcmdec_thread_t *   p_lpcmdec;

    /* Allocate the memory needed to store the thread's structure */
    if( (p_lpcmdec = (lpcmdec_thread_t *)malloc (sizeof(lpcmdec_thread_t)) )
            == NULL) 
    {
        msg_Err( p_fifo, "out of memory" );
        DecoderError( p_fifo );
        return( -1 );
    }

    /*
     * Initialize the thread properties
     */
    p_lpcmdec->p_fifo = p_fifo;

    if( InitThread( p_lpcmdec ) )
    {
        DecoderError( p_fifo );
        free( p_lpcmdec );
        return( -1 );
    }

    /* lpcm decoder thread's main loop */
    while ((!p_lpcmdec->p_fifo->b_die) && (!p_lpcmdec->p_fifo->b_error))
    {
        DecodeFrame(p_lpcmdec);
    }

    /* If b_error is set, the lpcm decoder thread enters the error loop */
    if (p_lpcmdec->p_fifo->b_error)
    {
        DecoderError( p_lpcmdec->p_fifo );
    }

    /* End of the lpcm decoder thread */
    EndThread (p_lpcmdec);

    return( 0 );
}

/*****************************************************************************
 * InitThread : initialize an lpcm decoder thread
 *****************************************************************************/
static int InitThread (lpcmdec_thread_t * p_lpcmdec)
{

    /* Init the BitStream */
    InitBitstream( &p_lpcmdec->bit_stream, p_lpcmdec->p_fifo,
                   NULL, NULL);

    /* Creating the audio output fifo */
    p_lpcmdec->p_aout_fifo =
                aout_CreateFifo( p_lpcmdec->p_fifo, AOUT_FIFO_PCM,
                                 2, 48000, LPCMDEC_FRAME_SIZE / 2, NULL  );
    if ( p_lpcmdec->p_aout_fifo == NULL )
    {
        return( -1 );
    }
    return( 0 );
}

/*****************************************************************************
 * DecodeFrame: decodes a frame.
 *****************************************************************************/
void DecodeFrame( lpcmdec_thread_t * p_lpcmdec )
{
    byte_t *    buffer;
#ifndef WORDS_BIGENDIAN
    byte_t *    p_temp[LPCMDEC_FRAME_SIZE];
#endif
    vlc_bool_t  b_sync;
    int         i_loop;

    CurrentPTS( &p_lpcmdec->bit_stream,
        &p_lpcmdec->p_aout_fifo->date[p_lpcmdec->p_aout_fifo->i_end_frame],
        NULL );
    if( !p_lpcmdec->p_aout_fifo->date[p_lpcmdec->p_aout_fifo->i_end_frame] )
    {
        p_lpcmdec->p_aout_fifo->date[p_lpcmdec->p_aout_fifo->i_end_frame] =
            LAST_MDATE;
    }

    buffer = ((byte_t *)p_lpcmdec->p_aout_fifo->buffer) + 
              (p_lpcmdec->p_aout_fifo->i_end_frame * LPCMDEC_FRAME_SIZE);

    RemoveBits32(&p_lpcmdec->bit_stream);
#if 0
    byte1 = GetBits(&p_lpcmdec->bit_stream, 8) ;
    byte2 = GetBits(&p_lpcmdec->bit_stream, 8) ;
    
    /* I only have 2 test streams. As far as I understand
     * after the RemoveBits and the 2 GetBits, we should be exactly 
     * where we want : the sync word : 0x0180.
     * If not, we go and find it. */
    while( ( byte1 != 0x01 || byte2 != 0x80 ) && (!p_lpcmdec->p_fifo->b_die)
                                       && (!p_lpcmdec->p_fifo->b_error) )
    {
        byte1 = byte2;
        byte2 = GetBits(&p_lpcmdec->bit_stream, 8);
    }
#else
    b_sync = 0;
    while( ( !p_lpcmdec->p_fifo->b_die ) &&
           ( !p_lpcmdec->p_fifo->b_error ) &&
           ( !b_sync ) )
    {
        while( ( !p_lpcmdec->p_fifo->b_die ) &&
               ( !p_lpcmdec->p_fifo->b_error ) &&
               ( GetBits( &p_lpcmdec->bit_stream, 8 ) != 0x01 ) );
        b_sync = ( ShowBits( &p_lpcmdec->bit_stream, 8 ) == 0x80 );
    }
    RemoveBits( &p_lpcmdec->bit_stream, 8 );
#endif
    
#ifndef WORDS_BIGENDIAN
    GetChunk( &p_lpcmdec->bit_stream, p_temp, LPCMDEC_FRAME_SIZE);
    if( p_lpcmdec->p_fifo->b_die || p_lpcmdec->p_fifo->b_error ) return;

#   ifdef HAVE_SWAB
    swab( buffer, p_temp, LPCMDEC_FRAME_SIZE );
#   else
    for( i_loop = 0; i_loop < LPCMDEC_FRAME_SIZE/2; i_loop++ )
    {
        buffer[2*i_loop]=p_temp[2*i_loop+1];
        buffer[2*i_loop+1]=p_temp[2*i_loop];
    }
#   endif

#else
    GetChunk( &p_lpcmdec->bit_stream, buffer, LPCMDEC_FRAME_SIZE);
    if( p_lpcmdec->p_fifo->b_die ) return;
#endif

    vlc_mutex_lock (&p_lpcmdec->p_aout_fifo->data_lock);
    p_lpcmdec->p_aout_fifo->i_end_frame = 
        (p_lpcmdec->p_aout_fifo->i_end_frame + 1) & AOUT_FIFO_SIZE;
    vlc_cond_signal (&p_lpcmdec->p_aout_fifo->data_wait);
    vlc_mutex_unlock (&p_lpcmdec->p_aout_fifo->data_lock);
}

/*****************************************************************************
 * EndThread : lpcm decoder thread destruction
 *****************************************************************************/
static void EndThread( lpcmdec_thread_t * p_lpcmdec )
{
    /* If the audio output fifo was created, we destroy it */
    if( p_lpcmdec->p_aout_fifo != NULL ) 
    {
        aout_DestroyFifo( p_lpcmdec->p_aout_fifo );

        /* Make sure the output thread leaves the NextFrame() function */
        vlc_mutex_lock( &(p_lpcmdec->p_aout_fifo->data_lock) );
        vlc_cond_signal( &(p_lpcmdec->p_aout_fifo->data_wait) );
        vlc_mutex_unlock( &(p_lpcmdec->p_aout_fifo->data_lock) );
    }

    /* Destroy descriptor */
    free( p_lpcmdec );
}
