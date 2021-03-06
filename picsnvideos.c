/* $Id: */

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

#include <pi-dlp.h>
#include <pi-source.h>
#include <pi-util.h>

#include <gdbm.h>


#include "libplugin.h"

#define MYNAME "Pics&Videos"
#define MYVERSION VERSION

#define DCIM "/DCIM"
#define PCDIR "PalmPictures"
#define DATABASE_FILE "picsnvideos-fetched.gdbm"

#define LOGL1 JP_LOG_WARN
#define LOGL2 JP_LOG_WARN
#define LOGL3 JP_LOG_FATAL

char *helpText = 
"%s %s JPilot plugin (c) 2008 by Dan Bodoh\n\
\n\
Fetches pictures and videos from the Pics&Videos\n\
application in the Palm to the directory '%s' in your.\n\
home directory.\n\
\n\
Downloads each picture only once, even if it is\n\
moved to a different album in the Palm.  To download\n\
all the pictures, delete the file\n\
'%s'.";

struct PVAlbum {
    unsigned int volref;
    char albumName[vfsMAXFILENAME+1];
    struct PVAlbum *next;
};


struct PVAlbum *searchForAlbums(int, int *,  int );
void fetchAlbum(int, GDBM_FILE, struct PVAlbum *);
int vfsVolumeEnumerateIncludeHidden(int, int *, int *);

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
    char hfn[512];
    jp_get_home_file_name(DATABASE_FILE, hfn, 511);

    *text = malloc(strlen(helpText)+strlen(MYNAME)+strlen(MYVERSION)+
                   +strlen(PCDIR) + strlen(hfn)+20);

    if (*text==NULL) return 0;
    sprintf(*text,helpText,MYNAME,MYVERSION,PCDIR,hfn);
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
    struct PVAlbum *albumList = NULL;
    char *gdbmfn;
    GDBM_FILE gdbmfh;

    jp_logf(LOGL2,"Fetching %s\n",MYNAME); 

    /* Get list of volumes on pilot.  This function will find hidden
     * volumes, so that we also get the BUILTIN volume
     */ 
    if (vfsVolumeEnumerateIncludeHidden(sd, &volcount, volrefs) < 0) {
        jp_logf(LOGL2,"Could not find any VFS volumes; no pictures fetched.\n",
                       MYNAME);
        return -1;
    }

    /* Get list of albums on all the volumes */
    albumList = searchForAlbums(sd, volrefs, volcount);
    
    if (albumList==NULL) {
        jp_logf(LOGL2, "Could not find any albums; no pictures fetched.\n");
        return -1;
    }

    gdbmfn = malloc(1024);
    if (gdbmfn==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return -1;
    }
    gdbmfn[0]=0;
    jp_get_home_file_name(DATABASE_FILE, gdbmfn, 1023);

    gdbmfh = gdbm_open(gdbmfn, 0, GDBM_WRCREAT, 0600, NULL);

    if (gdbmfh==NULL) {
        jp_logf(LOGL3, "Failed to open database file '%s'\n",gdbmfn);
    } 
    free(gdbmfn);

    /* Iterate over each album, and fetch the files in that album */
    while (albumList) {
        struct PVAlbum *tmp;
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

/*
 *  Return directory name on the PC where
 *  the album should be stored.  Returned string is of the form
 *  "/home/danb/PalmPictures/Album/".  Directories in
 *  the path are created as needed.  User should
 *  free return value.  Null is returned if out of memory.
 */
char *destinationDir(int sd, struct PVAlbum *album) {
    char *dst;
    int len;
    char *card;
    struct VFSInfo volInfo;
    char *home;

    /* Use $HOME, or current directory if it is not defined */
    home = getenv("HOME");
    if (home==NULL) {
        home = "./";
    }

    /* Next level is indicator of which card */
    if (dlp_VFSVolumeInfo(sd, album->volref, &volInfo) < 0) {
        jp_logf(LOGL3,"Error: Could not get volume info on volref %d\n",
                      album->volref);
        return NULL;
    }
    card = malloc(16);
    if (card==NULL) return NULL;
    if (volInfo.mediaType == pi_mktag('T','F','F','S')) {
        strncpy(card, "Device", 16);
    } else if (volInfo.mediaType == pi_mktag('s','d','i','g')) {
        strncpy(card, "SDCard", 16);
    } else {
        sprintf(card,"card%d",volInfo.slotRefNum);
    }
        
    len = strlen(home) + strlen(PCDIR) + strlen(album->albumName) + 
            strlen(card) + 10;
    dst = malloc(len);
    strcpy(dst, home);
    strcat(dst,"/");
    strcat(dst,PCDIR);

    /* make PCDIR directory */
    mkdir(dst, 0777);

    strcat(dst,"/");
    strcat(dst,card);
    free(card);

    /* make card directory */
    mkdir(dst, 0777);

    strcat(dst,"/");
    strcat(dst,album->albumName);

    /* make album directory */
    mkdir(dst, 0777);

    strcat(dst,"/");

    return dst;
}

/*
 *  Return a key for the picsandvideos-fetched database
 *  Value must be free'd by caller
 */
char *fetchedDatabaseKey(struct PVAlbum *album, char *file, 
                         unsigned int size) {
    char *key = malloc(strlen(file) + 64);
    if (key==NULL) return NULL;
    sprintf(key,"%s:%d", file,size);
    return key;
}

void fetchFileIfNeeded(int sd, GDBM_FILE gdbmfh, struct PVAlbum *album, 
                        char *file,
                       char *dstDir) {
    char *srcPath;
    FileRef fileRef;
    unsigned int filesize;
    int fetched = 0;
    datum key, val;
    const unsigned int buffersize = 65536;
    unsigned int readsize, writesize;
	pi_buffer_t  *buffer;
    int errorDuringFetch = 0;

    srcPath = malloc(strlen(DCIM)+strlen(album->albumName)+strlen(file)+10);
    if (srcPath==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return;
    }
    sprintf(srcPath,"%s/%s/%s",DCIM,album->albumName,file);
        
    if (dlp_VFSFileOpen(sd, album->volref, srcPath, vfsModeRead, &fileRef)<=0) {
          jp_logf(LOGL2,"Could not open file '%s' on volume %d\n",
                    srcPath, album->volref);
          free(srcPath);
          return;
    }
    if (dlp_VFSFileSize(sd, fileRef, (int *)(&filesize)) < 0) {
        jp_logf(LOGL2,"Could not get file size '%s' on volume %d\n",
                    srcPath, album->volref);
        free(srcPath);
        return;
    }
    free(srcPath);

    key.dptr = fetchedDatabaseKey(album, file, (unsigned int)filesize);
    key.dsize = strlen(key.dptr);
    if (key.dptr==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return;
    }

    /* If file has not already been downloaded, fetch it */
    if (! gdbm_exists(gdbmfh, key)) {
        char *dstfile;
        FILE *fp;
        

        dstfile = malloc(strlen(dstDir)+strlen(file)+10);
        if (dstfile==NULL) {
            jp_logf(LOGL3,"Out of memory\n");
            return;
        }
        strcpy(dstfile,dstDir);
        strcat(dstfile,file);

        jp_logf(LOGL2,"    Fetching %s...\n",dstfile);

        fp = fopen(dstfile,"w");
        if (fp==NULL) {
            jp_logf(LOGL3,"Cannot open %s for writing!\n",dstfile);
            free(dstfile);
            return;
        }
        
        /* This copy code is based on pilot-xfer.c  by Kenneth Albanowski */
        buffer = pi_buffer_new(buffersize);
        readsize = 0;

        while ((filesize > 0) && (readsize >=0) && ! errorDuringFetch) {
            int offset;
            pi_buffer_clear(buffer);
            readsize = dlp_VFSFileRead(sd, fileRef, buffer,
                          ( filesize > buffersize ? buffersize : filesize));
            if (readsize <= 0 )  {
                jp_logf(LOGL3,"File read error; aborting\n");
                errorDuringFetch = 1;
                break;
            }
            filesize -= readsize;
        
            offset = 0;
            while (readsize > 0) {
                writesize = fwrite(buffer->data+offset, 1, readsize, fp);
                if (writesize < 0) {
                    jp_logf(LOGL3,"File write error; aborting\n");
                    errorDuringFetch = 1;
                    break;
                }
                readsize -= writesize;
                offset += writesize;
            }
        }
        fclose(fp);
                    
        if (errorDuringFetch) {
            /* remove the partially created file */
            unlink(dstfile);
        } else {
            fetched = 1;
        }
        free(dstfile);
    }
    dlp_VFSFileClose(sd, fileRef);

    /* inform database that file has been fetched */
    if (fetched) {
        int rv;
        val.dptr = "";
        val.dsize = 1;
        rv = gdbm_store(gdbmfh, key, val, GDBM_REPLACE);
    }
    free(key.dptr);
}
    
/*
 *  Return a list of albums on all the volumes in volrefs
 *
 */
struct PVAlbum *searchForAlbums(int sd, int *volrefs,  int volcount) {
    FileRef dirRef;
    unsigned long dirIterator;
    int maxDirItems = 1024;
    struct VFSDirInfo *dirInfo;
    int i, volumeIndex;
    struct PVAlbum *albumList = NULL;

    for (volumeIndex = 0; volumeIndex < volcount; volumeIndex++) {
        int volref = volrefs[volumeIndex];

        /* Albums are in /DCIM */
        if (dlp_VFSFileOpen(sd, volref, DCIM, vfsModeRead, &dirRef)<=0) {
            jp_logf(LOGL2,"Could not open dir '%s' on volume %d\n",
                    DCIM, volref);
            continue;
        }

        //jp_logf(LOGL1,"Opened dir '%s' on volume %d\n", DCIM, volref);

        dirInfo  = malloc(maxDirItems * sizeof(struct VFSDirInfo));
        if (dirInfo==NULL) {
            jp_logf(LOGL3,"Out of memory\n");
            return NULL;
        }
    
        dirIterator = vfsIteratorStart;
        while (dirIterator != vfsIteratorStop) {
            dlp_VFSDirEntryEnumerate(sd, dirRef, &dirIterator, &maxDirItems, 
                                 dirInfo);
            for (i=0; i<maxDirItems; i++) {
                if (dirInfo[i].attr & vfsFileAttrDirectory) {
                    struct PVAlbum *newAlbum = malloc(sizeof(struct PVAlbum));
                    if (newAlbum==NULL) {
                        jp_logf(LOGL3,"Out of memory\n");
                        return NULL;
                    }
                    /* Add a new album to growing list */
                    newAlbum->next = albumList;
                    albumList = newAlbum;
                    strncpy(newAlbum->albumName,dirInfo[i].name,vfsMAXFILENAME);
                    newAlbum->volref = volref;
                    //jp_logf(LOGL1,"Found album '%s' on volref %d\n",
                    //                newAlbum->albumName,volref);
                }
            }
        }
        free(dirInfo);
        dlp_VFSFileClose(sd, dirRef);
    }
    return albumList;
}

/*
 * Fetch the contents of one album
 *
 */
void fetchAlbum(int sd, GDBM_FILE gdbmfh, struct PVAlbum *album) {
    int maxDirItems = 1024;
    struct VFSDirInfo *dirInfo;
    char *srcAlbumDir;
    char *dstAlbumDir;
    unsigned long dirIterator;
    FileRef dirRef;

    jp_logf(LOGL2,"  Searching for new pictures and videos in the '%s' album on volume %d\n",album->albumName, album->volref);

    srcAlbumDir = malloc(strlen(DCIM)+strlen(album->albumName)+10);
    if (srcAlbumDir==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return;
    }
    /* Album is in /DCIM/<albunName> */
    strcpy(srcAlbumDir,DCIM);
    strcat(srcAlbumDir,"/");
    strcat(srcAlbumDir,album->albumName);

    if (dlp_VFSFileOpen(sd, album->volref, srcAlbumDir, vfsModeRead, &dirRef)<=0) {
        jp_logf(LOGL2,"Could not open dir '%s' on volume %d\n",
                    srcAlbumDir, album->volref);
        return;
    }
    dirInfo  = malloc(maxDirItems * sizeof(struct VFSDirInfo));
    if (dirInfo==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return;
    }
    
    dstAlbumDir = destinationDir(sd, album);
    if (dstAlbumDir==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return;
    }
        
    
    dirIterator = vfsIteratorStart;
    while (dirIterator != vfsIteratorStop) {
        int i;

        dlp_VFSDirEntryEnumerate(sd, dirRef, &dirIterator, &maxDirItems, 
                                 dirInfo);
        for (i=0; i<maxDirItems; i++) {
            int fnlen = strlen(dirInfo[i].name);

            /* Must end in .jpg or .3gp (videos) extension */
            if ( ! (strcmp(&(dirInfo[i].name[fnlen-4]), ".jpg") ||
                    strcmp(&(dirInfo[i].name[fnlen-4]), ".3gp"))) {

                    continue;
            }
            /* must be a regular file, but ignore the 'archived' bit */
            if ( dirInfo[i].attr & 
                    (vfsFileAttrHidden      |
                     vfsFileAttrSystem      |
                     vfsFileAttrVolumeLabel |
                     vfsFileAttrDirectory   |
                     vfsFileAttrLink)) {

                    continue;
            }

            fetchFileIfNeeded(sd, gdbmfh, album, dirInfo[i].name, dstAlbumDir);
                                 
        }
    }
    free(dirInfo);
    dlp_VFSFileClose(sd, dirRef);
    free(srcAlbumDir);
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
 * Parameters:  sd             --> Socket descriptor
 *		volume_count   <-> on input, size of volumes; on output
 *			           number of volumes on Palm
 *		volumes        <-- volume reference numbers
 *
 * Returns:    Same as dlp_VFSVolumeEnumerate() 
 *
 ***********************************************************************/
int vfsVolumeEnumerateIncludeHidden(int sd, int *numVols, int *volRefs) {
	int			    volenumResult;
	int			    result;
	int			    volRefsSize = *numVols;
	struct VFSInfo		    volInfo;
	int			    volRef1Found;
	int			    hiddenVolRef1Found;
	int			    i;

	volenumResult = dlp_VFSVolumeEnumerate(sd, numVols, volRefs);
	if (volenumResult <= 0) {
	    *numVols = 0;
	}
	/* On the Centro, it appears that the first non-hidden
	 * volRef is 2, and the hidden volRef is 1.  Let's poke
	 * around to see if there is really a volRef 1 that's
	 * hidden from the dlp_VFSVolumeEnumerate()
	 */
	volRef1Found = 0;
	hiddenVolRef1Found = 0;
	for (i=0; i<*numVols; i++) {
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
		if (volenumResult <= 0) return 4; /* fake dlp_VFSVolumeEnumerate() return val with 1 volume */
		else return volenumResult;
	}
	return volenumResult;
}
