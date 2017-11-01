/*
 *  Module contains basic database file handling routines. Everything is implemented as generic as possible.
 *
 *  Changelog ( date, changes, author ):
 *  2012-08-30, Initial version, gaoesa
*/

#ifndef __G_DB_FILEHANDLING_H__
#define __G_DB_FILEHANDLING_H__

// definitions hiding the file open modes
#define DB_FILEMODE_READ "rb"
#define DB_FILEMODE_TRUNCATE "wb"
#define DB_FILEMODE_UPDATE "r+b"
/**
	Function opens any database file. Hides directory structure, always opens from inside the g_dbdirectory.
	Mode is passed to fopen as is and returns the handle for easy fail checks, same as using fopen directly

	@param file The handle to the file.
	@param name The name of the file to open. Path not included.
	@param mode Equals tofopen modes
	@return Handle to the opened file. Same as parameter file.
*/
FILE* G_DB_File_Open(FILE **file, const char *name, const char *mode);

/**
	Function close any database file.

	@param file The handle to the file to be closed.
 */
void G_DB_File_Close(FILE **file);

/**
 *	Function initializes the directory path for the module to use.
 *
 * @return 0 when succesfull, -1 if the path is too long
*/
int32_t G_DB_InitDirectoryPath( void );

/**
 *	Function deletes the named file in the database directory
 *
 * @param name The name of the file to delete.
 */
void G_DB_DeleteFile(const char *name);

/**
 *	Function renames the file. If a file with that name already exists, it will be deleted first.
 *
 * @param newName The name to what the file is renamed.
 * @param oldName The name of the file to rename.
 */
void G_DB_RenameFile(const char *oldName, const char *newName);

/**
 * Function sets the file position of an open file to the position starting from the beginning of the file.
 *
 * @param handle The handle to the file.
 * @param position The position starting from the beginning of the file. If set to -1, position is set to the end of the file.
 */
void G_DB_SetFilePosition(FILE *handle, int position);

/**
 * Function checks if the file has been read through.
 *
 * @param handle The handle to the file.
 * @return Non zero if the file position has reached to the file end. 0 otherwise.
 */
int G_DB_IsFileAtEnd(FILE *handle);

/**
 * Function returns the number of bytes between current position and the end of file.
 *
 * @param handle The handle to the file.
 * @return number of bytes until the EOF from the current position-
 */
int G_DB_GetRemainingByteCount(FILE *handle);

/**
 *	Function reads any size of block from any position of the file. If the position is set as -1,
 *	the block is read from the current file position. With sequential reads, this may be preferred.
 *
 * @param handle The handle to the file to read.
 * @param block Pointer to the memory to place read data.
 * @param block_size The amount of data to read.
 * @param pos The optional file position if non negative. If set to -1, reads from the current position.
 * @return The position in the file where the block of data was read or -1 if failure.
 */
int G_DB_ReadBlockFromDBFile(FILE *handle, void *block, size_t block_size, int pos);

/**
 *	Function writes any size of block to any position in the file. File must be opened before.
 *	If the position parameter is -1, the block is written to the current position.
 *
 * @param handle The handle to the file to write.
 * @param block Pointer to the memory where the written data is.
 * @param block_size The amount of data to write.
 * @param pos The optional file position if non negative. If set to -1, writes to the current position.
 * @return file position where the block was written if success, -1 if can't write, -2 if possibly corrupted the file
 */
int G_DB_WriteBlockToFile(FILE *handle, void *block, size_t block_size, int pos);

#endif
