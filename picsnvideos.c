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

#define MYNAME "Pics&Videos"
#define MYVERSION VERSION

char *RootDirs[] = {
    "/DCIM",
    "/Photos & Videos",
    NULL
};

char *rcsid = "$Id: picsnvideos.c,v 1.8 2008/05/17 03:13:07 danbodoh Exp $";

#define PCDIR "PalmPictures"
#define DATABASE_FILE "picsnvideos-fetched.gdbm"
#define UNFILED_ALBUM "Unfiled"

#define LOGL1 JP_LOG_DEBUG
#define LOGL2 JP_LOG_GUI
#define LOGL3 JP_LOG_FATAL

char *helpText = 
"%s %s JPilot plugin (c) 2008 by Dan Bodoh\n\
Contributor: Ulf Zibis <Ulf.Zibis@CoSoCo.de>\n\
\n\
Fetches pictures and videos from the Pics&Videos\n\
application in the Palm to the directory '%s'\n\
in your home directory.\n\
\n\
For more documentation, bug reports and new versions,\n\
see http://sourceforge.net/projects/picsnvideos.";

struct PVAlbum {
    unsigned int volref;
    char root[vfsMAXFILENAME+1];
    char albumName[vfsMAXFILENAME+1];
    int isUnfiled;
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

    *text = malloc(strlen(helpText)+strlen(MYNAME)+strlen(MYVERSION)+
                   +strlen(PCDIR) +20);

    if (*text==NULL) return 0;
    sprintf(*text,helpText,MYNAME,MYVERSION,PCDIR);
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
    jp_logf(LOGL1,"picsnvideos version %s (%s)\n",VERSION, rcsid);

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

    srcPath = malloc(strlen(album->root)+strlen(album->albumName)+
                     strlen(file)+10);
    if (srcPath==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return;
    }
    if (album->isUnfiled) {
        sprintf(srcPath,"%s/%s",album->root,file);
    } else {
        sprintf(srcPath,"%s/%s/%s",album->root,album->albumName,file);
    }
        
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
            int status;
            time_t date;

            fetched = 1;

#ifdef HAVE_UTIME
            /* Get the date that the picture was created */
            status = dlp_VFSFileGetDate(sd, fileRef,vfsFileDateCreated ,&date);
            /* And set the destination file mod time to that date */
            if (status < 0) {
                jp_logf(LOGL2, "WARNING: Cannot get file date\n");
            } else {
                struct utimbuf t;
                t.actime = date;
                t.modtime = date;
                if (utime(dstfile, &t)!=0) {
                    jp_logf(LOGL2, "WARNING: Cannot set file date\n");
                }
            }
#endif // HAVE_UTIME
            
        }
        free(dstfile);
    } else {
        jp_logf(LOGL1,"    key '%s' exists, not copying file\n", key.dptr);
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
    int rootIndex = 0;
    struct PVAlbum *newAlbum;

    while (RootDirs[rootIndex] != NULL) {
        for (volumeIndex = 0; volumeIndex < volcount; volumeIndex++) {
            int volref = volrefs[volumeIndex];
    
            if (dlp_VFSFileOpen(sd, volref, RootDirs[rootIndex], 
                                vfsModeRead, &dirRef)<=0) {
                jp_logf(LOGL1," Root %s does not exist on volume %d\n", RootDirs[rootIndex],
                    volref);
                continue;
            }
            jp_logf(LOGL1," Opened root %s on volume %d\n", RootDirs[rootIndex],
                    volref);
    
            dirInfo  = malloc(maxDirItems * sizeof(struct VFSDirInfo));
            if (dirInfo==NULL) {
                jp_logf(LOGL3,"Out of memory\n");
                return NULL;
            }
        
            /* Add the unfiled album, which is simply the root dir. 
             * Apparently the Treo 650 can store pics in the root dir,
             * as well as the album dirs
             */
            newAlbum = malloc(sizeof(struct PVAlbum));
            if (newAlbum==NULL) {
                jp_logf(LOGL3,"Out of memory\n");
                return NULL;
            }
            newAlbum->next = albumList;
            albumList = newAlbum;
            newAlbum->albumName[0]=0;
            strncpy(newAlbum->albumName,UNFILED_ALBUM, vfsMAXFILENAME);
            strncpy(newAlbum->root,RootDirs[rootIndex], vfsMAXFILENAME);
            newAlbum->volref = volref;
            newAlbum->isUnfiled = 1;
            
            /* Iterate through the root directory looking for things
             * that might be albums
             */
            dirIterator = (unsigned long)vfsIteratorStart;
            while ((enum dlpVFSFileIteratorConstants)dirIterator != vfsIteratorStop) {
                dlp_VFSDirEntryEnumerate(sd, dirRef, &dirIterator, &maxDirItems, 
                                     dirInfo);
                for (i=0; i<maxDirItems; i++) {
                    // Treo 650 has #Thumbnail dir that is not an album
                    if (strcmp(dirInfo[i].name,"#Thumbnail")==0)
                        continue;
                    if (dirInfo[i].attr & vfsFileAttrDirectory) {
                        newAlbum = malloc(sizeof(struct PVAlbum));
                        if (newAlbum==NULL) {
                            jp_logf(LOGL3,"Out of memory\n");
                            return NULL;
                        }
                        /* Add a new album to growing list */
                        newAlbum->next = albumList;
                        albumList = newAlbum;
                        strncpy(newAlbum->albumName,dirInfo[i].name,
                                vfsMAXFILENAME);
                        strncpy(newAlbum->root,RootDirs[rootIndex],
                                vfsMAXFILENAME);
                        newAlbum->volref = volref;
                        newAlbum->isUnfiled = 0;
                        jp_logf(LOGL1,"  Found album '%s'\n", 
                                        newAlbum->albumName);
                    }
                }
            }
            free(dirInfo);
            dlp_VFSFileClose(sd, dirRef);
        }
        ++rootIndex;
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

    jp_logf(LOGL2,"  Searching album '%s' on volume %d\n",
            album->albumName, album->volref);
    jp_logf(LOGL1,"    root=%s  albumName=%s  isUnfiled=%d\n",
                   album->root, album->albumName, album->isUnfiled);

    srcAlbumDir = malloc(strlen(album->root)+strlen(album->albumName)+10);
    if (srcAlbumDir==NULL) {
        jp_logf(LOGL3,"Out of memory\n");
        return;
    }
    /* Album is in /<root>/<albunName> */
    strcpy(srcAlbumDir,album->root);

    /* Unfiled album is really just root dir; this happens on Treo 65O */
    if (! album->isUnfiled) {
        strcat(srcAlbumDir,"/");
        strcat(srcAlbumDir,album->albumName);
    }

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
        
    
    /* Iterate over all the files in the album dir, looking for
     * jpegs and 3gp's and 3g2's (videos)
     */
    dirIterator = (unsigned long)vfsIteratorStart;
    while ((enum dlpVFSFileIteratorConstants)dirIterator != vfsIteratorStop) {
        int i;

        dlp_VFSDirEntryEnumerate(sd, dirRef, &dirIterator, &maxDirItems, 
                                 dirInfo);
        for (i=0; i<maxDirItems; i++) {
            int fnlen = strlen(dirInfo[i].name);

            jp_logf(LOGL1,"      found file '%s' attribute %x\n",
                    dirInfo[i].name, dirInfo[i].attr);
            if (fnlen < 5) continue;

            /* Grab only files with known extensions */

            if ( /* .jpg = jpeg picture */
                 (strcmp(&(dirInfo[i].name[fnlen-4]), ".jpg") != 0) &&
                 /* .3gp is video (GSM phones) */
                 (strcmp(&(dirInfo[i].name[fnlen-4]), ".3gp") != 0) &&
                 /* .3g2 is video (CDMA phones) */
                 (strcmp(&(dirInfo[i].name[fnlen-4]), ".3g2") != 0) &&
                 /* .amr is audio caption (GSM phones) */
                 (strcmp(&(dirInfo[i].name[fnlen-4]), ".amr") != 0) &&
                 /* .qcp is audio caption (CDMA phones) */
                 (strcmp(&(dirInfo[i].name[fnlen-4]), ".qcp") != 0)) { 

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
