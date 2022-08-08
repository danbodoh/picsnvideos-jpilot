/* $Id: picsnvideos.c,v 1.8 2008/05/17 03:13:07 danbodoh Exp $ */

/*******************************************************************************
 * picsnvideos.c
 *
 * Copyright (C) 2008 by Dan Bodoh
 * Contributor (2022): Ulf Zibis <Ulf.Zibis@CoSoCo.de>
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

#include "libplugin.h"

#define MYNAME "Pics&Videos Plugin"
#define MYVERSION VERSION

char *rcsid = "$Id: picsnvideos.c,v 1.8 2008/05/17 03:13:07 danbodoh Exp $";

#define PCDIR "PalmPictures"

#define L_DEBUG JP_LOG_DEBUG
#define L_GUI   JP_LOG_GUI
#define L_FATAL JP_LOG_FATAL

static const char HELP_TEXT[] =
"JPilot plugin (c) 2008 by Dan Bodoh\n\
Contributor (2022): Ulf Zibis <Ulf.Zibis@CoSoCo.de>\n\
Version: "MYVERSION"\n\
\n\
Fetches pictures and videos from the Pics&Videos\n\
storage in the Palm to folder '"PCDIR"'\n\
in your home directory.\n\
\n\
For more documentation, bug reports and new versions,\n\
see http://sourceforge.net/projects/picsnvideos";

static const unsigned MAX_VOLUMES = 16;
static const unsigned MIN_DIR_ITEMS = 4;
static const unsigned MAX_DIR_ITEMS = 1024;
static const char *ROOTDIRS[] = {"/Photos & Videos", "/DCIM"};
static const char UNFILED_ALBUM[] = "Unfiled";

typedef struct VFSInfo VFSInfo;
typedef struct VFSDirInfo VFSDirInfo;

int volumeEnumerateIncludeHidden(int, int *, int *);
void *mallocLog(size_t);
int backupMedia(int, int);
int fetchAlbum(int, const unsigned, const char *, const char *);

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
        strcpy(name, "Help for ");
        strncat(name, MYNAME, len-10);
    } else {
        strncpy(name, "", len);
    }
    return 0;
}

int plugin_help(char **text, int *width, int *height) {
    // Unfortunately JPilot app tries to free the *text memory,
    // so we must copy the text to new allocated heap memory first.
    if ((*text = mallocLog(strlen(HELP_TEXT) + 1))) {
        strcpy(*text, HELP_TEXT);
    }
    // *text = HELP_TEXT;  // Alternative causes crash !!!
    *height = 0;
    *width = 0;
    return 0;
}

int plugin_startup(jp_startup_info *info) {
    jp_init();
    return 0;
}

int plugin_sync(int sd) {
    int volrefs[MAX_VOLUMES];
    int volumes = MAX_VOLUMES;

    jp_logf(L_GUI, "Fetching %s\n", MYNAME);
    jp_logf(L_DEBUG, "picsnvideos version %s (%s)\n", VERSION, rcsid);

    // Get list of the volumes on the pilot.
    if (volumeEnumerateIncludeHidden(sd, &volumes, volrefs) < 0) {
        jp_logf(L_GUI, "[%s] Could not find any VFS volumes; no pictures fetched\n", MYNAME);
        return -1;
    }

    // Scan all the volumes for media and backup them.
    int result = -1;
    for (int i=0; i<volumes; i++) {
        if (backupMedia(sd, volrefs[i])) {
            jp_logf(L_GUI, "[%s] Could not find any media root on volume %d; no pictures fetched\n", MYNAME, volrefs[i]);
            continue;
        }
        result = 0;
    }
    return result;
}

/***********************************************************************
 *
 * Function:      volumeEnumerateIncludeHidden
 *
 * Summary:       Drop-in replacement for dlp_VFSVolumeEnumerate().
 *                Attempts to include hidden volumes in the list,
 *                so that we also get the device's BUILTIN volume.
 *                Dan Bodoh, May 2, 2008
 *
 * Parameters:
 *  sd            --> Socket descriptor
 *  volume_count  <-> on input, size of volumes; on output
 *                    number of volumes on Palm
 *  volumes       <-- volume reference numbers
 *
 * Returns:       <-- Same as dlp_VFSVolumeEnumerate()
 *
 ***********************************************************************/
int volumeEnumerateIncludeHidden(int sd, int *numVols, int *volRefs) {
    PI_ERR   bytes;
    VFSInfo  volInfo;

    // bytes on Treo 650:
    // -301 : No volume (SDCard) found, but maybe hidden volume 1 exists
    //    4 : At least one volume found, but maybe additional hidden volume 1 exists
    bytes = dlp_VFSVolumeEnumerate(sd, numVols, volRefs);
    jp_logf(L_DEBUG, "[%s] dlp_VFSVolumeEnumerate result code %d, found %d volumes\n", MYNAME, bytes, *numVols);
    // On the Centro, Treo 650 and maybe more, it appears that the
    // first non-hidden volRef is 2, and the hidden volRef is 1.
    // Let's poke around to see, if there is really a volRef 1
    // that's hidden from the dlp_VFSVolumeEnumerate().
    if (bytes < 0)  *numVols = 0; // On Error reset numVols
    for (int i=0; i<*numVols; i++) { // Search for volume 1
        jp_logf(L_DEBUG, "[%s] *numVols=%d, volRefs[%d]=%d\n", MYNAME, *numVols, i, volRefs[i]);
        if (volRefs[i]==1)
            goto Exit; // No need to search for hidden volume
    }
    if (dlp_VFSVolumeInfo(sd, 1, &volInfo) >= 0 && volInfo.attributes & vfsVolAttrHidden) {
        jp_logf(L_DEBUG, "[%s] Found hidden volume 1\n", MYNAME);
        if (*numVols < MAX_VOLUMES)  (*numVols)++;
        else {
            jp_logf(L_FATAL, "[%s] Volumes > %d were discarded\n", MYNAME, MAX_VOLUMES);
        }
        for (int i = (*numVols)-1; i > 0; i--) { // Move existing volRefs
            jp_logf(L_DEBUG, "[%s] *numVols=%d, volRefs[%d]=%d, volRefs[%d]=%d\n", MYNAME, *numVols, i-1, volRefs[i-1], i, volRefs[i]);
            volRefs[i] = volRefs[i-1];
        }
        volRefs[0] = 1;
        if (bytes < 0)
            bytes = 4; // fake dlp_VFSVolumeEnumerate() with 1 volume return value
    }
Exit:
    jp_logf(L_DEBUG, "[%s] volumeEnumerate final result code %d, found %d volumes\n", MYNAME, bytes, *numVols);
    return bytes;
}

void *mallocLog(size_t size) {
    void *p;
    if (!(p = malloc(size)))
        jp_logf(L_FATAL, "[%s] Out of memory\n", MYNAME);
    return p;
}

/*
 *  Backup all albums from volume volref.
 */
int backupMedia(int sd, int volref) {
    int result = -1;

    jp_logf(L_DEBUG, "[%s] Searching roots on volume %d\n", MYNAME, volref);
    for (int d = 0; d < sizeof(ROOTDIRS)/sizeof(*ROOTDIRS); d++) {

        // Fetch the unfiled album, which is simply the root dir.
        // Apparently the Treo 650 can store pics in the root dir, as well as in album dirs.
        if (!fetchAlbum(sd, volref, ROOTDIRS[d], UNFILED_ALBUM)) {
            result = 0;
        }

        // Iterate through the root directory, looking for things that might be albums.
        FileRef dirRef;
        if (dlp_VFSFileOpen(sd, volref, ROOTDIRS[d], vfsModeRead, &dirRef) < 0) {
            jp_logf(L_DEBUG, "[%s]  Root '%s' does not exist on volume %d\n", MYNAME, ROOTDIRS[d], volref);
            continue;
        }
        jp_logf(L_DEBUG, "[%s]  Opened root '%s' on volume %d\n", MYNAME, ROOTDIRS[d], volref);
        result = 0;

        // Workaround type mismatch bug <https://github.com/juddmon/jpilot/issues/39>
        unsigned long itr = (unsigned long)vfsIteratorStart;
        while ((enum dlpVFSFileIteratorConstants)itr != vfsIteratorStop) {
            int dirItems = 4;
            VFSDirInfo dirInfos[dirItems];
            jp_logf(L_DEBUG, "[%s]  Enumerate root '%s', dirRef=%lu, itr=%d, dirItems=%d\n", MYNAME, ROOTDIRS[d], dirRef, (int)itr, dirItems);
            PI_ERR bytes;
            if ((bytes = dlp_VFSDirEntryEnumerate(sd, dirRef, &itr, &dirItems, dirInfos)) < 0) {
                // Further research is neccessary (see: <https://github.com/juddmon/jpilot/issues/41> ):
                // - Why in case of i.e. setting dirItems=4, itr == vfsIteratorStop, even if there are more than 4 files?
                // - For workaround and additional bug on SDCard volume, see at fetchAlbum()
                jp_logf(L_FATAL, "[%s]  Enumerate ERROR: bytes=%d, dirRef=%lu, itr=%d, dirItems=%d\n", MYNAME, bytes, dirRef, (int)itr, dirItems);
                break;
            } else {
                jp_logf(L_DEBUG, "[%s]  Enumerate OK: bytes=%d, dirRef=%lu, itr=%d, dirItems=%d\n", MYNAME, bytes, dirRef, (int)itr, dirItems);
            }
            jp_logf(L_DEBUG, "[%s]  Now search for albums to fetch ...\n", MYNAME);
            for (int i=0; i<dirItems; i++) {
                jp_logf(L_DEBUG, "[%s]   Found album candidate '%s'\n", MYNAME,  dirInfos[i].name);
                // Treo 650 has #Thumbnail dir that is not an album
                if (dirInfos[i].attr & vfsFileAttrDirectory && strcmp(dirInfos[i].name, "#Thumbnail")) {
                //if (dirInfos[i].attr & vfsFileAttrDirectory) { // With thumbnails album
                    jp_logf(L_DEBUG, "[%s]   Found real album '%s'\n", MYNAME,  dirInfos[i].name);
                    fetchAlbum(sd, volref, ROOTDIRS[d], dirInfos[i].name);
                }
            }
        }
        dlp_VFSFileClose(sd, dirRef);
    }
    return result;
}

int createDir(char *path, const char *dir) {
    strcat(path, "/");
    strcat(path, dir);
    int result;
    if ((result = mkdir(path, 0777))) {
        if (errno != EEXIST) {
            jp_logf(L_FATAL, "[%s] Error: Could not create directory %s\n", MYNAME, path);
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
char *destinationDir(int sd, const unsigned volref, const char *name) {
    char *dst;
    VFSInfo volInfo;
    char *home;

    // Use $HOME, or current directory if it is not defined.
    if (!(home = getenv("HOME"))) {
        home = "./";
    }

    // Next level is indicator of which card.
    char card[16];
    if (dlp_VFSVolumeInfo(sd, volref, &volInfo) < 0) {
        jp_logf(L_FATAL, "[%s] Error: Could not get volume info on volref %d\n", MYNAME, volref);
        return NULL;
    }
    if (volInfo.mediaType == pi_mktag('T', 'F', 'F', 'S')) {
        strcpy(card, "Device");
    } else if (volInfo.mediaType == pi_mktag('s', 'd', 'i', 'g')) {
        strcpy(card, "SDCard");
    } else {
        sprintf(card, "card%d", volInfo.slotRefNum);
    }

    if (!(dst = mallocLog(strlen(home) + strlen(PCDIR) + strlen(card) + strlen(name) + 5))) {
        return dst;
    }
    // Create album directory if not existent.
    if (createDir(strcpy(dst, home), PCDIR) || createDir(dst, card) || createDir(dst, name)) {
        free(dst);
        return NULL;
    }
    return strcat(dst, "/"); // must be free'd by caller
}

/*
 * Fetch a file and backup it if not existent.
 */
int fetchFileIfNeeded(int sd, const unsigned volref, const char *root, const char *name, char *file, char *dstDir) {
    char srcPath[strlen(root) + strlen(name) + strlen(file) + 3];
    FileRef fileRef;
    unsigned int filesize;

    if (name == UNFILED_ALBUM) {
        sprintf(srcPath, "%s/%s", root, file);
    } else {
        sprintf(srcPath, "%s/%s/%s", root, name, file);
    }
    
    if (dlp_VFSFileOpen(sd, volref, srcPath, vfsModeRead, &fileRef) < 0) {
          jp_logf(L_GUI, "[%s]     Could not open file '%s' on volume %d\n", MYNAME, srcPath, volref);
          return -1;
    }
    if (dlp_VFSFileSize(sd, fileRef, (int *)(&filesize)) < 0) {
        jp_logf(L_GUI, "[%s]     Could not get file size '%s' on volume %d\n", MYNAME, srcPath, volref);
        return -1;
    }

    char dstfile[strlen(dstDir) + strlen(file) + 1];
    // Get full destination file path.
    strcpy(dstfile,dstDir);
    strcat(dstfile,file);
    struct stat fstat;
    int statErr = stat(dstfile, &fstat);

    if (!statErr && fstat.st_size == filesize) {
        jp_logf(L_DEBUG, "[%s]     File '%s' already exists, not copying it\n", MYNAME, dstfile);
    } else { // If file has not already been backuped, fetch it.
        if (!statErr) {
            jp_logf(L_DEBUG, "[%s]     File '%s' already exists, but has different size %d vs. %d\n", MYNAME, dstfile, fstat.st_size, filesize);
        }
        // Open destination file.
        FILE *fp;
        jp_logf(L_GUI, "[%s]     Fetching %s ...\n", MYNAME, dstfile);
        if (!(fp = fopen(dstfile, "w"))) {
            jp_logf(L_FATAL, "[%s]      Cannot open %s for writing!\n", MYNAME, dstfile);
            return -1;
        }

        // This copy code is based on pilot-xfer.c by Kenneth Albanowski.
        PI_ERR readsize = 0, writesize = 0;
        const unsigned int buffersize = 65536;
        pi_buffer_t *buffer = pi_buffer_new(buffersize);
        while ((filesize > 0) && (readsize >= 0)) {
            int offset;
            pi_buffer_clear(buffer);
            readsize = dlp_VFSFileRead(sd, fileRef, buffer, (filesize > buffersize ? buffersize : filesize));
            if (readsize < 0)  {
                jp_logf(L_FATAL, "[%s]      File read error; aborting\n", MYNAME);
                break;
            }
            filesize -= readsize;

            offset = 0;
            while (readsize > 0) {
                writesize = fwrite(buffer->data+offset, 1, readsize, fp);
                if (writesize < 0) {
                    jp_logf(L_FATAL, "[%s]      File write error; aborting\n", MYNAME);
                    break;
                }
                readsize -= writesize;
                offset += writesize;
            }
        }
        fclose(fp);

        if (readsize < 0 || writesize < 0) {
            unlink(dstfile); // remove the partially created file
        } else {
#ifdef HAVE_UTIME
            time_t date;
            statErr = 0;
            // Get the date that the picture was created (not the file), aka modified time.
            if (dlp_VFSFileGetDate(sd, fileRef, vfsFileDateModified, &date) < 0) {
                jp_logf(L_GUI, "[%s]     WARNING: Cannot get date of file '%s' on volume %d\n", MYNAME, srcPath, volref);
            // And set the destination file modified time to that date.
            } else if (!(statErr = stat(dstfile, &fstat))) {
                struct utimbuf utim;
                utim.actime = (time_t)fstat.st_atime;
                utim.modtime = date;
                statErr = utime(dstfile, &utim);
            }
            if (statErr) {
                jp_logf(L_GUI, "[%s]     WARNING: Cannot set date of file '%s'\n", MYNAME, dstfile);
            }
#endif // HAVE_UTIME
        }
    }
    dlp_VFSFileClose(sd, fileRef);
    return 0;
}

/*
 * Fetch the contents of one album and backup them if not existent.
 */
int fetchAlbum(int sd, const unsigned volref, const char *root, const char *name) {
    int dirItems = MIN_DIR_ITEMS;
    VFSDirInfo dirInfos[MAX_DIR_ITEMS];
    char tmp[strlen(root) + strlen(name) + 2];
    char *srcAlbumDir, *dstAlbumDir;
    FileRef dirRef;

    //strcpy(srcAlbumDir, root); // Album is in /<root>/<name>.
    // Unfiled album is really just root dir; this happens on Treo 65O.
    srcAlbumDir = (name == UNFILED_ALBUM) ? (char *)root : strcat(strcat(strcpy(tmp ,root), "/"), name);
    
    if (dlp_VFSFileOpen(sd, volref, srcAlbumDir, vfsModeRead, &dirRef) < 0) {
        jp_logf(L_GUI, "[%s]   Could not open %s '%s' on volume %d\n", MYNAME, (srcAlbumDir == root) ? "root" : "dir", srcAlbumDir, volref);
        return -1;
    }
    if (!(dstAlbumDir = destinationDir(sd, volref, name))) {
        jp_logf(L_GUI, "[%s]   Could not open dir '%s'\n", MYNAME, dstAlbumDir);
        return -1;
    }
    jp_logf(L_GUI, "[%s]   Fetching album '%s' in '%s' on volume %d ...\n", MYNAME, name, root, volref);

    // Iterate over all the files in the album dir, looking for jpegs and 3gp's and 3g2's (videos).
    
    // Workaround type mismatch bug <https://github.com/juddmon/jpilot/issues/39>
    unsigned long itr = (unsigned long)vfsIteratorStart;
    int loops = 16; // for debugging
    //while (itr != (unsigned long)vfsIteratorStop) {
    for (int dirItems_backup; (dirItems_backup = dirItems) <= MAX_DIR_ITEMS; dirItems *= 2) { // WORKAROUND
        if (--loops < 0)  break; // for debugging
        PI_ERR bytes;
        itr = (unsigned long)vfsIteratorStart; // workaround, if itr was -1 or 1888
        jp_logf(L_DEBUG, "[%s]    Enumerate album '%s', dirRef=%lu, itr=%d, dirItems=%d\n", MYNAME, srcAlbumDir, dirRef, (int)itr, dirItems);
        if ((bytes = dlp_VFSDirEntryEnumerate(sd, dirRef, &itr, &dirItems, dirInfos)) < 0) {
            // Further research is neccessary (see: <https://github.com/juddmon/jpilot/issues/41> ):
            // - Why in case of i.e. setting dirItems=4, itr != 0, even if there are more than 4 files?
            // - Why on SDCard itr == 1888, so out of allowed range?
            // - Why then on SDCard bytes is not negative, but operation freezes and logged: "caught signal SIGCHLD"?
            jp_logf(L_FATAL, "[%s]    Enumerate ERROR: bytes=%d, dirRef=%lu, itr=%d, dirItems=%d\n", MYNAME, bytes, dirRef, (int)itr, dirItems);
            break;
        }
        jp_logf(L_DEBUG, "[%s]    Enumerate OK: bytes=%d, dirRef=%lu, itr=%d, dirItems=%d\n", MYNAME, bytes, dirRef, (int)itr, dirItems);
        if (dirItems < dirItems_backup) {
            break;
        }
    }
    jp_logf(L_DEBUG, "[%s]    Now search of %d files, which to fetch ...\n", MYNAME, dirItems);
    for (int i=0; i<dirItems; i++) {
        char *fname = dirInfos[i].name;

        jp_logf(L_DEBUG, "[%s]     Found file '%s' attribute %x\n", MYNAME, fname, dirInfos[i].attr);
        // Grab only regular files, but ignore the 'read only' and 'archived' bits,
        // and only with known extensions.
        char *ext = fname + strlen(fname)-4;
        if (dirInfos[i].attr & (
                vfsFileAttrHidden      |
                vfsFileAttrSystem      |
                vfsFileAttrVolumeLabel |
                vfsFileAttrDirectory   |
                vfsFileAttrLink) ||
                strlen(name) < 5 || (
                //strcasecmp(ext, ".thb") &&  // thumbnail from album #Thumbnail (Treo 650)
                //strcasecmp(ext+1, ".db") && // DB file
                strcasecmp(ext, ".jpg") &&  // JPEG picture
                strcasecmp(ext, ".3gp") &&  // video (GSM phones)
                strcasecmp(ext, ".3g2") &&  // video (CDMA phones)
                strcasecmp(ext, ".amr") &&  // audio caption (GSM phones)
                strcasecmp(ext, ".qcp"))) { // audio caption (CDMA phones)
            continue;
        }
        fetchFileIfNeeded(sd, volref, root, name, fname, dstAlbumDir);
    }
    dlp_VFSFileClose(sd, dirRef);
    free(dstAlbumDir);
    return 0;
}
