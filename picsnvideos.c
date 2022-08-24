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
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include <pi-dlp.h>
#include <pi-source.h>
#include <pi-util.h>

#include "libplugin.h"
//#include "i18n.h"

#define MYNAME "Pics&Videos"
#define PCDIR "Media"

#define L_DEBUG JP_LOG_DEBUG
#define L_INFO  JP_LOG_INFO // Unfortunately doesn't show up in GUI
#define L_WARN  JP_LOG_WARN
#define L_FATAL JP_LOG_FATAL
#define L_GUI   JP_LOG_GUI

typedef struct VFSInfo VFSInfo;
typedef struct VFSDirInfo VFSDirInfo;
typedef struct fileType {char ext[16]; struct fileType *next;} fileType;

static const char rcsid[] = "$Id: picsnvideos.c,v 1.8 2008/05/17 03:13:07 danbodoh Exp $";

static const char HELP_TEXT[] =
"JPilot plugin (c) 2008 by Dan Bodoh\n\
Contributor (2022): Ulf Zibis <Ulf.Zibis@CoSoCo.de>\n\
Version: "VERSION"\n\
\n\
Fetches media as pictures, videos and audios from the\n\
Pics&Videos storage in the Palm and from SDCard to\n\
folder '"PCDIR"' in your JPilot data directory,\n\
usually \"$JPILOT_HOME/.jpilot\".\n\
\n\
For more documentation, bug reports and new versions,\n\
see https://github.com/danbodoh/picsnvideos-jpilot";

static const unsigned MAX_VOLUMES = 16;
static const unsigned MIN_DIR_ITEMS = 2;
static const unsigned MAX_DIR_ITEMS = 1024;
static const char *ROOTDIRS[] = {"/Photos & Videos", "/Fotos & Videos", "/DCIM"};
static char PCPATH[256];
static const char *PREFS_FILE = "picsnvideos.rc";
static prefType PREFS[] = {
    {"synchThumbnailsAlbum", INTTYPE, INTTYPE, 0, NULL, 0},
    // JPEG picture
    // video (GSM phones)
    // video (CDMA phones)
    // audio caption (GSM phones)
    // audio caption (CDMA phones)
    {"fileTypes", CHARTYPE, CHARTYPE, 0, ".jpg.3gp.3g2.amr.qcp" , 256}
};
static const unsigned NUM_PREFS = sizeof(PREFS)/sizeof(prefType);
static long synchThumbnailsAlbum;
static char *fileTypes;
static fileType *fileTypeList = NULL;

void *mallocLog(size_t);
int volumeEnumerateIncludeHidden(const int, int *, int *);
int backupVolume(const int, int);

void plugin_version(int *major_version, int *minor_version) {
    *major_version = 0;
    *minor_version = 99;
    jp_logf(L_DEBUG, "picsnvideos version %s (%s)\n", VERSION, rcsid);
}

int plugin_get_name(char *name, int len) {
    //snprintf(name, len, "%s %d.%d", MYNAME, PLUGIN_MAJOR, PLUGIN_MINOR);
    snprintf(name, len, "%s %s", MYNAME, VERSION);
    return EXIT_SUCCESS;
}

int plugin_get_help_name(char *name, int len) {
    //g_snprintf(name, len, _("About %s"), _(MYNAME)); // With language support.
    //return EXIT_SUCCESS;
    snprintf(name, len, "About %s", MYNAME);
    return EXIT_SUCCESS;
}

int plugin_help(char **text, int *width, int *height) {
    // Unfortunately JPilot app tries to free the *text memory,
    // so we must copy the text to new allocated heap memory first.
    if ((*text = mallocLog(strlen(HELP_TEXT) + 1))) {
        strcpy(*text, HELP_TEXT);
    }
    // *text = HELP_TEXT;  // alternative, causes crash !!!
    *height = 0;
    *width = 0;
    return EXIT_SUCCESS;
}

int plugin_startup(jp_startup_info *info) {
    int result = EXIT_SUCCESS;
    jp_init();
    jp_pref_init(PREFS, NUM_PREFS);
    if (jp_pref_read_rc_file(PREFS_FILE, PREFS, NUM_PREFS) < 0)
        jp_logf(L_WARN, "%s: WARNING: Could not read PREFS from '%s'\n", MYNAME, PREFS_FILE);
    if (jp_get_pref(PREFS, 0, &synchThumbnailsAlbum, NULL) < 0)
        jp_logf(L_WARN, "%s: WARNING: Could not read pref '%s' from PREFS[]\n", MYNAME, PREFS[0].name);
    if (jp_get_pref(PREFS, 1, NULL, (const char **)&fileTypes) < 0)
        jp_logf(L_WARN, "%s: WARNING: Could not read pref '%s' from PREFS[]\n", MYNAME, PREFS[1].name);
    if (jp_pref_write_rc_file(PREFS_FILE, PREFS, NUM_PREFS) < 0) // To initialize with defaults, if pref file wasn't existent.
        jp_logf(L_WARN, "%s: WARNING: Could not write PREFS to '%s'\n", MYNAME, PREFS_FILE);
    for (char *last; (last = strrchr(fileTypes, '.')) >= fileTypes; *last = 0) {
        fileType *ftype;
        if (strlen(last) < sizeof(ftype->ext) && (ftype = mallocLog(sizeof(*ftype)))) {
            strcpy(ftype->ext, last);
            ftype->next = fileTypeList;
            fileTypeList = ftype;
        } else {
            plugin_exit_cleanup();
            result = EXIT_FAILURE;
            break;
        }
    }
    jp_free_prefs(PREFS, NUM_PREFS);
    return result;
}

int plugin_sync(int sd) {
    int volRefs[MAX_VOLUMES];
    int volumes = MAX_VOLUMES;

    jp_logf(L_GUI, "%s: Start syncing ...", MYNAME);
    jp_logf(L_DEBUG, "\n");

    // Get list of the volumes on the pilot.
    if (volumeEnumerateIncludeHidden(sd, &volumes, volRefs) < 0) {
        jp_logf(L_FATAL, "\n%s: ERROR: Could not find any VFS volumes; no media fetched\n", MYNAME);
        return EXIT_FAILURE;
    }
    // Use $JPILOT_HOME/.jpilot/ or current directory for PCDIR.
    if (jp_get_home_file_name(PCDIR, PCPATH, 256) < 0) {
        jp_logf(L_WARN, "\n%s: WARNING: Could not get $JPILOT_HOME path, so using './%s'\n", MYNAME, PCDIR);
        strcpy(PCPATH, PCDIR);
    } else {
        jp_logf(L_GUI, " with '%s'\n", PCPATH);
    }
    // Check if there are any file types loaded.
    if (!fileTypeList) {
        jp_logf(L_FATAL, "%s: ERROR: Could not find any file types from '%s'; no media fetched\n", MYNAME, PREFS_FILE);
        return EXIT_FAILURE;
    }

    // Scan all the volumes for media and backup them.
    PI_ERR result = EXIT_FAILURE;
    for (int i=0; i<volumes; i++) {
        PI_ERR volResult;
        if ((volResult = backupVolume(sd, volRefs[i])) < 0) {
            jp_logf(L_WARN, "%s: WARNING: Could not find any media on volume %d; no media fetched\n", MYNAME, volRefs[i]);
            jp_logf(L_DEBUG, "%s: Result from volume %d: %d\n", MYNAME, volRefs[i], volResult);
            continue;
        }
        result = EXIT_SUCCESS;
    }
    jp_logf(L_DEBUG, "%s: Sync done -> result=%d\n", MYNAME, result);
    return result;
}

int plugin_exit_cleanup(void) {
    for (fileType *tmp; (tmp = fileTypeList);) {
        fileTypeList = fileTypeList->next;
        free(tmp);
    }
    return EXIT_SUCCESS;
}

void *mallocLog(size_t size) {
    void *p;
    if (!(p = malloc(size)))
        jp_logf(L_FATAL, "%s: ERROR: Out of memory\n", MYNAME);
    return p;
}

int createDir(char *path, const char *dir) {
    if (dir == PCPATH)  strcpy(path, PCPATH);
    else  strcat(strcat(path, "/"), dir);
    int result;
    if ((result = mkdir(path, 0777))) {
        if (errno != EEXIST) {
            jp_logf(L_FATAL, "%s:     ERROR: Could not create directory %s\n", MYNAME, path);
            return result;
        }
    }
    return 0;
}

/*
 * Return directory name on the PC, where the album should be stored. Returned string is of the form
 * "$JPILOT_HOME/.jpilot/$PCDIR/Album/". Directories in the path are created as needed.
 * Null is returned if out of memory.
 * Caller should free return value.
 */
char *destinationDir(const int sd, const unsigned volRef, const char *name) {
    char *path;
    VFSInfo volInfo;

    if (!(path = mallocLog(256))) {
        return path;
    }

    // Get indicator of which card.
    char card[16];
    if (dlp_VFSVolumeInfo(sd, volRef, &volInfo) < 0) {
        jp_logf(L_FATAL, "%s:     ERROR: Could not get volume info from volRef %d\n", MYNAME, volRef);
        return NULL;
    }
    if (volInfo.mediaType == pi_mktag('T', 'F', 'F', 'S')) {
        strcpy(card, "Internal");
    } else if (volInfo.mediaType == pi_mktag('s', 'd', 'i', 'g')) {
        strcpy(card, "SDCard");
    } else {
        sprintf(card, "card%d", volInfo.slotRefNum);
    }

    // Create album directory if not existent.
    if (createDir(path, PCPATH) || createDir(path, card) || (name ? createDir(path, name) : 0)) {
        free(path);
        return NULL;
    }
    return path; // must be free'd by caller
}

/*
 * Fetch a file and backup it, if not existent.
 */
int fetchFileIfNeeded(const int sd, const unsigned volRef, const char *srcDir, const char *dstDir, const char *file) {
    char srcPath[strlen(srcDir) + strlen(file) + 2];
    char dstPath[strlen(dstDir) + strlen(file) + 2];
    FileRef fileRef;
    int filesize; // also serves as error return code

    strcat(strcat(strcpy(srcPath, srcDir), "/"), file);
    strcat(strcat(strcpy(dstPath, dstDir), "/"), file);

    if (dlp_VFSFileOpen(sd, volRef, srcPath, vfsModeRead, &fileRef) < 0) {
          jp_logf(L_FATAL, "%s:      ERROR: Could not open file '%s' on volume %d\n", MYNAME, srcPath, volRef);
          return -1;
    }
    if (dlp_VFSFileSize(sd, fileRef, (int *)(&filesize)) < 0) {
        jp_logf(L_WARN, "%s:      WARNING: Could not get size of '%s' on volume %d, so anyway fetch it.\n", MYNAME, srcPath, volRef);
    }

    struct stat fstat;
    int statErr = stat(dstPath, &fstat);
    if (!statErr && fstat.st_size == filesize) {
        jp_logf(L_DEBUG, "%s:      File '%s' already exists, not copying it\n", MYNAME, dstPath);
    } else { // If file has not already been backuped, fetch it.
        if (!statErr) {
            jp_logf(L_DEBUG, "%s:      File '%s' already exists, but has different size %d vs. %d\n", MYNAME, dstPath, fstat.st_size, filesize);
        }
        // Open destination file.
        FILE *dstFp;
        jp_logf(L_GUI, "%s:      Fetching %s ...", MYNAME, dstPath);
        if (!(dstFp = fopen(dstPath, "w"))) {
            jp_logf(L_FATAL, "\n%s:       ERROR: Cannot open %s for writing!\n", MYNAME, dstPath);
            return -1;
        }
        // Copy file.
        for (pi_buffer_t *buf = pi_buffer_new(65536); filesize > 0; filesize -= buf->used) {
            pi_buffer_clear(buf);
            if (dlp_VFSFileRead(sd, fileRef, buf, (filesize > buf->allocated ? buf->allocated : filesize)) < 0)  {
            //if (dlp_VFSFileRead(sd, fileRef, buf, buf->allocated) < 0)  { // works too, but is very slow
                jp_logf(L_FATAL, "\n%s:       ERROR: File read error; aborting\n", MYNAME);
                filesize = -1; // remember error
                break;
            }
            for (int writesize, offset = 0; offset < buf->used; offset += writesize) {
                if ((writesize = fwrite(buf->data + offset, 1, buf->used - offset, dstFp)) < 0) {
                    jp_logf(L_FATAL, "\n%s:       ERROR: File write error; aborting\n", MYNAME);
                    filesize = writesize; // remember error; breaks the outer loop
                    break;
                }
            }
        }
        fclose(dstFp);
        if (filesize < 0) {
            unlink(dstPath); // remove the partially created file
        } else {
            jp_logf(L_GUI, " OK\n");
            time_t date;
            // Get the date that the picture was created (not the file), aka modified time.
            if (dlp_VFSFileGetDate(sd, fileRef, vfsFileDateModified, &date) < 0) {
                jp_logf(L_WARN, "%s:      WARNING: Cannot get date of file '%s' on volume %d\n", MYNAME, srcPath, volRef);
                statErr = 0; // reset old state
            // And set the destination file modified time to that date.
            } else if (!(statErr = stat(dstPath, &fstat))) {
                //jp_logf(L_DEBUG, "%s:       modified: %s", MYNAME, ctime(&date));
                struct utimbuf utim;
                utim.actime = (time_t)fstat.st_atime;
                utim.modtime = date;
                statErr = utime(dstPath, &utim);
            }
            if (statErr) {
                jp_logf(L_WARN, "%s:      WARNING: Cannot set date of file '%s', ErrCode=%d\n", MYNAME, dstPath, statErr);
            }
        }
    }
    dlp_VFSFileClose(sd, fileRef);
    jp_logf(L_DEBUG, "%s:      File size / copy result of '%s': %d, statErr=%d\n", MYNAME, dstPath, filesize, statErr);
    return filesize;
}

int casecmpFileTypeList(char *fname) {
    char *ext = strrchr(fname, '.');
    int result = 1;
    for (fileType *tmp = fileTypeList; ext && tmp; tmp = tmp->next) {
        if (!(result = strcasecmp(ext, tmp->ext)))  break;
    }
    return result;
}

/*
 * Fetch the contents of one album and backup them if not existent.
 */
int fetchAlbum(const int sd, const unsigned volRef, FileRef dirRef, const char *root, const char *name) {
    char tmp[name ? strlen(root) + strlen(name) + 2 : 0];
    char *srcAlbumDir, *dstAlbumDir;
    int dirItems;
    VFSDirInfo dirInfos[MAX_DIR_ITEMS];
    PI_ERR result = 0;

    if (name) {
        srcAlbumDir = strcat(strcat(strcpy(tmp ,root), "/"), name);
        if (dlp_VFSFileOpen(sd, volRef, srcAlbumDir, vfsModeRead, &dirRef) < 0) {
            jp_logf(L_FATAL, "%s:    ERROR: Could not open dir '%s' on volume %d\n", MYNAME, srcAlbumDir, volRef);
            return -2;
        }
    } else {
        srcAlbumDir = (char *)root;
    }
    if (!(dstAlbumDir = destinationDir(sd, volRef, name))) {
        jp_logf(L_FATAL, "%s:    ERROR: Could not open dir '%s'\n", MYNAME, dstAlbumDir);
        result = -2;
        goto Exit;
    }
    jp_logf(L_GUI, "%s:    Fetching album '%s' in '%s' on volume %d ...\n", MYNAME, name ? name : ".", root, volRef);

    // Iterate over all the files in the album dir, looking for jpegs and 3gp's and 3g2's (videos).
    unsigned long itr = (unsigned long)vfsIteratorStart;
    //enum dlpVFSFileIteratorConstants itr = vfsIteratorStart; // doesn't work because of type mismatch bug <https://github.com/juddmon/jpilot/issues/39>
    int loops = 16; // for debugging
    //while (itr != (unsigned long)vfsIteratorStop) { // doesn't work because of bug <https://github.com/juddmon/jpilot/issues/39>
    //while (itr != (unsigned)vfsIteratorStop) { // doesn't work because of bug <https://github.com/juddmon/jpilot/issues/41>
    for (int dirItems_init = MIN_DIR_ITEMS; (dirItems = dirItems_init) <= MAX_DIR_ITEMS; dirItems_init *= 2) { // WORKAROUND
        if (--loops < 0)  break; // for debugging
        itr = (unsigned long)vfsIteratorStart; // workaround, reset itr if it wrongly was -1 or 1888
        jp_logf(L_DEBUG, "%s:     Enumerate album '%s', dirRef=%8lx, itr=%4lx, dirItems=%d\n", MYNAME, srcAlbumDir, dirRef, itr, dirItems);
        if ((result = dlp_VFSDirEntryEnumerate(sd, dirRef, &itr, &dirItems, dirInfos)) < 0) {
            // Further research is neccessary (see: <https://github.com/juddmon/jpilot/issues/41>):
            // - Why in case of i.e. setting dirItems=4, itr != 0, even if there are more than 4 files?
            // - Why then on SDCard itr == 1888 in the first loop, so out of allowed range?
            jp_logf(L_FATAL, "%s:     Enumerate ERROR: result=%4d, dirRef=%8lx, itr=%4lx, dirItems=%d\n", MYNAME, result, dirRef, itr, dirItems);
            goto Exit;
        }
        jp_logf(L_DEBUG, "%s:     Enumerate OK: result=%4d, dirRef=%8lx, itr=%4lx, dirItems=%d\n", MYNAME, result, dirRef, itr, dirItems);
        for (int i= dirItems_init==MIN_DIR_ITEMS ? 0 : dirItems_init/2; i<dirItems; i++) {
            jp_logf(L_DEBUG, "%s:      dirItem %3d: '%s' attribute %x\n", MYNAME, i, dirInfos[i].name, dirInfos[i].attr);
        }
        if (dirItems < dirItems_init) {
            break;
        }
    }
    jp_logf(L_DEBUG, "%s:     Now search of %d files, which to fetch ...\n", MYNAME, dirItems);
    for (int i=0; i<dirItems; i++) {
        char *fname = dirInfos[i].name;
        jp_logf(L_DEBUG, "%s:      Found file '%s' attribute %x\n", MYNAME, fname, dirInfos[i].attr);
        // Grab only regular files, but ignore the 'read only' and 'archived' bits,
        // and only with known extensions.
        if (dirInfos[i].attr & (
                vfsFileAttrHidden      |
                vfsFileAttrSystem      |
                vfsFileAttrVolumeLabel |
                vfsFileAttrDirectory   |
                vfsFileAttrLink)  ||
                strlen(fname) < 2 ||
                casecmpFileTypeList(fname)) {
            continue;
        }
        if (fetchFileIfNeeded(sd, volRef, srcAlbumDir, dstAlbumDir, fname) < 0) {
            result = -1;
        }
    }
    free(dstAlbumDir);
Exit:
    if (name)  dlp_VFSFileClose(sd, dirRef);
    jp_logf(L_DEBUG, "%s:    Album '%s' done -> result=%d\n", MYNAME,  srcAlbumDir, result);
    return result;
}

/*
 *  Backup all albums from volume volRef.
 */
int backupVolume(const int sd, int volRef) {
    PI_ERR rootResult = -3, result = 0;

    jp_logf(L_DEBUG, "%s:  Searching roots on volume %d\n", MYNAME, volRef);
    for (int d = 0; d < sizeof(ROOTDIRS)/sizeof(*ROOTDIRS); d++) {

        // Iterate through the root directory, looking for things that might be albums.
        FileRef dirRef;
        if (dlp_VFSFileOpen(sd, volRef, ROOTDIRS[d], vfsModeRead, &dirRef) < 0) {
            jp_logf(L_DEBUG, "%s:   Root '%s' does not exist on volume %d\n", MYNAME, ROOTDIRS[d], volRef);
            continue;
        }
        jp_logf(L_DEBUG, "%s:   Opened root '%s' on volume %d\n", MYNAME, ROOTDIRS[d], volRef);
        rootResult = 0;

        // Fetch the unfiled album, which is simply the root dir.
        // Apparently the Treo 650 can store pics in the root dir, as well as in album dirs.
        result = fetchAlbum(sd, volRef, dirRef, ROOTDIRS[d], NULL);

        //enum dlpVFSFileIteratorConstants itr = vfsIteratorStart;
        //while (itr != vfsIteratorStop) { // doesn't work because of type mismatch bug <https://github.com/juddmon/jpilot/issues/39>
        unsigned long itr = (unsigned long)vfsIteratorStart;
        while ((enum dlpVFSFileIteratorConstants)itr != vfsIteratorStop) {
            int dirItems = 1024;
            VFSDirInfo dirInfos[dirItems];
            jp_logf(L_DEBUG, "%s:   Enumerate root '%s', dirRef=%8lx, itr=%4lx, dirItems=%d\n", MYNAME, ROOTDIRS[d], dirRef, itr, dirItems);
            PI_ERR enRes;
            if ((enRes = dlp_VFSDirEntryEnumerate(sd, dirRef, &itr, &dirItems, dirInfos)) < 0) {
                // Further research is neccessary (see: <https://github.com/juddmon/jpilot/issues/41> ):
                // - Why in case of i.e. setting dirItems=4, itr == vfsIteratorStop, even if there are more than 4 files?
                // - For workaround and additional bug on SDCard volume, see at fetchAlbum()
                jp_logf(L_FATAL, "%s:   Enumerate ERROR: enRes=%4d, dirRef=%8lx, itr=%4lx, dirItems=%d\n", MYNAME, enRes, dirRef, itr, dirItems);
                rootResult = -3;
                break;
            } else {
                jp_logf(L_DEBUG, "%s:   Enumerate OK: enRes=%4d, dirRef=%8lx, itr=%4lx, dirItems=%d\n", MYNAME, enRes, dirRef, itr, dirItems);
            }
            jp_logf(L_DEBUG, "%s:   Now search for albums to fetch ...\n", MYNAME);
            for (int i=0; i<dirItems; i++) {
                jp_logf(L_DEBUG, "%s:    Found album candidate '%s'\n", MYNAME,  dirInfos[i].name);
                // Treo 650 has #Thumbnail dir that is not an album
                if (dirInfos[i].attr & vfsFileAttrDirectory && (synchThumbnailsAlbum || strcmp(dirInfos[i].name, "#Thumbnail"))) {
                    jp_logf(L_DEBUG, "%s:    Found real album '%s'\n", MYNAME, dirInfos[i].name);
                    int albumResult = fetchAlbum(sd, volRef, 0, ROOTDIRS[d], dirInfos[i].name);
                    result = MIN(result, albumResult);
                }
            }
        }
        dlp_VFSFileClose(sd, dirRef);
    }
    jp_logf(L_DEBUG, "%s:  Volume %d done -> rootResult=%d, result=%d\n", MYNAME,  volRef, rootResult, result);
    return rootResult + result;
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
 *  sd            --> socket descriptor
 *  volume_count  <-> on input, size of volumes; on output
 *                    number of volumes on Palm
 *  volumes       <-- volume reference numbers
 *
 * Returns:       <-- same as dlp_VFSVolumeEnumerate()
 *
 ***********************************************************************/
int volumeEnumerateIncludeHidden(const int sd, int *numVols, int *volRefs) {
    PI_ERR   result;
    VFSInfo  volInfo;

    // result on Treo 650:
    // -301 : No volume (SDCard) found, but maybe hidden volume 1 exists
    //    4 : At least one volume found, but maybe additional hidden volume 1 exists
    result = dlp_VFSVolumeEnumerate(sd, numVols, volRefs);
    jp_logf(L_DEBUG, "%s: dlp_VFSVolumeEnumerate result code %d, found %d volumes\n", MYNAME, result, *numVols);
    // On the Centro, Treo 650 and maybe more, it appears that the
    // first non-hidden volRef is 2, and the hidden volRef is 1.
    // Let's poke around to see, if there is really a volRef 1
    // that's hidden from the dlp_VFSVolumeEnumerate().
    if (result < 0)  *numVols = 0; // On Error reset numVols
    for (int i=0; i<*numVols; i++) { // Search for volume 1
        jp_logf(L_DEBUG, "%s: *numVols=%d, volRefs[%d]=%d\n", MYNAME, *numVols, i, volRefs[i]);
        if (volRefs[i]==1)
            goto Exit; // No need to search for hidden volume
    }
    if (dlp_VFSVolumeInfo(sd, 1, &volInfo) >= 0 && volInfo.attributes & vfsVolAttrHidden) {
        jp_logf(L_DEBUG, "%s: Found hidden volume 1\n", MYNAME);
        if (*numVols < MAX_VOLUMES)  (*numVols)++;
        else {
            jp_logf(L_FATAL, "%s: ERROR: Volumes > %d were discarded\n", MYNAME, MAX_VOLUMES);
        }
        for (int i = (*numVols)-1; i > 0; i--) { // Move existing volRefs
            jp_logf(L_DEBUG, "%s: *numVols=%d, volRefs[%d]=%d, volRefs[%d]=%d\n", MYNAME, *numVols, i-1, volRefs[i-1], i, volRefs[i]);
            volRefs[i] = volRefs[i-1];
        }
        volRefs[0] = 1;
        if (result < 0)
            result = 4; // fake dlp_VFSVolumeEnumerate() with 1 volume return value
    }
Exit:
    jp_logf(L_DEBUG, "%s: volumeEnumerateIncludeHidden found %d volumes -> result=%d\n", MYNAME, *numVols, result);
    return result;
}
