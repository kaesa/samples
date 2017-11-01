/*
 *  Module contains basic database file handling routines. Everything is implemented as generic as possible.
 *
 *  Changelog ( date, changes, author ):
 *  2012-08-30, Initial version, gaoesa
*/

#include "g_local.h"

// moved to g_local.h
//typedef struct filesystem_info_s {
//	char directory_path[2048];
//} filesystem_info_t;

static filesystem_info_t filesystem_info;

static char* DB_CreateFullName(const char* file)
{
	static char fullname[2048];

	*fullname = '\0';
	Q_strncpyz(fullname, filesystem_info.directory_path, sizeof(fullname));
	Q_strcat(fullname, sizeof(fullname), file);

	return fullname;
}

/**
 *	Function initializes the directory path for the module to use.
 *
 * @return 0 when succesfull, -1 if the path is too long
*/
int32_t G_DB_InitDirectoryPath( void )
{
	// the only place where g_dbDirectory is accessed
	size_t					bytes;
	char					*buffer = filesystem_info.directory_path;
	uint32_t				dir_path_size = sizeof(filesystem_info.directory_path);

	// building the path, [userdefined folder] and filename
	trap_Cvar_VariableStringBuffer("fs_homepath", buffer, dir_path_size);
	bytes=strlen(buffer);	// GCC says the ET function trap_Cvar_VariableStringBuffer will never fail
							// in GCC we trust :P
	if(bytes && !(buffer[bytes-1]=='\\' || buffer[bytes-1]=='/')) {
		if(bytes==dir_path_size) {
			return -1;
		}
		buffer[bytes]=PATH_SEP;
		buffer[++bytes]='\0';
	}
	trap_Cvar_VariableStringBuffer("fs_game", &buffer[bytes], dir_path_size-bytes);
	bytes=strlen(buffer);
	if(bytes && !(buffer[bytes-1]=='\\' || buffer[bytes-1]=='/')) {
		if(bytes==dir_path_size) {
			return -1;
		}
		buffer[bytes]=PATH_SEP;
		buffer[++bytes]='\0';
	}
	Q_strcat(buffer, dir_path_size, g_dbDirectory.string);  
	bytes=strlen(buffer);
	if(bytes && !(buffer[bytes-1]=='\\' || buffer[bytes-1]=='/')) {
		if(bytes==dir_path_size) {
			return -1;
		}
		buffer[bytes]=PATH_SEP;
		buffer[++bytes]='\0';
	}

	return 0;
}

FILE* G_DB_File_Open(FILE **file, const char *name, const char *mode)
{
	char *filename = DB_CreateFullName(name);
	*file=fopen(filename, mode);

	return *file;
}

void G_DB_File_Close(FILE **file)
{
	if(*file) {
		fclose(*file);
		*file=NULL;
	}
}

void G_DB_DeleteFile(const char *name)
{
	remove( DB_CreateFullName(name) );
}

void G_DB_RenameFile(const char *oldName, const char *newName)
{
	char oldfile[2048];
	char *newfile;

	Q_strncpyz(oldfile, DB_CreateFullName(oldName), sizeof(oldfile));
	newfile = DB_CreateFullName(newName);
	remove(newfile);
	rename(oldfile, newfile);
}

void G_DB_SetFilePosition(FILE *handle, int position)
{
	if( position != -1 ) {
		fseek(handle, position, SEEK_SET);
	} else {
		fseek(handle, 0, SEEK_END);
	}
}

int G_DB_IsFileAtEnd(FILE *handle)
{
	return feof(handle);
}

int G_DB_GetRemainingByteCount(FILE *handle)
{
	int curpos, endpos;
	
	curpos = ftell(handle);

	fseek(handle, 0, SEEK_END);
	endpos = ftell(handle);

	fseek(handle, curpos, SEEK_SET);

	return (endpos - curpos);
}

int G_DB_ReadBlockFromDBFile(FILE *handle, void *block, size_t block_size, int pos)
{
	int bytes, position;

	if( pos != -1 ) {
		fseek(handle, pos, SEEK_SET);
	}

	position = ftell(handle);

	bytes = fread(block, block_size, 1, handle);
	if( bytes != 1 ) {
		G_LogPrintf("  File Error: Failed to read data from file.\n");
		return -1;
	}

	return position;
}

int G_DB_WriteBlockToFile(FILE *handle, void *block, size_t block_size, int pos)
{
	int bytes, filePos;

	if( pos != -1 ) {
		bytes = fseek(handle, pos, SEEK_SET);
		if(bytes) {
			G_LogPrintf("  OS Error: Failed to set file position for write.\n");
			return -1;
		}
		filePos = pos;
	} else {
		filePos = ftell(handle);
	}

	bytes = fwrite(block, block_size, 1, handle);
	if( bytes != 1 ) {
		// This shouldn't happen but if it does, the db is corrupted
		G_LogPrintf("  OS Error: Failed to write data to file. Will possibly corrupt database.\n");
		return -2;
	}
	if( (pos != -1) && fflush(handle) ) {
		// flush only on non sequential writes (i.e. position is given)
		G_LogPrintf("  OS Error: Failed to flush file.\n");
		return -2;
	}

	return filePos;
}
