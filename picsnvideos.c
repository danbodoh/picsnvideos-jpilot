/* $Id: picsnvideos.c,v 1.8 2008/05/17 03:13:07 danbodoh Exp $ */

/*******************************************************************************
 * picsnvideos.c
 *
 * Copyright (C) 2008 by Dan Bodoh
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 ******************************************************************************/

#include "config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include <pi-dlp.h>
#include <pi-source.h>
#include <pi-util.h>

#include <gdbm.h>

#include "libplugin.h"

#define MYNAME "Pics&Videos Plugin"
#define MYVERSION VERSION

char *rcsid = "$Id: picsnvideos.c,v 1.8 2008/05/17 03:13:07 danbodoh Exp $";

#define PCDIR "PalmPictures"
#define DATABASE_FILE "picsnvideos-fetched.gdbm"

#define L_DEBUG JP_LOG_DEBUG
#define L_GUI   JP_LOG_GUI
#define L_FATAL JP_LOG_FATAL

static char *const HELP_TEXT =
"%s %s JPilot plugin (c) 2008 by Dan Bodoh\n\
Contributor: Ulf Zibis <Ulf.Zibis@CoSoCo.de>\n\
\n\
Fetches pictures and videos from the Pics&Videos\n\
storage in the Palm to '%s'\n\
in your home directory.\n\
\n\
For more documentation, bug reports and new versions,\n\
see http://sourceforge.net/projects/picsnvideos.";

static char *const UNFILED_ALBUM = "Unfiled";

typedef struct VFSInfo VFSInfo;
typedef struct VFSDirInfo VFSDirInfo;
typedef struct PVAlbum {
    unsigned int volref;
    char root[vfsMAXFILENAME];
    char albumName[vfsMAXFILENAME];
    int isUnfiled;
    struct PVAlbum *next;
} PVAlbum;

PVAlbum *searchForAlbums(int, int *,  int);
void fetchAlbum(int, GDBM_FILE, PVAlbum *);
int vfsVolumeEnumerateIncludeHidden(int, int *, int *);

int ooM(void *ok) {
    if (!ok)
        jp_logf(L_FATAL, "[%s] Out of memory\n", MYNAME);
    return !ok;
}

void plugin_version(int *major_version, int *minor_version) {
   *major_version=0;
   *minor_version=99;
}

int plugin_get_name(char *name, int len) {
   strncpy(name, MYNAME, len);
   return 0;
}

int plugin_get_help_name(char *name, int len) {
    if (len > 6) {
        strcpy(name,"Help for ");
        strncat(name, MYNAME, len-10);
    } else {
        strncpy(name,"", len);
    }
    return 0;
}

int plugin_help(char **text, int *width, int *height) {

    *text = malloc(strlen(HELP_TEXT) + strlen(MYNAME) + strlen(MYVERSION) + strlen(PCDIR) + 20);

    if (*text==NULL) return 0;
    sprintf(*text, HELP_TEXT, MYNAME, MYVERSION, PCDIR);
    *height = 0;
    *width = 0;
    return 0;
}

int plugin_startup(jp_startup_info *info) {
    jp_init();
    return 0;
}

int plugin_sync(int sd) {
    int volrefs[16];
    int volcount = 16;
    PVAlbum *albumList = NULL;
    GDBM_FILE gdbmfh;

    jp_logf(L_GUI,"Fetching %s\n", MYNAME);
    jp_logf(L_DEBUG,"picsnvideos version %s (%s)\n", VERSION, rcsid);

    /* Get list of volumes on pilot. This function will find hidden
     * volumes, so that we also get the BUILTIN volume.
     */
    if (vfsVolumeEnumerateIncludeHidden(sd, &volcount, volrefs) < 0) {
        jp_logf(L_GUI,"[%s] Could not find any VFS volumes; no pictures fetched.\n", MYNAME);
        return -1;
    }

    /* Get list of albums on all the volumes. */
    if (!(albumList = searchForAlbums(sd, volrefs, volcount))) {
        jp_logf(L_GUI, "[%s] Could not list any albums; no pictures fetched.\n", MYNAME);
        return -1;
    }

    char gdbmfn[1024] = {""};
    jp_get_home_file_name(DATABASE_FILE, gdbmfn, sizeof(gdbmfn-1));
    if (!(gdbmfh = gdbm_open(gdbmfn, 0, GDBM_WRCREAT, 0600, NULL))) {
        jp_logf(L_FATAL, "[%s] Failed to open database '%s'\n", MYNAME, gdbmfn);
    }

    /* Iterate over each album, and fetch the files in that album. */
    while (albumList) {
        PVAlbum *tmp;
        fetchAlbum(sd, gdbmfh, albumList);
        tmp = albumList->next;
        free(albumList);
        albumList = tmp;
    }
    if (gdbmfh) {
        gdbm_close(gdbmfh);
        gdbmfh = NULL;
    }

    return 0;
}

int createDir(char *path, char *dir) {
    strcat(path,"/");
    strcat(path, dir);
    int result;
    if ((result = mkdir(path, 0777))) {
        if (errno != EEXIST) {
            jp_logf(L_FATAL,"[%s] Error: Could not create directory %s.\n", MYNAME, path);
            return result;
        }
    }
    return 0;
}

/*
 * Return directory name on the PC, where the album
 * should be stored. Returned string is of the form
 * "/home/danb/PalmPictures/Album/". Directories in
 * the path are created as needed.
 * Null is returned if out of memory.
 * Caller should free return value.
 */
char *destinationDir(int sd, PVAlbum *album) {
    char *dst;
    VFSInfo volInfo;
    char *home;

    // Use $HOME, or current directory if it is not defined.
    if (!(home = getenv("HOME"))) {
        home = "./";
    }

    // Next level is indicator of which card.
    char card[16];
    if (dlp_VFSVolumeInfo(sd, album->volref, &volInfo) < 0) {
        jp_logf(L_FATAL,"[%s] Error: Could not get volume info on volref %d.\n", MYNAME, album->volref);
        return NULL;
    }
    if (volInfo.mediaType == pi_mktag('T','F','F','S')) {
        strcpy(card, "Device");
    } else if (volInfo.mediaType == pi_mktag('s','d','i','g')) {
        strcpy(card, "SDCard");
    } else {
        sprintf(card,"card%d", volInfo.slotRefNum);
    }

    if (ooM(dst = malloc(strlen(home) + strlen(PCDIR) + strlen(card) + strlen(album->albumName) + 5))) {
        return dst;
    }
    // Create album directory if not existent.
    if (createDir(strcpy(dst, home), PCDIR) || createDir(dst, card) || createDir(dst, album->albumName)) {
        free(dst);
        return NULL;
    }
    return strcat(dst,"/"); // must be free'd by caller
}

/*
 * Return a key for the picsandvideos-fetched database.
 * Value must be free'd by caller.
 */
char *fetchedDatabaseKey(PVAlbum *album, char *file, unsigned int size) {
    char *key = malloc(strlen(file) + 64);
    if (key==NULL) return NULL;
    sprintf(key,"%s:%d", file,size);
    return key; // must be free'd by caller
}

void fetchFileIfNeeded(int sd, GDBM_FILE gdbmfh, PVAlbum *album, char *file, char *dstDir) {
    char *srcPath;
    FileRef fileRef;
    unsigned int filesize;
    int fetched = 0;
    datum key, val;
    const unsigned int buffersize = 65536;
    unsigned int readsize, writesize;
    pi_buffer_t  *buffer;
    int errorDuringFetch = 0;

    if (ooM(srcPath = malloc(strlen(album->root) + strlen(album->albumName) + strlen(file) + 10))) {
        return;
    }
    if (album->isUnfiled) {
        sprintf(srcPath,"%s/%s",album->root,file);
    } else {
        sprintf(srcPath,"%s/%s/%s",album->root,album->albumName,file);
    }

    if (dlp_VFSFileOpen(sd, album->volref, srcPath, vfsModeRead, &fileRef)<=0) {
          jp_logf(L_GUI,"[%s] Could not open file '%s' on volume %d\n", MYNAME, srcPath, album->volref);
          free(srcPath);
          return;
    }
    if (dlp_VFSFileSize(sd, fileRef, (int *)(&filesize)) < 0) {
        jp_logf(L_GUI,"[%s] Could not get file size '%s' on volume %d\n", MYNAME, srcPath, album->volref);
        free(srcPath);
        return;
    }
    free(srcPath);

    if (ooM(key.dptr = fetchedDatabaseKey(album, file, (unsigned int)filesize))) {
        return;
    }
    key.dsize = strlen(key.dptr);

    /* If file has not already been downloaded, fetch it. */
    if (! gdbm_exists(gdbmfh, key)) {
        char *dstfile;
        FILE *fp;

        if (ooM(dstfile = malloc(strlen(dstDir) + strlen(file) + 10))) {
            return;
        }
        strcpy(dstfile,dstDir);
        strcat(dstfile,file);

        jp_logf(L_GUI,"[%s]     Fetching %s...\n", MYNAME, dstfile);

        fp = fopen(dstfile,"w");
        if (fp==NULL) {
            jp_logf(L_FATAL,"[%s] Cannot open %s for writing!\n", MYNAME, dstfile);
            free(dstfile);
            return;
        }

        /* This copy code is based on pilot-xfer.c by Kenneth Albanowski. */
        buffer = pi_buffer_new(buffersize);
        readsize = 0;

        while ((filesize > 0) && (readsize >=0) && ! errorDuringFetch) {
            int offset;
            pi_buffer_clear(buffer);
            readsize = dlp_VFSFileRead(sd, fileRef, buffer, (filesize > buffersize ? buffersize : filesize));
            if (readsize <= 0 )  {
                jp_logf(L_FATAL,"[%s] File read error; aborting\n", MYNAME);
                errorDuringFetch = 1;
                break;
            }
            filesize -= readsize;

            offset = 0;
            while (readsize > 0) {
                writesize = fwrite(buffer->data+offset, 1, readsize, fp);
                if (writesize < 0) {
                    jp_logf(L_FATAL,"[%s] File write error; aborting\n", MYNAME);
                    errorDuringFetch = 1;
                    break;
                }
                readsize -= writesize;
                offset += writesize;
            }
        }
        fclose(fp);

        if (errorDuringFetch) {
            unlink(dstfile); // remove the partially created file
        } else {
            int status;
            time_t date;

            fetched = 1;
#ifdef HAVE_UTIME
            /* Get the date that the picture was created. */
            status = dlp_VFSFileGetDate(sd, fileRef,vfsFileDateCreated ,&date);
            /* And set the destination file mod time to that date. */
            if (status < 0) {
                jp_logf(L_GUI, "[%s] WARNING: Cannot get file date\n", MYNAME);
            } else {
                struct utimbuf t;
                t.actime = date;
                t.modtime = date;
                if (utime(dstfile, &t)!=0) {
                    jp_logf(L_GUI, "[%s] WARNING: Cannot set file date\n", MYNAME);
                }
            }
#endif // HAVE_UTIME
        }
        free(dstfile);
    } else {
        jp_logf(L_DEBUG,"[%s]     key '%s' exists, not copying file\n", MYNAME, key.dptr);
    }
    dlp_VFSFileClose(sd, fileRef);

    // Inform database, that file has been fetched.
    if (fetched) {
        int rv;
        val.dptr = "";
        val.dsize = 1;
        rv = gdbm_store(gdbmfh, key, val, GDBM_REPLACE);
    }
    free(key.dptr);
}

/*
 *  Free the list of albums and return it as NULL.
 */
PVAlbum *freeAlbumList(PVAlbum *albumList) {
    for (PVAlbum *tmp; (tmp = albumList);) {
        albumList = albumList->next;
        free(tmp);
    }
    return albumList; // is now NULL
}

/*
 *  Append new album to the list of albums and return it.
 */
PVAlbum *apendAlbum(PVAlbum *albumList, const char *name, const char *root, unsigned volref) {
    PVAlbum *newAlbum;

    if (ooM(newAlbum = malloc(sizeof(PVAlbum)))) {
        return freeAlbumList(albumList);
    }
    strncpy(newAlbum->albumName, name, vfsMAXFILENAME);
    newAlbum->albumName[vfsMAXFILENAME-1] = 0;
    strncpy(newAlbum->root, root, vfsMAXFILENAME);
    newAlbum->root[vfsMAXFILENAME-1] = 0;
    newAlbum->volref = volref;
    newAlbum->isUnfiled = (name == UNFILED_ALBUM) ? 1 : 0;
    jp_logf(L_DEBUG,"[%s]   Found album '%s'\n", MYNAME,  name);
    // Add new album to the growing list
    newAlbum->next = albumList;
    return newAlbum; // must be free'd by caller
}

/*
 *  Return a list of albums on all the volumes in volrefs.
 */
PVAlbum *searchForAlbums(int sd, int *volrefs, int volcount) {
    char *rootDirs[] = {"/DCIM", "/Photos & Videos"};
    FileRef dirRef;
    int maxDirItems = 1024;
    VFSDirInfo *dirInfo;
    PVAlbum *albumList = NULL;

    for (int i = 0; i < sizeof(rootDirs)/sizeof(*rootDirs); i++) {
        jp_logf(L_DEBUG,"[%s] Searching on Root %s\n", MYNAME, rootDirs[i]);
        for (int volumeIndex = 0; volumeIndex < volcount; volumeIndex++) {
            int volref = volrefs[volumeIndex];

            if (dlp_VFSFileOpen(sd, volref, rootDirs[i], vfsModeRead, &dirRef) <=0) {
                jp_logf(L_DEBUG,"[%s]  Root %s does not exist on volume %d\n", MYNAME, rootDirs[i], volref);
                continue;
            }
            jp_logf(L_DEBUG,"[%s]  Opened root %s on volume %d\n", MYNAME, rootDirs[i], volref);
            if (ooM(dirInfo = malloc(maxDirItems * sizeof(struct VFSDirInfo)))) {
                albumList = freeAlbumList(albumList);
                goto closeFile; // albumList is NULL
            }

            /* Add the unfiled album, which is simply the root dir.
             * Apparently the Treo 650 can store pics in the root dir,
             * as well as the album dirs.
             */
            if (!(albumList = apendAlbum(albumList, UNFILED_ALBUM, rootDirs[i], volref))) {
                goto freeDir; // albumList is NULL
            }

            /* Iterate through the root directory, looking for things
             * that might be albums.
             */
            unsigned long itr = (unsigned long)vfsIteratorStart;
            while ((enum dlpVFSFileIteratorConstants)itr != vfsIteratorStop) {
                dlp_VFSDirEntryEnumerate(sd, dirRef, &itr, &maxDirItems, dirInfo);
                for (int i=0; i<maxDirItems; i++) {
                    // Treo 650 has #Thumbnail dir that is not an album
                    if (strcmp(dirInfo[i].name,"#Thumbnail")==0)
                        continue;
                    if (dirInfo[i].attr & vfsFileAttrDirectory) {
                        if (!(albumList = apendAlbum(albumList, dirInfo[i].name, rootDirs[i], volref))) {
                            goto freeDir; // albumList is NULL
                        }
                    }
                }
            }
freeDir:    free(dirInfo);
closeFile:  dlp_VFSFileClose(sd, dirRef);
            if (!albumList) {
                return albumList; // Could not list any albums
            }
        }
    }
    return albumList; // must be free'd by caller
}

/*
 * Fetch the contents of one album.
 */
void fetchAlbum(int sd, GDBM_FILE gdbmfh, PVAlbum *album) {
    char *albumName = album->albumName;
    int maxDirItems = 1024;
    VFSDirInfo dirInfo[maxDirItems];
    char srcAlbumDir[strlen(album->root) + strlen(albumName) + 2];
    char *dstAlbumDir;
    FileRef dirRef;
    unsigned int volref = album->volref;

    jp_logf(L_GUI,"[%s]   Searching album '%s' on volume %d\n", MYNAME, albumName, volref);
    jp_logf(L_DEBUG,"[%s]     root=%s  albumName=%s  isUnfiled=%d\n", MYNAME, album->root, albumName, album->isUnfiled);

    strcpy(srcAlbumDir, album->root); // Album is in /<root>/<albunName>.
    // Unfiled album is really just root dir; this happens on Treo 65O.
    if (! album->isUnfiled) {
        strcat(srcAlbumDir, "/");
        strcat(srcAlbumDir, albumName);
    }
    if (dlp_VFSFileOpen(sd, volref, srcAlbumDir, vfsModeRead, &dirRef) <= 0) {
        jp_logf(L_GUI,"[%s] Could not open dir '%s' on volume %d\n", MYNAME, srcAlbumDir, volref);
        return;
    }
    if (!(dstAlbumDir = destinationDir(sd, album))) {
        return;
    }

    /* Iterate over all the files in the album dir, looking for
     * jpegs and 3gp's and 3g2's (videos).
     */
    unsigned long itr = (unsigned long)vfsIteratorStart;
    while ((enum dlpVFSFileIteratorConstants)itr != vfsIteratorStop) {
        dlp_VFSDirEntryEnumerate(sd, dirRef, &itr, &maxDirItems, dirInfo);
        for (int i=0; i<maxDirItems; i++) {
            unsigned long attr = dirInfo[i].attr;
            char *name = dirInfo[i].name;

            jp_logf(L_DEBUG,"[%s]       found file '%s' attribute %x\n", MYNAME, name, attr);
            // Grab only regular files, but ignore the 'read only' and 'archived' bits,
            // and only with known extensions.
            char *ext = name + strlen(name)-4;
            if (attr & (
                    vfsFileAttrHidden      |
                    vfsFileAttrSystem      |
                    vfsFileAttrVolumeLabel |
                    vfsFileAttrDirectory   |
                    vfsFileAttrLink) ||
                    strlen(name) < 5 ||
                    strcmp(ext, ".jpg") && // jpeg picture
                    strcmp(ext, ".3gp") && // video (GSM phones)
                    strcmp(ext, ".3g2") && // video (CDMA phones)
                    strcmp(ext, ".amr") && // audio caption (GSM phones)
                    strcmp(ext, ".qcp")) { // audio caption (CDMA phones)
                continue;
            }
            fetchFileIfNeeded(sd, gdbmfh, album, name, dstAlbumDir);
        }
    }
    dlp_VFSFileClose(sd, dirRef);
    free(dstAlbumDir);
}


/***********************************************************************
 *
 * Function:    vfsVolumeEnumerateIncludeHidden
 *
 * Summary:     Drop-in replacement for dlp_VFSVolumeEnumerate().
 *              Attempts to include hidden volumes in the list.
 *              Dan Bodoh, May 2, 2008
 *
 * Parameters:  sd     --> Socket descriptor
 *      volume_count   <-> on input, size of volumes; on output
 *                     number of volumes on Palm
 *      volumes        <-- volume reference numbers
 *
 * Returns:    Same as dlp_VFSVolumeEnumerate()
 *
 ***********************************************************************/
int vfsVolumeEnumerateIncludeHidden(int sd, int *numVols, int *volRefs) {
    int      volenumResult;
    int      result;
    int      volRefsSize = *numVols;
    VFSInfo  volInfo;
    int      volRef1Found;
    int      hiddenVolRef1Found;

    volenumResult = dlp_VFSVolumeEnumerate(sd, numVols, volRefs);
    if (volenumResult <= 0) {
        *numVols = 0;
    }
    /* On the Centro, it appears that the first non-hidden
     * volRef is 2, and the hidden volRef is 1. Let's poke
     * around to see. if there is really a volRef 1 that's
     * hidden from the dlp_VFSVolumeEnumerate().
     */
    volRef1Found = 0;
    hiddenVolRef1Found = 0;
    for (int i=0; i<*numVols; i++) {
        if (volRefs[i]==1) {
            volRef1Found = 1;
            break;
        }
    }
    if (volRef1Found) {
        return volenumResult;
    }
    else {
        result = dlp_VFSVolumeInfo(sd, 1, &volInfo);
        if (result > 0) {
            if (volInfo.attributes & vfsVolAttrHidden) {
                hiddenVolRef1Found = 1;
            }
        }
    }
    if (hiddenVolRef1Found) {
        ++ *numVols;
        if (volRefsSize >= *numVols) {
            volRefs[*numVols - 1] = 1;
        }
        if (volenumResult <= 0) return 4; // fake dlp_VFSVolumeEnumerate() return val with 1 volume
        else return volenumResult;
    }
    return volenumResult;
}
