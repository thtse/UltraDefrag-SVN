/*
 *  UltraDefrag - a powerful defragmentation tool for Windows NT.
 *  Copyright (c) 2007-2017 Dmitri Arkhangelski (dmitriar@gmail.com).
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/**
 * @file defrag.c
 * @brief Volume defragmentation.
 * @addtogroup Defrag
 * @{
 */

/*
* Ideas by Stefan Pendl <stefanpe@users.sourceforge.net>
* and Dmitri Arkhangelski <dmitriar@gmail.com>.
*/

#include "udefrag-internals.h"

static ULONGLONG defrag_cc_routine(udefrag_job_parameters *jp);

/************************************************************/
/*                       Test suite                         */
/************************************************************/

/*
* Uncomment it to test defragmentation
* of various special files like reparse
* points, attribute lists and others.
*/
//#define TEST_SPECIAL_FILES_DEFRAG

/* Test suite for special files. */
#ifdef TEST_SPECIAL_FILES_DEFRAG
void test_move(winx_file_info *f,udefrag_job_parameters *jp)
{
    winx_volume_region *target_rgn;
    ULONGLONG source_lcn = f->disp.blockmap->lcn;
    
    /* try to move the first cluster to the last free region */
    target_rgn = find_last_free_region(jp,1);
    if(target_rgn == NULL){
        etrace("no free region found on disk");
        return;
    }
    if(move_file(f,f->disp.blockmap->vcn,1,target_rgn->lcn,jp) < 0){
        etrace("move failed for %ws",f->path);
        return;
    } else {
        dtrace("move succeeded for %ws",f->path);
    }
    /* try to move the first cluster back */
    if(can_move(f,jp->is_fat)){
        if(move_file(f,f->disp.blockmap->vcn,1,source_lcn,jp) < 0){
            etrace("move failed for %ws",f->path);
            return;
        } else {
            dtrace("move succeeded for %ws",f->path);
        }
    } else {
        etrace("file became unmovable %ws",f->path);
    }
    /* release temporarily allocated space */
    release_temp_space_regions(jp);
}

/*
* Tests defragmentation of reparse points,
* encrypted files, bitmaps and attribute lists.
*/
void test_special_files_defrag(udefrag_job_parameters *jp)
{
    winx_file_info *f;
    int special_file = 0;
    
    dtrace("test of special files defragmentation started");

    for(f = jp->filelist; f; f = f->next){
        if(can_move(f,jp->is_fat)){
            special_file = 0;
            if(is_reparse_point(f)){
                dtrace("reparse point detected: %ws",f->path);
                special_file = 1;
            } else if(is_encrypted(f)){
                dtrace("encrypted file detected: %ws",f->path);
                special_file = 1;
            } else if(winx_wcsistr(f->path,L"$BITMAP")){
                dtrace("bitmap detected: %ws",f->path);
                special_file = 1;
            } else if(winx_wcsistr(f->path,L"$ATTRIBUTE_LIST")){
                dtrace("attribute list detected: %ws",f->path);
                special_file = 1;
            }
            if(special_file)
                test_move(f,jp);
        }
        if(f->next == jp->filelist) break;
    }
    
    dtrace("test of special files defragmentation completed");
}
#endif /* TEST_SPECIAL_FILES_DEFRAG */

/************************************************************/
/*                   Auxiliary routines                     */
/************************************************************/

/**
 * @internal
 * @brief Defines whether a file can be defragmented or not.
 */
static int can_defragment(winx_file_info *f,udefrag_job_parameters *jp)
{
    if(!can_move(f, jp->fs_type))
        return 0;

    /* skip not fragmented files */
    if(!is_fragmented(f))
        return 0;
        
    /* skip MFT */
    if(is_mft(f,jp->fs_type))
        return 0;
    
    /* skip FAT directories */
    if(jp->is_fat && is_directory(f))
        return 0;
        
    /* in MFT optimization defragment marked files only */
    if(jp->job_type == MFT_OPTIMIZATION_JOB \
      && !is_fragmented_by_file_opt(f))
        return 0;
    
    return 1;
}

/**
 * @internal
 * @brief build_fragments_list helper.
 */
static winx_blockmap *add_fragment(winx_blockmap **fragments,
    winx_blockmap **prev_fragment, ULONGLONG vcn, ULONGLONG lcn,
    ULONGLONG length)
{
    winx_blockmap *fragment;
    
    fragment = (winx_blockmap *)winx_list_insert((list_entry **)(void *)fragments,
        (list_entry *)*prev_fragment,sizeof(winx_blockmap));
    fragment->vcn = vcn;
    fragment->lcn = lcn;
    fragment->length = length;
    *prev_fragment = fragment;
    return fragment;
}

/**
 * @internal
 * @brief Enumerates fragments of a file.
 */
winx_blockmap *build_fragments_list(winx_file_info *f,ULONGLONG *n_fragments)
{
    winx_blockmap *block, *p = NULL, *fragments = NULL;
    ULONGLONG vcn = 0, lcn = 0, length = 0;
    
    if(n_fragments) *n_fragments = 0;
    
    for(block = f->disp.blockmap; block; block = block->next){
        if(block == f->disp.blockmap){
            vcn = block->vcn;
            lcn = block->lcn;
            length = block->length;
        } else {
            if(block->lcn == block->prev->lcn + block->prev->length){
                length += block->length;
            } else {
                if(length){
                    if(!add_fragment(&fragments,&p,vcn,lcn,length))
                        break;
                    if(n_fragments) (*n_fragments) ++;
                }
                vcn = block->vcn;
                lcn = block->lcn;
                length = block->length;
            }
        }
        if(block->next == f->disp.blockmap) break;
    }
    
    if(length){
        if(add_fragment(&fragments,&p,vcn,lcn,length)){
            if(n_fragments) (*n_fragments) ++;
        }
    }
    
    if(fragments == NULL && n_fragments) *n_fragments = 0;
    return fragments;
}

/**
 * @internal
 * @brief Releases a list of file fragments.
 */
void release_fragments_list(winx_blockmap **fragments)
{
    winx_list_destroy((list_entry **)(void *)fragments);
}

/**
 * @internal
 * @brief Clears UD_FILE_CURRENTLY_EXCLUDED flag for all files.
 */
void clear_currently_excluded_flag(udefrag_job_parameters *jp)
{
    winx_file_info *f;

    for(f = jp->filelist; f; f = f->next){
        f->user_defined_flags &= ~UD_FILE_CURRENTLY_EXCLUDED;
        if(f->next == jp->filelist) break;
    }
}

/**
 * @internal
 * @brief Calculates number of clusters which
 * need to be moved to complete defragmentation.
 */
static ULONGLONG defrag_cc_routine(udefrag_job_parameters *jp)
{
    struct prb_traverser t;
    winx_file_info *file;
    ULONGLONG n = 0;
    
    /* fine calculation will take too much time */
    prb_t_init(&t,jp->fragmented_files);
    file = prb_t_first(&t,jp->fragmented_files);
    while(file){
        if(jp->termination_router((void *)jp)) break;
        /* count all fragmented files which can be processed */
        if(can_defragment(file,jp)) n += file->disp.clusters;
        file = prb_t_next(&t);
    }
    return n;
}

/**
 * @internal
 * @brief Eliminates little fragments respect
 * to the fragment size threshold filter.
 */
static void defrag_routine(udefrag_job_parameters *jp)
{
    winx_volume_region *rgn, *largest_rgn;
    struct prb_traverser t;
    winx_file_info *file, *next_file;
    int move_entirely;
    ULONGLONG defragmented_files;
    ULONGLONG defragmented_entirely = 0, defragmented_partially = 0;
    ULONGLONG x, moved_entirely = 0, moved_partially = 0;
    ULONGLONG min_vcn, max_vcn; /* used to avoid infinite loops */
    winx_blockmap *fragments, *fr, *fr2, *next_fr, *head_fr;
    ULONGLONG vcn, length, n, new_min_vcn;
    ULONGLONG cut_length;
    int defrag_succeeded;
    char buffer[32];

    winx_dbg_print_header(0,0,I"defragmentation pass #%u",++jp->pi.pass_number);
    jp->pi.current_operation = VOLUME_DEFRAGMENTATION;
    jp->pi.moved_clusters = 0;

    /* release temporarily allocated space */
    release_temp_space_regions(jp);

    /* no files are excluded by this task currently */
    clear_currently_excluded_flag(jp);

    jp->pi.clusters_to_process = \
        jp->pi.processed_clusters + defrag_cc_routine(jp);
        
    /*
    dtrace(">>> %I64u\\%I64u <<<",
        jp->pi.processed_clusters,jp->pi.clusters_to_process);
    */

    /*
    * Eliminate little fragments. Defragment
    * the most fragmented files first of all.
    */
    defragmented_files = 0;
    prb_t_init(&t,jp->fragmented_files);
    file = prb_t_first(&t,jp->fragmented_files);
    while(file){
        if(jp->termination_router((void *)jp)) break;
        next_file = prb_t_next(&t);
        if(can_defragment(file,jp)){
            move_entirely = 0;
            if(file->disp.clusters * jp->v_info.bytes_per_cluster \
              < 2 * jp->udo.fragment_size_threshold) move_entirely = 1;
            else if(jp->win_version < WINDOWS_XP && jp->fs_type == FS_NTFS)
                move_entirely = 1; /* keep algorithm simple */
            if(move_entirely){
                /* move the entire file */
                rgn = find_first_free_region(jp,0,file->disp.clusters,NULL);
                if(rgn){
                    x = jp->pi.moved_clusters;
                    if(move_file(file,file->disp.blockmap->vcn,
                     file->disp.clusters,rgn->lcn,jp) >= 0){
                        if(jp->udo.dbgprint_level >= DBG_DETAILED)
                            itrace("Defrag success for %ws",file->path);
                        defragmented_files ++;
                        defragmented_entirely ++;
                        moved_entirely += (jp->pi.moved_clusters - x);
                    } else {
                        etrace("Defrag failure for %ws",file->path);
                    }
                }
            } else {
                /* eliminate little fragments */
                min_vcn = file->disp.blockmap->vcn;
                max_vcn = file->disp.blockmap->prev->vcn + \
                    file->disp.blockmap->prev->length;
                defrag_succeeded = 0;
                x = jp->pi.moved_clusters;
                while(min_vcn < max_vcn && can_defragment(file,jp)){
                    /* build list of fragments */
                    fragments = build_fragments_list(file,NULL);
                    if(fragments == NULL) break;
                    
                    /* cut off already processed fragments and data after max_vcn */
                    for(fr = fragments; fr; fr = next_fr){
                        head_fr = fragments;
                        next_fr = fr->next;
                        if(fr->vcn < min_vcn || (fr->vcn + fr->length > max_vcn))
                            winx_list_remove((list_entry **)(void *)&fragments,(list_entry *)(void *)fr);
                        if(fragments == NULL) goto completed;
                        if(next_fr == head_fr) break;
                    }
                    
                    /* how much clusters can we join together? */
                    largest_rgn = find_largest_free_region(jp);
                    if(largest_rgn == NULL) break;
                    
                    /* find clusters needing optimization */
                    vcn = length = n = new_min_vcn = 0;
                    for(fr = fragments; fr; fr = fr->next){
                        /* find the first little fragment */
                        if(fr->length * jp->v_info.bytes_per_cluster < jp->udo.fragment_size_threshold){
                            if(fr->length >= largest_rgn->length) break;
                            vcn = fr->vcn;
                            length = fr->length, n++;
                            new_min_vcn = fr->vcn + fr->length;
                            /* look forward for the next little fragments */
                            for(fr2 = fr->next; fr2 != fragments; fr2 = fr2->next){
                                if(fr2->length * jp->v_info.bytes_per_cluster >= jp->udo.fragment_size_threshold)
                                    break;
                                if(length + fr2->length > largest_rgn->length) goto move_clusters;
                                length += fr2->length, n++;
                                new_min_vcn = fr2->vcn + fr2->length;
                            }
                            if(largest_rgn->length * jp->v_info.bytes_per_cluster < jp->udo.fragment_size_threshold) break;
                            if(length * jp->v_info.bytes_per_cluster < jp->udo.fragment_size_threshold){
                                cut_length = jp->udo.fragment_size_threshold / jp->v_info.bytes_per_cluster;
                                if(cut_length * jp->v_info.bytes_per_cluster != jp->udo.fragment_size_threshold)
                                    cut_length ++;
                                cut_length -= length;
                                if(fr2 != fragments){
                                    /* let's cut from the next fragment */
                                    if((fr2->length - cut_length) * jp->v_info.bytes_per_cluster \
                                      < jp->udo.fragment_size_threshold){
                                        length += fr2->length, n++;
                                        new_min_vcn = fr2->vcn + fr2->length;
                                    } else {
                                        length += cut_length, n++;
                                        new_min_vcn = fr2->vcn + cut_length;
                                    }
                                } else if(fr != fragments){
                                    /* let's cut from the previous fragment */
                                    if((fr->prev->length - cut_length) * jp->v_info.bytes_per_cluster \
                                      < jp->udo.fragment_size_threshold){
                                        vcn = fr->prev->vcn;
                                        length += fr->prev->length, n++;
                                    } else {
                                        vcn = fr->prev->vcn + (fr->prev->length - cut_length);
                                        length += cut_length, n++;
                                    }
                                }
                            }
                            break;
                        }
                        if(fr->next == fragments) break;
                    }
                    
move_clusters:                    
                    /* move clusters */
                    if(length == 0 || n < 2){
                        min_vcn = max_vcn;
                    } else {
                        rgn = find_first_free_region(jp,0,length,NULL);
                        if(rgn){
                            if(move_file(file,vcn,length,rgn->lcn,jp) >= 0){
                                if(jp->udo.dbgprint_level >= DBG_DETAILED)
                                    itrace("Defrag success for %ws",file->path);
                                defrag_succeeded = 1;
                            } else {
                                etrace("Defrag failure for %ws",file->path);
                            }
                        }
                        min_vcn = new_min_vcn;
                    }
                    
                    /* release list of fragments */
                    release_fragments_list(&fragments);
                }
                if(defrag_succeeded){
                    defragmented_files ++;
                    defragmented_partially ++;
                    moved_partially += (jp->pi.moved_clusters - x);
                }
            }
        }
completed:
        file->user_defined_flags |= UD_FILE_CURRENTLY_EXCLUDED;
        file = next_file;
    }
    
    /*
    dtrace(">>> %I64u\\%I64u <<<",
        jp->pi.processed_clusters,jp->pi.clusters_to_process);
    */

    /* display amount of moved data and number of defragmented files */
    itrace("%I64u files defragmented",defragmented_files);
    itrace("  %I64u clusters moved",jp->pi.moved_clusters);
    winx_bytes_to_hr(jp->pi.moved_clusters * jp->v_info.bytes_per_cluster,1,buffer,sizeof(buffer));
    itrace("  %s moved",buffer);
    
    itrace("%I64u files defragmented entirely",defragmented_entirely);
    itrace("  %I64u clusters moved",moved_entirely);
    winx_bytes_to_hr(moved_entirely * jp->v_info.bytes_per_cluster,1,buffer,sizeof(buffer));
    itrace("  %s moved",buffer);
    itrace("%I64u files defragmented partially",defragmented_partially);
    itrace("  %I64u clusters moved",moved_partially);
    winx_bytes_to_hr(moved_partially * jp->v_info.bytes_per_cluster,1,buffer,sizeof(buffer));
    itrace("  %s moved",buffer);
    
    /* cleanup */
    clear_currently_excluded_flag(jp);
}

/**
 * @internal
 * @brief Defragments the disk once.
 */
static void defrag_sequence(udefrag_job_parameters *jp)
{
    do {
        if(jp->pi.fragmented == 0) return;
        if(jp->termination_router((void *)jp)) return;
        defrag_routine(jp);
    } while(jp->pi.moved_clusters);
    
    /* defragment remaining files partially */
    if(jp->udo.fragment_size_threshold == DEFAULT_FRAGMENT_SIZE_THRESHOLD){
        jp->udo.fragment_size_threshold = PART_DEFRAG_MAGIC_CONSTANT;
        jp->udo.algorithm_defined_fst = 1;
        itrace("partial defragmentation: fragment size threshold = %I64u",
            jp->udo.fragment_size_threshold);
        do {
            if(jp->pi.fragmented == 0) break;
            if(jp->termination_router((void *)jp)) break;
            defrag_routine(jp);
        } while(jp->pi.moved_clusters);
        jp->udo.fragment_size_threshold = DEFAULT_FRAGMENT_SIZE_THRESHOLD;
        jp->udo.algorithm_defined_fst = 0;
    }
}

/************************************************************/
/*                    The entry point                       */
/************************************************************/

/**
 * @internal
 * @brief Defragments the disk.
 * @details To avoid infinite data moves in multipass
 * processing, we exclude files for which moving failed.
 * On the other hand, number of fragmented files instantly
 * decreases, so we'll never have infinite loops here.
 * @return Zero for success, negative value otherwise.
 */
int defragment(udefrag_job_parameters *jp)
{
    int result;
    struct prb_traverser t;
    winx_file_info *file;
    int second_attempt = 0;
    ULONGLONG time;
    
    if(jp->job_type == DEFRAGMENTATION_JOB){
        /* analyze the disk */
        result = analyze(jp); /* we need to call it once, here */
        if(result < 0) return result;
        if(jp->termination_router((void *)jp)) return 0;
    #ifdef TEST_SPECIAL_FILES_DEFRAG
        test_special_files_defrag(jp);
        return 0;
    #endif
        /* check fragmentation level */
        if(!check_fragmentation_level(jp))
            return 0;
        /* reset counters */
        jp->pi.processed_clusters = 0;
        jp->pi.clusters_to_process = 0;
    }
    
    time = start_timing("defragmentation",jp);

    /* do the job */
    defrag_sequence(jp);
    
    /*
    * Some files haven't been moved because target
    * space turned out to be already in use. So, 
    * let's give those files another chance.
    */
    prb_t_init(&t,jp->fragmented_files);
    file = prb_t_first(&t,jp->fragmented_files);
    while(file){
        if(jp->termination_router((void *)jp)) break;
        if(is_moving_failed(file)){
            second_attempt = 1;
            file->user_defined_flags &= ~UD_FILE_MOVING_FAILED;
        }
        file = prb_t_next(&t);
    }
    if(second_attempt) defrag_sequence(jp);
    
    stop_timing("defragmentation",time,jp);
    return 0;
}

/** @} */
